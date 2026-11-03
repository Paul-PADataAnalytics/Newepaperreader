#include "reader/BookCover.h"
#include "AppStorage.h"
#include "FileBrowser.h"
#include "EpubParser.h"
#include "DisplayHAL.h"
#include "SystemSettings.h"
#include <tjpgd.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <list>
#include <memory>
#include <sys/stat.h>
#include <utility>
#include <vector>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#define STBI_MALLOC(size) ps_malloc(size)
#define STBI_REALLOC(pointer, size) ps_realloc(pointer, size)
#define STBI_FREE(pointer) free(pointer)
#endif

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

namespace {

const char* kCoverDir = "/data/covers";
const char kMagic[4] = {'C', 'V', 'R', '1'};
// Largest intermediate greyscale image we will hold while resizing.
constexpr uint64_t kMaxDecodedPixels = 4u * 1024u * 1024u;

struct Grey {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;
};

// 16-level image as stored in the cache files: two pixels per byte, left
// pixel in the high nibble, rows padded to a whole byte.
struct Packed {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> data;
    int stride() const { return (w + 1) / 2; }
    uint8_t at(int x, int y) const {
        const uint8_t b = data[(size_t)y * stride() + x / 2];
        return (x & 1) ? (b & 0x0F) : (b >> 4);
    }
};

std::string runtimePath(const std::string& sdPath) {
    return AppStorage::toVfsPath(AppStorage::toRuntimePath(sdPath));
}

std::string coverFile(const std::string& id, const char* extension) {
    return runtimePath(std::string(kCoverDir) + "/" + id + "." + extension);
}

void ensureCoverDir() {
    mkdir(runtimePath("/data").c_str(), 0775);
    mkdir(runtimePath(kCoverDir).c_str(), 0775);
}

uint32_t nowMs() {
#ifndef NATIVE_TESTING
    return millis();
#else
    return (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000));
#endif
}

void cooperativeYield() {
#ifndef NATIVE_TESTING
    // The worker shares core 0 with the radio stacks and the idle task
    // (which feeds the task watchdog); give them a tick now and then.
    static uint32_t calls = 0;
    if ((++calls & 7) == 0) vTaskDelay(1);
#endif
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

// Fitted size of a w x h image inside a box, aspect ratio preserved.
void fitInBox(int w, int h, int boxW, int boxH, int& outW, int& outH) {
    const double scale = std::min((double)boxW / w, (double)boxH / h);
    outW = std::max(1, (int)std::lround(w * scale));
    outH = std::max(1, (int)std::lround(h * scale));
}

struct JpegContext {
    EpubParser* epub;
    void* stream;
    Grey* image;
};

int jpegOutput(JDEC* decoder, void* bitmap, JRECT* rect) {
    JpegContext* context = static_cast<JpegContext*>(decoder->device);
    Grey& image = *context->image;
    const uint8_t* source = static_cast<const uint8_t*>(bitmap);
    const int width = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom; ++y) {
        if (y < image.h) {
            const int count = std::min(width, image.w - rect->left);
            if (count > 0) memcpy(&image.px[(size_t)y * image.w + rect->left], source, (size_t)count);
        }
        source += width;
    }
    return 1;
}

size_t jpegInput(JDEC* decoder, uint8_t* buffer, size_t length) {
    JpegContext* context = static_cast<JpegContext*>(decoder->device);
    cooperativeYield();
    if (buffer) return context->epub->readStream(context->stream, buffer, length);
    // A null buffer asks us to skip bytes.
    uint8_t scratch[256];
    size_t skipped = 0;
    while (skipped < length) {
        const size_t chunk = std::min(sizeof(scratch), length - skipped);
        const size_t got = context->epub->readStream(context->stream, scratch, chunk);
        if (got == 0) break;
        skipped += got;
    }
    return skipped;
}

enum class DecodeResult { OK, UNSUPPORTED, FAILED };

