#pragma once

#include <string>
#include <vector>
#include <map>

class EpubParser {
public:
    EpubParser();
    ~EpubParser();

    // Open and parse the EPUB file metadata
    bool open(const std::string& filepath);
    
    // Close and release resources
    void close();

    // Get the list of chapters (HTML/XHTML files) in reading order (spine)
    // The paths returned are relative to the root of the EPUB archive.
    std::vector<std::string> getChapterList() const;

    // Extract a specific file's content from the EPUB archive into memory
    char* getFileContent(const std::string& internalPath, size_t& outSize);

private:
    std::string m_filepath;
    std::string m_opfPath;
    std::string m_opfDir; // Directory containing the OPF file, useful for resolving relative paths
    
    std::map<std::string, std::string> m_manifest; // id -> href
    std::vector<std::string> m_spine; // Ordered list of hrefs (resolved to root paths)

    bool parseContainer();
    bool parseOpf();
    
    std::string resolvePath(const std::string& basePath, const std::string& relPath);
};
