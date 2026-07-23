#include "ImageViewerApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "AppComm.h"
#include "Launcher.h"

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#endif

// Define custom allocation hooks for stb_image to run inside ESP32 PSRAM
#ifndef NATIVE_TESTING
#define STBI_MALLOC(sz)        heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)
#define STBI_REALLOC(p,newsz)  heap_caps_realloc(p, newsz, MALLOC_CAP_SPIRAM)
#define STBI_FREE(p)           heap_caps_free(p)
#else
#define STBI_MALLOC(sz)        malloc(sz)
#define STBI_REALLOC(p,newsz)  realloc(p,newsz)
#define STBI_FREE(p)           free(p)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

extern uint8_t *framebuffer;
extern TypographyEngine typography;

static void writePixelToBuffer(uint8_t* buf, int x, int y, uint8_t val) {
    if (x < 0 || x >= 960 || y < 0 || y >= 540) return;
    int index = (y * 960 + x) / 2;
    bool isOdd = (x % 2 != 0);
    uint8_t c = val & 0x0F;
#ifndef NATIVE_TESTING
    if (isOdd) {
        buf[index] = (buf[index] & 0x0F) | (c << 4);
    } else {
        buf[index] = (buf[index] & 0xF0) | c;
    }
#else
    if (isOdd) {
        buf[index] = (buf[index] & 0xF0) | c;
    } else {
        buf[index] = (buf[index] & 0x0F) | (c << 4);
    }
#endif
}

ImageViewerApp::~ImageViewerApp() {
    freeBuffers();
}

void ImageViewerApp::freeBuffers() {
    if (m_fileBuffer) {
        free(m_fileBuffer);
        m_fileBuffer = nullptr;
    }
    if (m_rgbData) {
        stbi_image_free(m_rgbData);
        m_rgbData = nullptr;
    }
    if (m_convertedFrame) {
        free(m_convertedFrame);
        m_convertedFrame = nullptr;
    }
}

void ImageViewerApp::onCreate() {
    m_state = STATE_BROWSER;
    m_page = 0;
    m_selectedIndex = -1;
    m_conversionStep = 0;
    m_progress = 0;
    m_statusMsg = "";
    
#ifndef NATIVE_TESTING
    m_cachePath = "/data/cache_image.raw";
#else
    m_cachePath = "data/cache_image.raw";
#endif

    updateFileList();
}

void ImageViewerApp::onDestroy() {
    freeBuffers();
}

void ImageViewerApp::updateFileList() {
    m_files.clear();
    m_fileBrowser.setRoot("/images");
    std::vector<FileInfo> allFiles = m_fileBrowser.getFiles();
    
    for (const auto& f : allFiles) {
        if (f.isDirectory) continue;
        std::string ext = f.name;
        for (char& c : ext) c = tolower(c);
        if (ext.length() >= 4 && ext.substr(ext.length() - 4) == ".jpg") {
            m_files.push_back(f);
        } else if (ext.length() >= 5 && ext.substr(ext.length() - 5) == ".jpeg") {
            m_files.push_back(f);
        }
    }
}

void ImageViewerApp::draw() {
    if (m_state == STATE_BROWSER) {
        drawBrowser();
    } else if (m_state == STATE_CONVERTING) {
        drawConverting();
    } else if (m_state == STATE_VIEW) {
        drawView();
    }
}