// Baseline JPEG straight from the zip stream, descaled by 1/2..1/8 while
// decoding so even 4000-pixel covers need well under a megabyte.
DecodeResult decodeJpegStream(EpubParser& epub, const std::string& entry, int boxW, int boxH, Grey& image) {
    size_t size = 0;
    void* stream = epub.openStream(entry, size);
    if (!stream) return DecodeResult::FAILED;

    constexpr size_t kPoolSize = 32 * 1024;
    std::unique_ptr<uint8_t, void (*)(void*)> pool(static_cast<uint8_t*>(malloc(kPoolSize)), free);
    JpegContext context{&epub, stream, &image};
    JDEC decoder;
    DecodeResult result = DecodeResult::FAILED;
    JRESULT prepared = pool ? jd_prepare(&decoder, jpegInput, pool.get(), kPoolSize, &context) : JDR_MEM1;
    if (prepared == JDR_FMT3) {
        result = DecodeResult::UNSUPPORTED; // progressive or other unsupported variant
    } else if (prepared == JDR_OK && decoder.width > 0 && decoder.height > 0) {
        // Coarsest descaling that still leaves at least the fitted box size.
        int fitW = 0, fitH = 0;
        fitInBox(decoder.width, decoder.height, boxW, boxH, fitW, fitH);
        uint8_t scale = 0;
        while (scale < 3 && (decoder.width >> (scale + 1)) >= fitW && (decoder.height >> (scale + 1)) >= fitH) {
            ++scale;
        }
        while (scale < 3 && (uint64_t)(decoder.width >> scale) * (decoder.height >> scale) > kMaxDecodedPixels) {
            ++scale;
        }
        image.w = (decoder.width + (1 << scale) - 1) >> scale;
        image.h = (decoder.height + (1 << scale) - 1) >> scale;
        if ((uint64_t)image.w * image.h <= kMaxDecodedPixels) {
            image.px.assign((size_t)image.w * image.h, 0xFF);
            if (jd_decomp(&decoder, jpegOutput, scale) == JDR_OK) result = DecodeResult::OK;
        }
    }
    epub.closeStream(stream);
    if (result != DecodeResult::OK) {
        image = Grey();
    }
    return result;
}

// ---------------------------------------------------------------------------
// Progressive JPEG fallback: an 1/8-scale image from the DC coefficients.
// stb_image keeps every coefficient of a progressive JPEG in memory (about
// six bytes per pixel - over 20 MB for a typical cover), which the device
// can't spare; the DC terms alone are each 8x8 block's average and need only
// a few hundred KB. Plenty for thumbnails; the sleep-screen cover is softer.
// ---------------------------------------------------------------------------

struct HuffTable {
    bool present = false;
    uint8_t symbols[256] = {};
    int minCode[17] = {};
    int maxCode[17] = {};
    int valuePointer[17] = {};
};

bool buildHuffTable(const uint8_t* counts, const uint8_t* symbols, int total, HuffTable& table) {
    if (total > 256) return false;
    memcpy(table.symbols, symbols, (size_t)total);
    int code = 0, k = 0;
    for (int length = 1; length <= 16; ++length) {
        table.valuePointer[length] = k;
        table.minCode[length] = code;
        code += counts[length - 1];
        k += counts[length - 1];
        table.maxCode[length] = counts[length - 1] ? code - 1 : -1;
        code <<= 1;
    }
    table.present = true;
    return true;
}

struct JpegBits {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t value = 0;
    int count = 0;
    bool atMarker = false;

    int nextBit() {
        if (count == 0) {
            uint8_t b = 0;
            if (!atMarker && p < end) {
                b = *p;
                if (b == 0xFF) {
                    if (p + 1 < end && p[1] == 0x00) {
                        p += 2;
                    } else {
                        atMarker = true; // feed zeros until the scan ends
                        b = 0;
                    }
                } else {
                    ++p;
                }
            }
            value = b;
            count = 8;
        }
        --count;
        return (value >> count) & 1;
    }

    int bits(int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | nextBit();
        return v;
    }

    int decode(const HuffTable& table) {
        int code = 0;
        for (int length = 1; length <= 16; ++length) {
            code = (code << 1) | nextBit();
            if (table.maxCode[length] >= 0 && code <= table.maxCode[length] && code >= table.minCode[length]) {
                return table.symbols[table.valuePointer[length] + code - table.minCode[length]];
            }
        }
        return -1;
    }

    // Skips to just past the next RSTn marker (byte-aligned).
    void restart() {
        count = 0;
        atMarker = false;
        while (p + 1 < end && !(p[0] == 0xFF && p[1] >= 0xD0 && p[1] <= 0xD7)) ++p;
        if (p + 1 < end) p += 2;
    }

