#pragma once

#include "reader/FileBrowser.h"
#include "reader/AppStorage.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Persistent per-book details for the library (/data/library_meta.tsv):
// EPUB title/author, cover state and a snapshot of the reading position, each
// keyed by the book's path + size + modification stamp. Unchanged books are
// never re-opened, and bookmark files are only read when they changed.
namespace LibraryCatalog {

struct Record {
    std::string path;   // SD path, e.g. "/books/foo.epub"
    uint32_t size = 0;
    uint32_t mtime = 0;
    std::string title;  // from the EPUB package; empty if unknown
    std::string author;
    uint8_t cover = 0;  // BookCover::State
    bool hasBookmark = false;
    // Stamp of the .bmk file when `bookmark` was read (0/0 = not known, so
    // the next directory walk re-reads it).
    uint32_t bookmarkSize = 0;
    uint32_t bookmarkMtime = 0;
    AppStorage::BookmarkInfo bookmark;

    std::string coverId() const;
};

struct SyncStats {
    int parsed = 0;        // EPUBs opened for title/author
    int bookmarksRead = 0; // .bmk files read
    int removed = 0;       // books gone since the last sync
};

// Returns one record per book, reusing cached details when the book is
// unchanged. `bookmarkFiles` maps "<book>.bmk" SD paths to the entries seen
// by a directory walk; pass nullptr when no walk was done (cached bookmark
// snapshots are then trusted, except for `refreshBookmarkFor`, whose .bmk
// is always re-read). `onParse(done, total)` reports EPUB parsing progress.
std::vector<Record> sync(const std::vector<FileInfo>& books,
                         const std::unordered_map<std::string, FileInfo>* bookmarkFiles,
                         const std::string& refreshBookmarkFor,
                         const std::function<void(int, int)>& onParse = nullptr,
                         SyncStats* stats = nullptr);

// Persists the records (skipped if nothing changed since the last load/save).
bool save(const std::vector<Record>& records);

} // namespace LibraryCatalog
