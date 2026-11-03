#include "reader/LibraryCatalog.h"
#include "reader/BookCover.h"
#include "EpubParser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace {

const char* kCatalogPath = "/data/library_meta.tsv";
const char* kHeader = "LIBMETA1";

// The last loaded/saved contents, so returning to the library doesn't
// re-read the file.
std::unordered_map<std::string, LibraryCatalog::Record> s_records;
bool s_loaded = false;
bool s_dirty = false;

std::string clean(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return fields;
}

uint32_t toU32(const std::string& text) { return (uint32_t)strtoul(text.c_str(), nullptr, 10); }
int toInt(const std::string& text) { return atoi(text.c_str()); }

std::string catalogFile() {
    return AppStorage::toVfsPath(AppStorage::toRuntimePath(kCatalogPath));
}

void load() {
    if (s_loaded) return;
    s_loaded = true;
    s_records.clear();
    FILE* file = fopen(catalogFile().c_str(), "rb");
    if (!file) return;
    std::string contents;
    char buffer[4096];
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) contents.append(buffer, got);
    fclose(file);

    size_t start = 0;
    bool headerSeen = false;
    while (start < contents.size()) {
        size_t end = contents.find('\n', start);
        if (end == std::string::npos) end = contents.size();
        std::string line = contents.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!headerSeen) {
            if (line != kHeader) return; // unknown format: rebuild from scratch
            headerSeen = true;
            continue;
        }
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() < 12 || f[0].empty()) continue;
        LibraryCatalog::Record r;
        r.path = f[0];
        r.size = toU32(f[1]);
        r.mtime = toU32(f[2]);
        r.title = f[3];
        r.author = f[4];
        r.cover = (uint8_t)toInt(f[5]);
        r.hasBookmark = f[6] == "1";
        r.bookmarkSize = toU32(f[7]);
        r.bookmarkMtime = toU32(f[8]);
        r.bookmark.offset = toInt(f[9]);
        r.bookmark.chapterIndex = toInt(f[10]);
        r.bookmark.totalChapters = toInt(f[11]);
        s_records[r.path] = r;
    }
}

bool sameBook(const LibraryCatalog::Record& r, const FileInfo& f) {
    return r.size == (uint32_t)f.size && r.mtime == f.mtime;
}

bool sameBookmark(const LibraryCatalog::Record& r, const FileInfo& f) {
    return r.hasBookmark && r.bookmarkSize == (uint32_t)f.size && r.bookmarkMtime == f.mtime &&
           (r.bookmarkSize != 0 || r.bookmarkMtime != 0);
}

bool equalRecords(const LibraryCatalog::Record& a, const LibraryCatalog::Record& b) {
    return a.path == b.path && a.size == b.size && a.mtime == b.mtime && a.title == b.title &&
           a.author == b.author && a.cover == b.cover && a.hasBookmark == b.hasBookmark &&
           a.bookmarkSize == b.bookmarkSize && a.bookmarkMtime == b.bookmarkMtime &&
           a.bookmark.offset == b.bookmark.offset && a.bookmark.chapterIndex == b.bookmark.chapterIndex &&
           a.bookmark.totalChapters == b.bookmark.totalChapters;
}

void readBookmark(LibraryCatalog::Record& r, LibraryCatalog::SyncStats* stats) {
    r.bookmark = AppStorage::loadBookmarkInfo(AppStorage::toRuntimePath(r.path));
    if (stats) stats->bookmarksRead++;
}

} // namespace