    // Position of the marker that ends the current entropy-coded segment.
    const uint8_t* nextMarker() const {
        const uint8_t* q = p;
        while (q + 1 < end && !(q[0] == 0xFF && q[1] != 0x00 && !(q[1] >= 0xD0 && q[1] <= 0xD7))) ++q;
        return q;
    }
};

int extendSign(int value, int bits) {
    return bits == 0 ? 0 : (value < (1 << (bits - 1)) ? value - (1 << bits) + 1 : value);
}

bool isProgressiveJpeg(const uint8_t* data, size_t length) {
    if (length < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 4 <= length && data[i] == 0xFF) {
        const uint8_t marker = data[i + 1];
        if (marker == 0xC2) return true;
        if (marker == 0xDA || marker == 0xC0 || marker == 0xC1) return false;
        i += 2 + ((data[i + 2] << 8) | data[i + 3]);
    }
    return false;
}

bool decodeProgressiveDc(const uint8_t* data, size_t length, Grey& image) {
    struct Component {
        int id = 0, h = 1, v = 1, q = 0, dcTable = 0, predictor = 0;
    };
    Component components[4];
    int componentCount = 0, width = 0, height = 0, maxH = 1, maxV = 1, restartInterval = 0;
    uint16_t quant[4][64] = {};
    HuffTable dcTables[4];
    std::vector<int> luma; // DC of each luma block, MCU-padded
    int lumaStride = 0, mcusX = 0, mcusY = 0;
    bool frame = false;

    const uint8_t* p = data + 2;
    const uint8_t* end = data + length;
    if (length < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) { ++p; continue; }
        const uint8_t marker = p[1];
        if (marker == 0xFF) { ++p; continue; }
        if (marker == 0xD9) break;
        const int segment = (p[2] << 8) | p[3];
        const uint8_t* s = p + 4;
        const uint8_t* next = p + 2 + segment;
        if (segment < 2 || next > end) return false;
        if (marker == 0xDB) {
            while (s < next) {
                const int precision = s[0] >> 4, index = s[0] & 3;
                ++s;
                for (int k = 0; k < 64 && s < next; ++k) {
                    quant[index][k] = precision ? (uint16_t)((s[0] << 8) | s[1]) : s[0];
                    s += precision ? 2 : 1;
                }
            }
        } else if (marker == 0xC4) {
            while (s + 17 <= next) {
                const int tableClass = s[0] >> 4, index = s[0] & 3;
                int total = 0;
                for (int k = 0; k < 16; ++k) total += s[1 + k];
                if (s + 17 + total > next) return false;
                HuffTable ignored;
                if (!buildHuffTable(s + 1, s + 17, total, tableClass == 0 ? dcTables[index] : ignored)) return false;
                s += 17 + total;
            }
        } else if (marker == 0xDD) {
            restartInterval = (s[0] << 8) | s[1];
        } else if (marker == 0xC2) {
            height = (s[1] << 8) | s[2];
            width = (s[3] << 8) | s[4];
            componentCount = s[5];
            if (width <= 0 || height <= 0 || componentCount < 1 || componentCount > 4) return false;
            for (int c = 0; c < componentCount; ++c) {
                components[c].id = s[6 + c * 3];
                components[c].h = std::max(1, s[7 + c * 3] >> 4);
                components[c].v = std::max(1, s[7 + c * 3] & 15);
                components[c].q = s[8 + c * 3] & 3;
                maxH = std::max(maxH, components[c].h);
                maxV = std::max(maxV, components[c].v);
            }
            mcusX = (width + 8 * maxH - 1) / (8 * maxH);
            mcusY = (height + 8 * maxV - 1) / (8 * maxV);
            lumaStride = mcusX * components[0].h;
            if ((uint64_t)lumaStride * mcusY * components[0].v > kMaxDecodedPixels) return false;
            luma.assign((size_t)lumaStride * mcusY * components[0].v, 0);
            frame = true;
        } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            return false; // not a (Huffman) progressive JPEG
        } else if (marker == 0xDA) {
            if (!frame) return false;
            const int inScan = s[0];
            int scan[4];
            for (int i = 0; i < inScan && i < 4; ++i) {
                scan[i] = -1;
                for (int c = 0; c < componentCount; ++c) {
                    if (components[c].id == s[1 + i * 2]) scan[i] = c;
                }
                if (scan[i] < 0) return false;
                components[scan[i]].dcTable = s[2 + i * 2] >> 4;
            }
            const uint8_t* tail = s + 1 + inScan * 2;
            const int spectralStart = tail[0], approxHigh = tail[2] >> 4, approxLow = tail[2] & 15;
            JpegBits bits;
            bits.p = next;
            bits.end = end;
            if (spectralStart == 0) {
                for (int i = 0; i < inScan; ++i) {
                    components[scan[i]].predictor = 0;
                    if (approxHigh == 0 && !dcTables[components[scan[i]].dcTable].present) return false;
                }
                // Interleaved scans walk MCUs; single-component scans walk
                // that component's own blocks.
                const bool single = inScan == 1;
                const Component& only = components[scan[0]];
                const int compW = (width * only.h + maxH - 1) / maxH;
                const int compH = (height * only.v + maxV - 1) / maxV;
                const int unitsX = single ? (compW + 7) / 8 : mcusX;
                const int unitsY = single ? (compH + 7) / 8 : mcusY;
                int sinceRestart = 0;
                for (int uy = 0; uy < unitsY; ++uy) {
                    for (int ux = 0; ux < unitsX; ++ux) {
                        if (restartInterval && sinceRestart == restartInterval) {
                            bits.restart();
                            sinceRestart = 0;
                            for (int i = 0; i < inScan; ++i) components[scan[i]].predictor = 0;
                        }
                        ++sinceRestart;
                        for (int i = 0; i < inScan; ++i) {
                            Component& c = components[scan[i]];
                            const int blocksH = single ? 1 : c.h, blocksV = single ? 1 : c.v;
                            for (int by = 0; by < blocksV; ++by) {
                                for (int bx = 0; bx < blocksH; ++bx) {
                                    int dc;
                                    if (approxHigh == 0) {
                                        const int size = bits.decode(dcTables[c.dcTable]);
                                        if (size < 0 || size > 16) return false;
                                        c.predictor += extendSign(bits.bits(size), size);
                                        dc = c.predictor << approxLow;
                                    } else {
                                        dc = bits.nextBit() << approxLow;
                                    }
                                    if (scan[i] != 0) continue;
                                    const int blockX = single ? ux : ux * c.h + bx;
                                    const int blockY = single ? uy : uy * c.v + by;
                                    if (blockX >= lumaStride || blockY * lumaStride >= (int)luma.size()) continue;
                                    int& stored = luma[(size_t)blockY * lumaStride + blockX];
                                    stored = approxHigh == 0 ? dc : (stored | dc);
                                }
                            }
                        }
                    }
                    cooperativeYield();
                }
            }
            p = bits.nextMarker(); // AC scans are skipped entirely
            continue;
        }
        p = next;
    }
    if (!frame) return false;

    const int lumaW = (width * components[0].h + maxH - 1) / maxH;
    const int lumaH = (height * components[0].v + maxV - 1) / maxV;
    image.w = (lumaW + 7) / 8;
    image.h = (lumaH + 7) / 8;
    image.px.resize((size_t)image.w * image.h);
    const int scale = quant[components[0].q][0];
    for (int y = 0; y < image.h; ++y) {
        for (int x = 0; x < image.w; ++x) {
            const int value = 128 + (luma[(size_t)y * lumaStride + x] * scale) / 8;
            image.px[(size_t)y * image.w + x] = (uint8_t)std::max(0, std::min(255, value));
        }
    }
    return true;
}

