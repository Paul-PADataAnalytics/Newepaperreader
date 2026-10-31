#pragma once

#include <cstdint>
#include <string>

namespace BookCover {

// Draw a fitted EPUB JPEG/PNG cover into the 4-bit grayscale framebuffer.
// Returns false for unsupported, missing, or invalid cover images.
bool draw(const std::string& bookPath, uint8_t* framebuffer,
          int x, int y, int width, int height);

// Drop decoded thumbnails after the book library has changed.
void clearCache();

} // namespace BookCover