void ImageViewerApp::drawBrowser() {
    UIFramework::performFullScreenDraw(framebuffer, [&]() {
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();
        uint8_t fg = UIFramework::getForegroundColor();

        typography.setFontSize(40.0f);
        typography.renderText("Image Viewer", 70, 2, framebuffer, fg);

        // Draw Settings Cog in top-right
        UIFramework::drawIcon16x16(framebuffer, 920, 18, COG_ICON, fg);
        DisplayHAL::drawHLine(0, LIB_TOP_H - 1, w, fg, framebuffer);

        // Draw main list and side panel
        DisplayHAL::drawRect(LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H, fg, framebuffer);

        // Left scrollbar / paging area
        DisplayHAL::drawRect(0, LIB_MAIN_Y, LIB_PAGING_W, LIB_MAIN_H, fg, framebuffer);

        // Up / Down scroll buttons
        int pBtnW = LIB_PAGING_W - 20;
        int pBtnH = (LIB_MAIN_H / 2) - 20;
        int upY = LIB_MAIN_Y + 10;
        int dnY = LIB_MAIN_Y + (LIB_MAIN_H / 2) + 10;

        UIFramework::drawButton(framebuffer, 10, upY, pBtnW, pBtnH, "");
        UIFramework::drawButton(framebuffer, 10, dnY, pBtnW, pBtnH, "");

        typography.setFontSize(28.0f);
        int symbolHeight = 24;
        std::string upSymbol = "^";
        std::string dnSymbol = "v";
        int twUp = typography.measureText(upSymbol);
        int twDn = typography.measureText(dnSymbol);
        typography.renderText(upSymbol, 10 + (pBtnW - twUp) / 2, upY + (pBtnH - symbolHeight) / 2, framebuffer, fg);
        typography.renderText(dnSymbol, 10 + (pBtnW - twDn) / 2, dnY + (pBtnH - symbolHeight) / 2, framebuffer, fg);

        // Exit Button on the right panel
        int btnH = LIB_MAIN_H / 4;
        UIFramework::drawButton(framebuffer, LIB_SIDE_X + 10, LIB_MAIN_Y + 10, LIB_SIDE_W - 20, btnH - 20, "");
        typography.setFontSize(24.0f);
        int extW1 = typography.measureText("Exit to");
        int extW2 = typography.measureText("Launcher");
        typography.renderText("Exit to", LIB_SIDE_X + (LIB_SIDE_W - extW1) / 2, LIB_MAIN_Y + 20, framebuffer, fg);
        typography.renderText("Launcher", LIB_SIDE_X + (LIB_SIDE_W - extW2) / 2, LIB_MAIN_Y + 50, framebuffer, fg);

        // Render JPG file list
        int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
        int startIdx = m_page * itemsPerPage;

        for (int i = 0; i < itemsPerPage; i++) {
            int idx = startIdx + i;
            int rowY = LIB_MAIN_Y + (i * LIB_ROW_H);
            if (idx < (int)m_files.size()) {
                std::string name = m_files[idx].name;
                int maxW = LIB_LIST_W - 40;
                typography.setFontSize(26.0f);
                if (typography.measureText(name) > maxW) {
                    while (name.length() > 3 && typography.measureText(name + "...") > maxW) {
                        name.pop_back();
                    }
                    name += "...";
                }
                typography.renderText(name, LIB_LIST_X + 20, rowY + 25, framebuffer, fg);
                DisplayHAL::drawHLine(LIB_LIST_X, rowY + LIB_ROW_H - 1, LIB_LIST_W, 0x05, framebuffer);
            }
        }

        if (m_files.empty()) {
            typography.setFontSize(26.0f);
            typography.renderText("No JPG images found in /images", LIB_LIST_X + 20, LIB_MAIN_Y + 50, framebuffer, fg);
        }
    });
}

void ImageViewerApp::drawConverting(bool fullRefresh) {
    auto renderContent = [&]() {
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();

        uint8_t fg = UIFramework::getForegroundColor();
        uint8_t bg = UIFramework::getBackgroundColor();

        if (fullRefresh) {
            typography.setFontSize(40.0f);
            typography.renderText("Converting Image...", 50, 100, framebuffer, fg);

            typography.setFontSize(24.0f);
            typography.renderText("Path: " + m_selectedPath, 50, 180, framebuffer, 0x03);
        } else {
            // Overwrite old text and bar with background color before redrawing them
            DisplayHAL::fillRect(0, 200, w, 200, bg, framebuffer);
        }

        typography.setFontSize(24.0f);
        typography.renderText("Status: " + m_statusMsg, 50, 220, framebuffer, fg);

        // Draw Progress Bar
        int barX = 50;
        int barY = 280;
        int barW = w - 100;
        int barH = 36;
        DisplayHAL::drawRect(barX, barY, barW, barH, fg, framebuffer);

        int progressW = (barW - 4) * m_progress / 100;
        if (progressW > 0) {
            DisplayHAL::fillRect(barX + 2, barY + 2, progressW, barH - 4, fg, framebuffer);
        }

        // Progress Text or Error Text
        if (m_conversionStep == -1) {
            typography.renderText("Touch anywhere to return to browser.", 50, 400, framebuffer, fg);
        } else {
            std::string pct = std::to_string(m_progress) + "%";
            int pctW = typography.measureText(pct);
            typography.renderText(pct, barX + (barW - pctW) / 2, barY + barH + 10, framebuffer, fg);
        }
    };

    if (fullRefresh) {
        UIFramework::performFullScreenDraw(framebuffer, renderContent);
    } else {
        int w = DisplayHAL::getWidth();
        UIFramework::performFastPartialUpdate(framebuffer, 0, 200, w, 250, renderContent);
    }
}