// Bytes stb_image is likely to need for a w x h image (all component planes,
// progressive coefficients, output) - used to refuse decodes that can't fit.
uint64_t stbBudget(int w, int h, int components, bool progressive) {
    const uint64_t pixels = (uint64_t)w * h;
    if (progressive) return pixels * 6 + 128 * 1024;
    return pixels * (uint64_t)(components + 2) + 128 * 1024;
}

bool enoughMemory(uint64_t bytes) {
#ifndef NATIVE_TESTING
    const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    return bytes + 256 * 1024 < freeBytes && bytes / 2 < largest;
#else
    return bytes < 512ull * 1024 * 1024;
#endif
}

// Whole-file decoders for what the streaming decoder can't do (PNG,
// progressive and unusual JPEGs): stb_image when the image fits in memory,
// else the DC-only fallback for progressive JPEGs.
bool decodeInMemory(EpubParser& epub, const std::string& entry, Grey& image, const char*& method) {
    size_t length = 0;
    char* bytes = epub.getFileContent(entry, length);
    if (!bytes) return false;
    const stbi_uc* data = reinterpret_cast<const stbi_uc*>(bytes);
    const bool progressive = isProgressiveJpeg(data, length);
    int w = 0, h = 0, components = 0;
    bool ok = false;
    if (stbi_info_from_memory(data, (int)length, &w, &h, &components) && w > 0 && h > 0 &&
        enoughMemory(stbBudget(w, h, components, progressive) + length)) {
        stbi_uc* grey = stbi_load_from_memory(data, (int)length, &w, &h, &components, 1);
        if (grey) {
            image.w = w;
            image.h = h;
            image.px.assign(grey, grey + (size_t)w * h);
            stbi_image_free(grey);
            method = "stb";
            ok = true;
        }
    }
    if (!ok && progressive) {
        ok = decodeProgressiveDc(data, length, image);
        method = "progressive DC";
    }
    free(bytes);
    return ok;
}

