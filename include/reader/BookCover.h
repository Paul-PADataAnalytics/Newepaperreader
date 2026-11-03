#pragma once

#include <cstdint>
#include <string>
#include <vector>

// EPUB cover images, decoded once and cached on the SD card as ready-to-draw
// 16-grey bitmaps (/data/covers/<id>.thm for the library list and
// <id>.big for the sleep screen), so drawing a cover never touches the EPUB.
//
// Book paths are SD-root paths such as "/books/foo.epub".
namespace BookCover {

// Thumbnail box used by the library list, and the sleep-screen cover box.
constexpr int THUMB_W = 80;
constexpr int THUMB_H = 120;
constexpr int LARGE_W = 432;
constexpr int LARGE_H = 648;

enum State : uint8_t {
    STATE_UNKNOWN = 0, // not generated yet
    STATE_READY = 1,   // cached images exist
    STATE_NONE = 2,    // no usable cover (or it could not be decoded)
};

// Stable cache id (8 hex digits) for one version of a book file.
std::string cacheId(const std::string& sdPath, uint32_t size, uint32_t mtime);
// Same, reading the size and stamp from the card. Empty if the file is missing.
std::string cacheIdForFile(const std::string& sdPath);

bool isEpub(const std::string& path);

// Draws the cached thumbnail centred in the box and reports where it went.
// False if missing.
bool drawThumbnail(const std::string& id, uint8_t* framebuffer, int x, int y, int w, int h,
                   int* drawnX = nullptr, int* drawnY = nullptr, int* drawnW = nullptr, int* drawnH = nullptr);
// Draws the cached large cover centred horizontally at the top of the box
// (scaled down if the box is smaller). Reports the drawn size. False if missing.
bool drawLarge(const std::string& id, uint8_t* framebuffer, int x, int y, int w, int h,
               int* drawnW = nullptr, int* drawnH = nullptr);
bool hasLarge(const std::string& id);
// Ensures the large cached cover exists on disk, waiting for an in-flight
// background decode of the same book or generating it synchronously if needed.
bool ensureLarge(const std::string& sdPath, const std::string& id);

// Decodes the book's cover and writes both cached images. Slow (reads and
// decodes the whole image) - prefer request().
State generate(const std::string& sdPath, const std::string& id);

// Background generation. On the device a low-priority task on the other
// core works through requests; in the simulator pump() runs one per call.
// Completed jobs are collected with takeResult() on the UI thread.
void request(const std::string& sdPath, const std::string& id);
void cancelPending();
void pump();
bool takeResult(std::string& id, State& state);
bool busy();

// Drops in-memory thumbnails.
void clearCache();
// Deletes cached covers of books no longer in the library.
void pruneCache(const std::vector<std::string>& keepIds);

} // namespace BookCover