void ImageViewerApp::drawView() {
    UIFramework::performFullScreenDraw(framebuffer, [&]() {
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();

        // 1. Load converted raw 4-bit E-Ink format directly into framebuffer
#ifdef NATIVE_TESTING
        FILE* f = fopen(m_cachePath.c_str(), "rb");
        if (f) {
            fread(framebuffer, 1, 960 * 540 / 2, f);
            fclose(f);
        }
#else
        File f = SD.open(m_cachePath.c_str(), FILE_READ);
        if (f) {
            f.read(framebuffer, 960 * 540 / 2);
            f.close();
        }
#endif

        // 2. Draw a Back navigation overlay button (bottom right)
        uint8_t fg = UIFramework::getForegroundColor();
        int backX = w - 180;
        int backY = h - 60;
        int backW = 160;
        int backH = 48;
        UIFramework::drawButton(framebuffer, backX, backY, backW, backH, "", false);
        typography.setFontSize(22.0f);
        int tw = typography.measureText("Exit View");
        typography.renderText("Exit View", backX + (backW - tw) / 2, backY + 12, framebuffer, fg);
    });
}

void ImageViewerApp::handleTouch(int x, int y) {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    if (m_state == STATE_BROWSER) {
        // Settings Cog (top-right x >= 900)
        if (x >= 900 && y <= LIB_TOP_H) {
            Launcher::getInstance().switchToApp(2);
            return;
        }

        // Exit Launcher Button
        int btnH = LIB_MAIN_H / 4;
        if (x >= LIB_SIDE_X && y >= LIB_MAIN_Y && y <= LIB_MAIN_Y + btnH) {
            onDestroy();
            extern void exitToSystemLauncher();
            exitToSystemLauncher();
            return;
        }

        // Paging Up / Down
        if (x <= LIB_PAGING_W && y >= LIB_MAIN_Y) {
            int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
            if (y < LIB_MAIN_Y + LIB_MAIN_H / 2) {
                if (m_page > 0) m_page--;
            } else {
                if ((m_page + 1) * itemsPerPage < (int)m_files.size()) {
                    m_page++;
                }
            }
            drawBrowser();
            return;
        }

        // File Select
        if (x > LIB_PAGING_W && x < LIB_SIDE_X && y >= LIB_MAIN_Y) {
            int itemsPerPage = LIB_MAIN_H / LIB_ROW_H;
            int clickedRow = (y - LIB_MAIN_Y) / LIB_ROW_H;
            int clickedIndex = m_page * itemsPerPage + clickedRow;
            if (clickedIndex >= 0 && clickedIndex < (int)m_files.size()) {
                m_selectedIndex = clickedIndex;
                startConversion(m_files[clickedIndex].path);
            }
        }
    } else if (m_state == STATE_CONVERTING) {
        if (m_conversionStep == -1) {
            m_state = STATE_BROWSER;
            m_conversionStep = 0;
            drawBrowser();
        }
    } else if (m_state == STATE_VIEW) {
        // Exit View button bounds
        int backX = w - 180;
        int backY = h - 60;
        int backW = 160;
        int backH = 48;
        if (x >= backX && x <= backX + backW && y >= backY && y <= backY + backH) {
            m_state = STATE_BROWSER;
            drawBrowser();
        }
    }
}

void ImageViewerApp::startConversion(const std::string& path) {
    m_selectedPath = path;

    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        m_cachePath = path.substr(0, dotPos) + ".raw";
    } else {
        m_cachePath = path + ".raw";
    }

    bool isCached = false;
#ifdef NATIVE_TESTING
    FILE* f = fopen(m_cachePath.c_str(), "rb");
    if (f) {
        isCached = true;
        fclose(f);
    }
#else
    std::string actualPath = m_cachePath;
    if (actualPath.rfind("/sd", 0) == 0) {
        actualPath = actualPath.substr(3);
    }
    if (SD.exists(actualPath.c_str())) {
        isCached = true;
    }
#endif

    if (isCached) {
        m_state = STATE_VIEW;
        drawView();
        return;
    }

    m_state = STATE_CONVERTING;
    m_conversionStep = 1; // Start loading step
    m_progress = 0;
    m_statusMsg = "Reading image file...";
    drawConverting();
}

static uint8_t* readEntireFile(const std::string& path, size_t& out_size) {
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    out_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buffer = (uint8_t*)malloc(out_size);
    if (buffer) {
        size_t readBytes = fread(buffer, 1, out_size, f);
        (void)readBytes;
    }
    fclose(f);
    return buffer;
#else
    std::string actualPath = path;
    if (actualPath.rfind("/sd", 0) == 0) {
        actualPath = actualPath.substr(3);
    }
    if (!SD.exists(actualPath.c_str())) return nullptr;
    File f = SD.open(actualPath.c_str(), FILE_READ);
    if (!f) return nullptr;
    out_size = f.size();
    uint8_t* buffer = (uint8_t*)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM);
    if (buffer) {
        f.read(buffer, out_size);
    }
    f.close();
    return buffer;
#endif
}