// ---------------------------------------------------------------------------
// Resizing and dithering
// ---------------------------------------------------------------------------

// Per-destination-pixel source spans: area averaging when shrinking,
// bilinear-ish (two taps) when enlarging. Weights are in 1/256ths.
struct Taps {
    std::vector<int> start;
    std::vector<int> count;
    std::vector<int> offset;
    std::vector<uint16_t> weight;
};

Taps buildTaps(int source, int target) {
    Taps taps;
    taps.start.resize(target);
    taps.count.resize(target);
    taps.offset.resize(target);
    const double ratio = (double)source / target;
    for (int d = 0; d < target; ++d) {
        taps.offset[d] = (int)taps.weight.size();
        std::vector<double> weights;
        int first = 0;
        if (ratio >= 1.0) {
            const double a = d * ratio;
            const double b = std::min((double)source, (d + 1) * ratio);
            first = (int)std::floor(a);
            const int last = std::min(source, (int)std::ceil(b));
            for (int i = first; i < last; ++i) {
                weights.push_back(std::min(b, (double)i + 1) - std::max(a, (double)i));
            }
        } else {
            const double center = (d + 0.5) * ratio - 0.5;
            first = std::max(0, std::min(source - 1, (int)std::floor(center)));
            const int second = std::min(source - 1, first + 1);
            const double t = std::max(0.0, std::min(1.0, center - first));
            weights.push_back(1.0 - t);
            if (second != first) weights.push_back(t);
        }
        double total = 0;
        for (double w : weights) total += w;
        int assigned = 0;
        for (size_t i = 0; i < weights.size(); ++i) {
            int w = (i + 1 == weights.size()) ? 256 - assigned : (int)std::lround(weights[i] / total * 256.0);
            w = std::max(0, w);
            assigned += w;
            taps.weight.push_back((uint16_t)w);
        }
        taps.start[d] = first;
        taps.count[d] = (int)weights.size();
    }
    return taps;
}

Grey resize(const Grey& source, int targetW, int targetH) {
    const Taps horizontal = buildTaps(source.w, targetW);
    const Taps vertical = buildTaps(source.h, targetH);
    // Horizontal pass into 8.8 fixed point rows, then vertical.
    std::vector<uint16_t> rows((size_t)source.h * targetW);
    for (int y = 0; y < source.h; ++y) {
        const uint8_t* in = &source.px[(size_t)y * source.w];
        uint16_t* out = &rows[(size_t)y * targetW];
        for (int x = 0; x < targetW; ++x) {
            uint32_t sum = 0;
            const uint16_t* w = &horizontal.weight[horizontal.offset[x]];
            const uint8_t* s = in + horizontal.start[x];
            for (int i = 0; i < horizontal.count[x]; ++i) sum += (uint32_t)s[i] * w[i];
            out[x] = (uint16_t)std::min<uint32_t>(sum, 65535);
        }
        if ((y & 63) == 0) cooperativeYield();
    }
    Grey result;
    result.w = targetW;
    result.h = targetH;
    result.px.resize((size_t)targetW * targetH);
    for (int y = 0; y < targetH; ++y) {
        const uint16_t* w = &vertical.weight[vertical.offset[y]];
        for (int x = 0; x < targetW; ++x) {
            uint32_t sum = 0;
            for (int i = 0; i < vertical.count[y]; ++i) {
                sum += (uint32_t)rows[(size_t)(vertical.start[y] + i) * targetW + x] * w[i];
            }
            result.px[(size_t)y * targetW + x] = (uint8_t)std::min<uint32_t>((sum + 32768) >> 16, 255);
        }
    }
    return result;
}

