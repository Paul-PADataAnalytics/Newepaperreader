#pragma once

#include <string>
#include <vector>
#include <map>

class EpubParser {
public:
    EpubParser();
    ~EpubParser();
    EpubParser(const EpubParser&) = delete;
    EpubParser& operator=(const EpubParser&) = delete;

    // Open and parse the EPUB file metadata. readNavigation = false skips the
    // table of contents (chapter titles fall back to file names), which is
    // all a library scan needs and saves inflating the nav/NCX document.
    bool open(const std::string& filepath, bool readNavigation = true);
    
    // Close and release resources
    void close();

    // Get the list of chapters (HTML/XHTML files) in reading order (spine)
    // The paths returned are relative to the root of the EPUB archive.
    std::vector<std::string> getChapterList() const;

    struct TocEntry {
        std::string title;
        std::string fragment; // anchor within the spine item, "" = its start
        int spineIndex = 0;
        int depth = 0;        // 0 = top level
    };
    // Reads the table of contents - the EPUB3 nav document, else the EPUB2
    // NCX - with every entry mapped onto its spine index. Parsed on demand
    // (not by open(), so cover/metadata-only users don't pay for it) and not
    // cached here: callers keep the result. Empty if there is no usable TOC.
    std::vector<TocEntry> readToc();

    // Archive path of the cover image as declared by the package (EPUB3
    // cover-image property, EPUB2 <meta name="cover">, or a manifest image
    // named "cover"). May be empty when only a cover page is known - use
    // findCoverImage() to also look inside that page.
    const std::string& getCoverPath() const { return m_coverPath; }
    // getCoverPath(), or else the first <img>/<svg:image> of the cover XHTML
    // page (EPUB2 guide/meta, or a manifest page named "cover"). Only JPEG
    // and PNG images are returned. Empty if the book has no usable cover.
    std::string findCoverImage();
    // False when the package names neither a cover image nor a cover page.
    bool mayHaveCover() const { return !m_coverPath.empty() || !m_coverPagePath.empty(); }
    const std::string& getTitle() const { return m_title; }
    const std::string& getAuthor() const { return m_author; }

    // Extract a specific file's content from the EPUB archive into memory
    char* getFileContent(const std::string& internalPath, size_t& outSize);

    // Incremental reads of one archive entry, so large images can be decoded
    // without holding the whole (compressed or inflated) file in memory.
    // openStream() returns nullptr if the entry is missing; readStream()
    // returns 0 at the end or on error. Always closeStream() a stream before
    // closing the parser.
    void* openStream(const std::string& internalPath, size_t& uncompressedSize);
    size_t readStream(void* stream, void* buffer, size_t size);
    void closeStream(void* stream);

private:
    std::string m_filepath;
    std::string m_opfPath;
    std::string m_opfDir; // Directory containing the OPF file, useful for resolving relative paths
    
    std::map<std::string, std::string> m_manifest; // id -> href
    std::vector<std::string> m_spine; // Ordered list of hrefs (resolved to root paths)
    std::string m_navPath;
    std::string m_ncxPath;
    std::string m_coverPath;
    std::string m_coverPagePath; // XHTML page showing the cover, if known
    std::string m_title;
    std::string m_author;
    void* m_archive = nullptr;

    bool parseContainer();
    bool parseOpf(bool includeNavigation);
    
    std::string resolvePath(const std::string& basePath, const std::string& relPath);
};