void ImageViewerApp::update() {
    if (m_state != STATE_CONVERTING) return;
    if (m_conversionStep == -1) return;

    if (m_conversionStep == 1) {
        // Step 1: Read raw JPG file
        freeBuffers();
        m_statusMsg = "Reading JPG from disk...";
        m_progress = 5;
        drawConverting(false);

        m_fileBuffer = readEntireFile(m_selectedPath, m_fileBufferSize);
        if (!m_fileBuffer || m_fileBufferSize == 0) {
            m_statusMsg = "Error: Failed to read file.";
            m_conversionStep = -1;
            drawConverting(false);
            return;
        }

        m_conversionStep = 2;
    } else if (m_conversionStep == 2) {
        // Step 2: Decode JPEG
        m_statusMsg = "Decoding JPG compressed stream...";
        m_progress = 20;
        drawConverting(false);

        m_rgbData = stbi_load_from_memory(m_fileBuffer, m_fileBufferSize, &m_imgWidth, &m_imgHeight, &m_imgChannels, 3);
        
        // Free compressed source buffer to save memory
        free(m_fileBuffer);
        m_fileBuffer = nullptr;
        m_fileBufferSize = 0;

        if (!m_rgbData) {
            m_statusMsg = "Error: JPG decoding failed. Image may be too large.";
            m_conversionStep = -1;
            drawConverting(false);
            return;
        }

        // Allocate converted frame buffer
#ifndef NATIVE_TESTING
        m_convertedFrame = (uint8_t*)heap_caps_malloc(960 * 540 / 2, MALLOC_CAP_SPIRAM);
#else
        m_convertedFrame = (uint8_t*)malloc(960 * 540 / 2);
#endif
        if (!m_convertedFrame) {
            m_statusMsg = "Error: Framebuffer allocation failed.";
            stbi_image_free(m_rgbData);
            m_rgbData = nullptr;
            m_conversionStep = -1;
            drawConverting(false);
            return;
        }

        // Clear converted buffer to white
        memset(m_convertedFrame, 0xFF, 960 * 540 / 2);
        
        m_conversionLine = 0;
        m_conversionStep = 3;
    } else if (m_conversionStep == 3) {
        // Step 3: Convert lines incrementally to prevent blocking the CPU/watchdog
        m_statusMsg = "Mapping color values to 4-bit grayscale...";
        
        // Convert 90 lines per update cycle (takes 6 updates to complete 540 lines)
        int linesToConvert = 90;
        int endLine = m_conversionLine + linesToConvert;
        if (endLine > 540) endLine = 540;

        for (int y = m_conversionLine; y < endLine; y++) {
            for (int x = 0; x < 960; x++) {
                int src_y, src_x;
                if (m_imgHeight > m_imgWidth) {
                    src_y = x * m_imgHeight / 960;
                    src_x = (539 - y) * m_imgWidth / 540;
                } else {
                    src_y = y * m_imgHeight / 540;
                    src_x = x * m_imgWidth / 960;
                }
                
                int src_idx = (src_y * m_imgWidth + src_x) * 3;
                uint8_t r = m_rgbData[src_idx];
                uint8_t g = m_rgbData[src_idx + 1];
                uint8_t b = m_rgbData[src_idx + 2];

                // Standard luma formula: Y = 0.299R + 0.587G + 0.114B
                uint8_t luma = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
                
                // Map 8-bit luma to 4-bit (16 shades of gray)
                uint8_t gray16 = luma >> 4;

                writePixelToBuffer(m_convertedFrame, x, y, gray16);
            }
        }

        m_conversionLine = endLine;
        m_progress = 20 + (m_conversionLine * 70 / 540); // Scaling up to 90%
        drawConverting(false);

        if (m_conversionLine >= 540) {
            // Free the decompressed RGB raw image to reclaim memory
            stbi_image_free(m_rgbData);
            m_rgbData = nullptr;
            
            m_conversionStep = 4;
        }
    } else if (m_conversionStep == 4) {
        // Step 4: Save cache raw data to SD card
        m_statusMsg = "Caching grayscale output to SD card...";
        m_progress = 95;
        drawConverting(false);

#ifdef NATIVE_TESTING
        FILE* out = fopen(m_cachePath.c_str(), "wb");
        if (out) {
            fwrite(m_convertedFrame, 1, 960 * 540 / 2, out);
            fclose(out);
        }
#else
        std::string actualPath = m_cachePath;
        if (actualPath.rfind("/sd", 0) == 0) {
            actualPath = actualPath.substr(3);
        }
        if (SD.exists(actualPath.c_str())) {
            SD.remove(actualPath.c_str());
        }
        File out = SD.open(actualPath.c_str(), FILE_WRITE);
        if (out) {
            out.write(m_convertedFrame, 960 * 540 / 2);
            out.close();
        }
#endif

        free(m_convertedFrame);
        m_convertedFrame = nullptr;

        m_progress = 100;
        m_state = STATE_VIEW;
        m_conversionStep = 0;
        draw();
    }
}