namespace LibraryCatalog {

std::string Record::coverId() const {
    return BookCover::cacheId(path, size, mtime);
}

std::vector<Record> sync(const std::vector<FileInfo>& books,
                         const std::unordered_map<std::string, FileInfo>* bookmarkFiles,
                         const std::string& refreshBookmarkFor,
                         const std::function<void(int, int)>& onParse,
                         SyncStats* stats) {
    load();

    int toParse = 0;
    for (const FileInfo& f : books) {
        const auto cached = s_records.find(f.path);
        if (BookCover::isEpub(f.path) && (cached == s_records.end() || !sameBook(cached->second, f))) toParse++;
    }

    std::vector<Record> out;
    out.reserve(books.size());
    int parsed = 0;
    for (const FileInfo& f : books) {
        Record r;
        const auto cached = s_records.find(f.path);
        const bool known = cached != s_records.end() && sameBook(cached->second, f);
        if (known) {
            r = cached->second;
        } else {
            r.path = f.path;
            r.size = (uint32_t)f.size;
            r.mtime = f.mtime;
            r.cover = BookCover::STATE_NONE;
            if (BookCover::isEpub(f.path)) {
                EpubParser epub;
                if (epub.open(AppStorage::toRuntimePath(f.path), false)) {
                    r.title = epub.getTitle();
                    r.author = epub.getAuthor();
                    if (epub.mayHaveCover()) r.cover = BookCover::STATE_UNKNOWN;
                }
                if (stats) stats->parsed++;
                if (onParse) onParse(++parsed, toParse);
            }
            // A replaced book keeps its bookmark file; carry the snapshot over.
            if (cached != s_records.end()) {
                r.hasBookmark = cached->second.hasBookmark;
                r.bookmark = cached->second.bookmark;
            }
        }

        const bool refresh = !refreshBookmarkFor.empty() && refreshBookmarkFor == f.path;
        if (bookmarkFiles) {
            const auto bmk = bookmarkFiles->find(f.path + ".bmk");
            if (bmk == bookmarkFiles->end()) {
                r.hasBookmark = false;
                r.bookmark = AppStorage::BookmarkInfo();
                r.bookmarkSize = r.bookmarkMtime = 0;
            } else if (refresh || !sameBookmark(r, bmk->second)) {
                readBookmark(r, stats);
                r.hasBookmark = true;
                r.bookmarkSize = (uint32_t)bmk->second.size;
                r.bookmarkMtime = bmk->second.mtime;
            }
        } else if (refresh || (!known && cached == s_records.end())) {
            // No walk this time: only the book just read (or a book we know
            // nothing about) can have a newer bookmark than our snapshot.
            readBookmark(r, stats);
            r.hasBookmark = r.bookmark.offset != 0 || r.bookmark.chapterIndex != 0 || r.bookmark.totalChapters != 0;
            r.bookmarkSize = r.bookmarkMtime = 0;
        }
        out.push_back(r);
    }

    if (stats) {
        for (const auto& cached : s_records) {
            bool present = false;
            for (const FileInfo& f : books) {
                if (f.path == cached.first && sameBook(cached.second, f)) {
                    present = true;
                    break;
                }
            }
            if (!present) stats->removed++;
        }
    }
    if (out.size() != s_records.size()) s_dirty = true;
    for (const Record& r : out) {
        const auto cached = s_records.find(r.path);
        if (cached == s_records.end() || !equalRecords(cached->second, r)) {
            s_dirty = true;
            break;
        }
    }
    return out;
}

bool save(const std::vector<Record>& records) {
    // An empty list is more likely a missing card than an emptied library;
    // keep what we know (stale entries are harmless).
    if (records.empty()) return true;
    bool changed = s_dirty || records.size() != s_records.size();
    for (size_t i = 0; !changed && i < records.size(); ++i) {
        const auto cached = s_records.find(records[i].path);
        changed = cached == s_records.end() || !equalRecords(cached->second, records[i]);
    }
    if (!changed) return true;

    mkdir(AppStorage::toVfsPath(AppStorage::toRuntimePath("/data")).c_str(), 0775);
    const std::string path = catalogFile();
    const std::string temporary = path + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) return false;
    std::string contents = std::string(kHeader) + "\n";
    char numbers[96];
    for (const Record& r : records) {
        contents += clean(r.path);
        snprintf(numbers, sizeof(numbers), "\t%u\t%u\t", (unsigned)r.size, (unsigned)r.mtime);
        contents += numbers;
        contents += clean(r.title) + "\t" + clean(r.author);
        snprintf(numbers, sizeof(numbers), "\t%d\t%d\t%u\t%u\t%d\t%d\t%d\n", (int)r.cover,
                 r.hasBookmark ? 1 : 0, (unsigned)r.bookmarkSize, (unsigned)r.bookmarkMtime,
                 r.bookmark.offset, r.bookmark.chapterIndex, r.bookmark.totalChapters);
        contents += numbers;
    }
    bool ok = fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    ok = (fclose(file) == 0) && ok;
    if (ok) {
        remove(path.c_str());
        ok = rename(temporary.c_str(), path.c_str()) == 0;
    }
    if (!ok) {
        remove(temporary.c_str());
        return false;
    }
    s_records.clear();
    for (const Record& r : records) s_records[r.path] = r;
    s_dirty = false;
    return true;
}

} // namespace LibraryCatalog