// Floyd-Steinberg (serpentine) down to the panel's 16 grey levels.
Packed dither(const Grey& image) {
    Packed packed;
    packed.w = image.w;
    packed.h = image.h;
    packed.data.assign((size_t)packed.stride() * packed.h, 0xFF);
    std::vector<int> current(image.w + 2, 0), next(image.w + 2, 0);
    for (int y = 0; y < image.h; ++y) {
        std::fill(next.begin(), next.end(), 0);
        const bool reverse = (y & 1) != 0;
        for (int i = 0; i < image.w; ++i) {
            const int x = reverse ? image.w - 1 - i : i;
            const int dir = reverse ? -1 : 1;
            const int value = std::max(0, std::min(255, image.px[(size_t)y * image.w + x] + current[x + 1] / 16));
            const int level = (value * 15 + 127) / 255;
            const int error = value - level * 17;
            current[x + 1 + dir] += error * 7;
            next[x + 1 - dir] += error * 3;
            next[x + 1] += error * 5;
            next[x + 1 + dir] += error * 1;
            uint8_t& b = packed.data[(size_t)y * packed.stride() + x / 2];
            b = (x & 1) ? (uint8_t)((b & 0xF0) | level) : (uint8_t)((b & 0x0F) | (level << 4));
        }
        std::swap(current, next);
    }
    return packed;
}

// ---------------------------------------------------------------------------
// Cache files
// ---------------------------------------------------------------------------

bool writePacked(const std::string& path, const Packed& image) {
    const std::string temporary = path + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) return false;
    uint8_t header[8];
    memcpy(header, kMagic, 4);
    header[4] = (uint8_t)(image.w & 0xFF);
    header[5] = (uint8_t)(image.w >> 8);
    header[6] = (uint8_t)(image.h & 0xFF);
    header[7] = (uint8_t)(image.h >> 8);
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
              fwrite(image.data.data(), 1, image.data.size(), file) == image.data.size();
    ok = (fclose(file) == 0) && ok;
    if (ok) {
        remove(path.c_str());
        ok = rename(temporary.c_str(), path.c_str()) == 0;
    }
    if (!ok) remove(temporary.c_str());
    return ok;
}

FILE* openPacked(const std::string& path, int& w, int& h) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return nullptr;
    uint8_t header[8];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, kMagic, 4) != 0) {
        fclose(file);
        return nullptr;
    }
    w = header[4] | (header[5] << 8);
    h = header[6] | (header[7] << 8);
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) {
        fclose(file);
        return nullptr;
    }
    return file;
}

bool readPacked(const std::string& path, Packed& image) {
    FILE* file = openPacked(path, image.w, image.h);
    if (!file) return false;
    image.data.resize((size_t)image.stride() * image.h);
    const bool ok = fread(image.data.data(), 1, image.data.size(), file) == image.data.size();
    fclose(file);
    return ok;
}

// Panel level for a cached level: the "Picture brightness" curve, and
// pre-inverted in dark mode (the whole frame is inverted when shown).
void buildLevelMap(uint8_t map[16]) {
    const float gamma = SystemSettings::imageGamma();
    const bool dark = DisplayHAL::isDarkMode();
    for (int i = 0; i < 16; ++i) {
        const int level = (int)std::lround(15.0f * std::pow(i / 15.0f, gamma));
        map[i] = (uint8_t)(dark ? 15 - level : level);
    }
}

// Small in-memory cache of thumbnails for redraws and page flips.
struct ThumbnailSlot {
    std::string id;
    Packed image;
};
std::list<ThumbnailSlot> s_thumbnails;
constexpr size_t kMaxThumbnails = 30;

const Packed* thumbnail(const std::string& id) {
    for (auto it = s_thumbnails.begin(); it != s_thumbnails.end(); ++it) {
        if (it->id == id) {
            s_thumbnails.splice(s_thumbnails.begin(), s_thumbnails, it);
            return &s_thumbnails.front().image;
        }
    }
    ThumbnailSlot slot;
    slot.id = id;
    if (!readPacked(coverFile(id, "thm"), slot.image)) return nullptr;
    s_thumbnails.push_front(std::move(slot));
    if (s_thumbnails.size() > kMaxThumbnails) s_thumbnails.pop_back();
    return &s_thumbnails.front().image;
}

