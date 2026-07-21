#pragma once

#include <string>

namespace AppStorage {

// Initializes storage on embedded targets and performs a basic root listing.
bool initialize();

// Converts a browser path (e.g. /books/foo.epub) into a runtime path.
std::string toRuntimePath(const std::string& browserPath);

// Returns the bookmark file path for a given runtime book path.
std::string toBookmarkPath(const std::string& runtimeBookPath);

// Persists bookmark offset.
bool saveBookmark(const std::string& runtimeBookPath, int offset);

// Loads bookmark offset; returns 0 when missing.
int loadBookmark(const std::string& runtimeBookPath);

} // namespace AppStorage
