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

    // Open and parse the EPUB file metadata
    bool open(const std::string& filepath);
    
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
    const std::string& getCoverPath() const { return m_coverPath; }
    const std::string& getTitle() const { return m_title; }
    const std::string& getAuthor() const { return m_author; }

    // Extract a specific file's content from the EPUB archive into memory
    char* getFileContent(const std::string& internalPath, size_t& outSize);

private:
    std::string m_filepath;
    std::string m_opfPath;
    std::string m_opfDir; // Directory containing the OPF file, useful for resolving relative paths
    
    std::map<std::string, std::string> m_manifest; // id -> href
    std::vector<std::string> m_spine; // Ordered list of hrefs (resolved to root paths)
    std::string m_navPath;
    std::string m_ncxPath;
    std::string m_coverPath;
    std::string m_title;
    std::string m_author;
    void* m_archive = nullptr;

    bool parseContainer();
    bool parseOpf();
    
    std::string resolvePath(const std::string& basePath, const std::string& relPath);
};