// ---------------------------------------------------------------------------
// Background queue
// ---------------------------------------------------------------------------

struct Job {
    std::string path;
    std::string id;
};
std::deque<Job> s_jobs;
std::deque<std::pair<std::string, BookCover::State>> s_results;
bool s_working = false;
std::string s_currentId; // job being decoded right now

#ifndef NATIVE_TESTING
SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_worker = nullptr;
struct Lock {
    Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_mutex); }
};
#else
struct Lock {};
#endif

bool runOneJob() {
    Job job;
    {
        Lock lock;
        if (s_jobs.empty()) {
            s_working = false;
            return false;
        }
        job = s_jobs.front();
        s_jobs.pop_front();
        s_working = true;
        s_currentId = job.id;
    }
    const BookCover::State state = BookCover::generate(job.path, job.id);
    Lock lock;
    s_results.emplace_back(job.id, state);
    s_currentId.clear();
    return true;
}

#ifndef NATIVE_TESTING
void workerTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (runOneJob()) {
        }
    }
}
#endif

} // namespace

namespace BookCover {

std::string cacheId(const std::string& sdPath, uint32_t size, uint32_t mtime) {
    // FNV-1a over the path and the file's size/stamp: a replaced or edited
    // book gets a fresh cover.
    uint32_t hash = 2166136261u;
    auto mix = [&hash](const void* data, size_t length) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; ++i) {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
    };
    mix(sdPath.data(), sdPath.size());
    mix(&size, sizeof(size));
    mix(&mtime, sizeof(mtime));
    char text[9];
    snprintf(text, sizeof(text), "%08x", (unsigned)hash);
    return text;
}

std::string cacheIdForFile(const std::string& sdPath) {
    uint32_t size = 0, mtime = 0;
    if (!FileBrowser::fileStamp(sdPath, size, mtime)) return "";
    return cacheId(sdPath, size, mtime);
}

bool isEpub(const std::string& path) {
    if (path.size() < 5) return false;
    std::string extension = path.substr(path.size() - 5);
    for (char& c : extension) c = (char)tolower((unsigned char)c);
    return extension == ".epub";
}

bool drawThumbnail(const std::string& id, uint8_t* framebuffer, int x, int y, int w, int h,
                   int* drawnX, int* drawnY, int* drawnW, int* drawnH) {
    if (!framebuffer || id.empty()) return false;
    const Packed* image = thumbnail(id);
    if (!image) return false;
    uint8_t map[16];
    buildLevelMap(map);
    // Thumbnails are generated for the library box; centre (or crop) them
    // in whatever box the caller asks for.
    const int left = x + (w - image->w) / 2;
    const int top = y + (h - image->h) / 2;
    for (int cy = std::max(0, y - top); cy < image->h && top + cy < y + h; ++cy) {
        for (int cx = std::max(0, x - left); cx < image->w && left + cx < x + w; ++cx) {
            DisplayHAL::setPixel(left + cx, top + cy, map[image->at(cx, cy)], framebuffer);
        }
    }
    if (drawnX) *drawnX = std::max(x, left);
    if (drawnY) *drawnY = std::max(y, top);
    if (drawnW) *drawnW = std::min(image->w, w);
    if (drawnH) *drawnH = std::min(image->h, h);
    return true;
}

bool hasLarge(const std::string& id) {
    struct stat st;
    return !id.empty() && stat(coverFile(id, "big").c_str(), &st) == 0;
}

bool drawLarge(const std::string& id, uint8_t* framebuffer, int x, int y, int w, int h,
               int* drawnW, int* drawnH) {
    if (!framebuffer || id.empty()) return false;
    int imageW = 0, imageH = 0;
    FILE* file = openPacked(coverFile(id, "big"), imageW, imageH);
    if (!file) return false;
    int outW = imageW, outH = imageH;
    if (imageW > w || imageH > h) fitInBox(imageW, imageH, w, h, outW, outH);
    const int stride = (imageW + 1) / 2;
    std::vector<uint8_t> row(stride);
    uint8_t map[16];
    buildLevelMap(map);
    const int left = x + (w - outW) / 2;
    int loadedRow = -1;
    bool ok = true;
    for (int oy = 0; oy < outH && ok; ++oy) {
        const int sy = (int)((int64_t)oy * imageH / outH);
        while (loadedRow < sy && ok) {
            ok = fread(row.data(), 1, stride, file) == (size_t)stride;
            ++loadedRow;
        }
        for (int ox = 0; ox < outW && ok; ++ox) {
            const int sx = (int)((int64_t)ox * imageW / outW);
            const uint8_t b = row[sx / 2];
            DisplayHAL::setPixel(left + ox, y + oy, map[(sx & 1) ? (b & 0x0F) : (b >> 4)], framebuffer);
        }
    }
    fclose(file);
    if (drawnW) *drawnW = outW;
    if (drawnH) *drawnH = outH;
    return ok;
}

