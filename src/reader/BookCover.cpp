#include "reader/BookCover.h"
#include "reader/AppStorage.h"
#include "EpubParser.h"
#include "DisplayHAL.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sys/stat.h>
#include <vector>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#define STBI_MALLOC(size) ps_malloc(size)
#define STBI_REALLOC(pointer, size) ps_realloc(pointer, size)
#define STBI_FREE(pointer) free(pointer)
#endif

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace {

struct DecodedCover {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

std::map<std::string, DecodedCover> s_coverCache;

std::string cacheKey(const std::string& path, int width, int height, const struct stat& st) {
    return path + ":" + std::to_string(static_cast<unsigned long long>(st.st_size)) +
           ":" + std::to_string(static_cast<long long>(st.st_mtime)) +
           ":" + std::to_string(width) + ":" + std::to_string(height);
}

bool decodeCover(const std::string& bookPath, int width, int height, DecodedCover& cover) {
    EpubParser epub;
    if (!epub.open(bookPath) || epub.getCoverPath().empty()) return false;

    size_t byteCount = 0;
    char* bytes = epub.getFileContent(epub.getCoverPath(), byteCount);
    if (!bytes || byteCount > 4 * 1024 * 1024) {
        free(bytes);
        return false;
    }

    int imageWidth = 0;
    int imageHeight = 0;
    int channels = 0;
    const bool valid = stbi_info_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes), static_cast<int>(byteCount),
        &imageWidth, &imageHeight, &channels) != 0;
    if (!valid || imageWidth <= 0 || imageHeight <= 0 ||
        static_cast<uint64_t>(imageWidth) * static_cast<uint64_t>(imageHeight) > 4000000) {
        free(bytes);
        return false;
    }

    stbi_uc* gray = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes), static_cast<int>(byteCount),
        &imageWidth, &imageHeight, &channels, 1);
    free(bytes);
    if (!gray) return false;

    const double scale = std::min(static_cast<double>(width) / imageWidth,
                                  static_cast<double>(height) / imageHeight);
    cover.width = std::max(1, static_cast<int>(imageWidth * scale));
    cover.height = std::max(1, static_cast<int>(imageHeight * scale));
    cover.pixels.resize(static_cast<size_t>(cover.width) * cover.height);
    for (int y = 0; y < cover.height; ++y) {
        const int sourceY = y * imageHeight / cover.height;
        for (int x = 0; x < cover.width; ++x) {
            const int sourceX = x * imageWidth / cover.width;
            cover.pixels[static_cast<size_t>(y) * cover.width + x] =
                static_cast<uint8_t>((gray[sourceY * imageWidth + sourceX] + 8) / 17);
        }
    }
    stbi_image_free(gray);
    return true;
}

} // namespace

namespace BookCover {

bool draw(const std::string& bookPath, uint8_t* framebuffer,
          int x, int y, int width, int height) {
    if (!framebuffer || width <= 0 || height <= 0) return false;
    const size_t dot = bookPath.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : bookPath.substr(dot);
    for (char& c : extension) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (extension != ".epub") return false;

    struct stat st;
    const std::string vfsPath = AppStorage::toVfsPath(bookPath);
    if (stat(vfsPath.c_str(), &st) != 0) return false;
    const std::string key = cacheKey(bookPath, width, height, st);
    auto found = s_coverCache.find(key);
    if (found == s_coverCache.end()) {
        if (s_coverCache.size() >= 12) s_coverCache.clear();
        DecodedCover cover;
        decodeCover(bookPath, width, height, cover);
        found = s_coverCache.emplace(key, std::move(cover)).first;
    }
    const DecodedCover& cover = found->second;
    if (cover.pixels.empty()) return false;

    const int left = x + (width - cover.width) / 2;
    const int top = y + (height - cover.height) / 2;
    for (int cy = 0; cy < cover.height; ++cy) {
        for (int cx = 0; cx < cover.width; ++cx) {
            DisplayHAL::setPixel(left + cx, top + cy,
                cover.pixels[static_cast<size_t>(cy) * cover.width + cx], framebuffer);
        }
    }
    return true;
}

void clearCache() {
    s_coverCache.clear();
}

} // namespace BookCover