State generate(const std::string& sdPath, const std::string& id) {
    if (!isEpub(sdPath) || id.empty()) return STATE_NONE;
    const uint32_t started = nowMs();
    EpubParser epub;
    if (!epub.open(AppStorage::toRuntimePath(sdPath), false)) return STATE_NONE;
    const std::string entry = epub.findCoverImage();
    if (entry.empty()) return STATE_NONE;

    Grey decoded;
    const char* method = "streamed";
    if (decodeJpegStream(epub, entry, LARGE_W, LARGE_H, decoded) != DecodeResult::OK &&
        !decodeInMemory(epub, entry, decoded, method)) {
        printf("[Cover] no decodable cover in %s (%s)\n", sdPath.c_str(), entry.c_str());
        return STATE_NONE;
    }
    epub.close();

    ensureCoverDir();
    int w = 0, h = 0;
    fitInBox(decoded.w, decoded.h, LARGE_W, LARGE_H, w, h);
    const bool large = writePacked(coverFile(id, "big"), dither(resize(decoded, w, h)));
    fitInBox(decoded.w, decoded.h, THUMB_W, THUMB_H, w, h);
    const bool small = writePacked(coverFile(id, "thm"), dither(resize(decoded, w, h)));
    printf("[Cover] %s: %dx%d (%s) -> %s in %ums\n", sdPath.c_str(), decoded.w, decoded.h,
           method, id.c_str(), (unsigned)(nowMs() - started));
    return (large && small) ? STATE_READY : STATE_NONE;
}

void pruneCache(const std::vector<std::string>& keepIds) {
    std::vector<std::string> stale;
    FileBrowser::listDirectory(kCoverDir, [&](const FileInfo& file) {
        const std::string id = file.name.substr(0, file.name.find('.'));
        if (std::find(keepIds.begin(), keepIds.end(), id) == keepIds.end()) stale.push_back(file.path);
    });
    for (const std::string& path : stale) remove(runtimePath(path).c_str());
    s_thumbnails.clear();
}

void request(const std::string& sdPath, const std::string& id) {
    if (id.empty()) return;
#ifndef NATIVE_TESTING
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return;
#endif
    {
        Lock lock;
        if (s_currentId == id) return;
        for (const Job& job : s_jobs) {
            if (job.id == id) return;
        }
        for (const auto& result : s_results) {
            if (result.first == id) return;
        }
        s_jobs.push_back({sdPath, id});
    }
#ifndef NATIVE_TESTING
    if (!s_worker) {
        // Core 0 at the lowest priority: the UI loop on core 1 stays
        // responsive while a cover decodes.
        xTaskCreatePinnedToCore(workerTask, "covers", 16384, nullptr, 1, &s_worker, 0);
    }
    if (s_worker) xTaskNotifyGive(s_worker);
#endif
}

void cancelPending() {
#ifndef NATIVE_TESTING
    if (!s_mutex) return;
#endif
    Lock lock;
    s_jobs.clear();
}

void pump() {
#ifdef NATIVE_TESTING
    runOneJob();
#endif
}

bool takeResult(std::string& id, State& state) {
#ifndef NATIVE_TESTING
    if (!s_mutex) return false;
#endif
    Lock lock;
    if (s_results.empty()) return false;
    id = s_results.front().first;
    state = s_results.front().second;
    s_results.pop_front();
    return true;
}

bool busy() {
#ifndef NATIVE_TESTING
    if (!s_mutex) return false;
#endif
    Lock lock;
    return s_working || !s_jobs.empty();
}

void clearCache() {
    s_thumbnails.clear();
}

} // namespace BookCover
