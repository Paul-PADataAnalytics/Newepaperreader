#include "EpubParser.h"
#include "AppStorage.h"
#ifndef NATIVE_TESTING
#include <Arduino.h>
#endif
#include <miniz.h>
#include <tinyxml2.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>

using namespace tinyxml2;

namespace {

void* epubAllocate(size_t size) {
#ifndef NATIVE_TESTING
    void* buffer = ps_malloc(size);
    if (buffer) return buffer;
#endif
    return malloc(size);
}

std::string localName(const char* name) {
    if (!name) return "";
    const char* colon = strrchr(name, ':');
    return colon ? colon + 1 : name;
}

XMLElement* child(XMLElement* parent, const char* name) {
    for (XMLElement* el = parent ? parent->FirstChildElement() : nullptr;
         el; el = el->NextSiblingElement()) {
        if (localName(el->Name()) == name) return el;
    }
    return nullptr;
}

bool hasToken(const char* value, const char* token) {
    std::istringstream stream(value ? value : "");
    std::string word;
    while (stream >> word) {
        if (word == token) return true;
    }
    return false;
}

std::string elementText(const XMLElement* element) {
    std::string text;
    for (const XMLNode* node = element ? element->FirstChild() : nullptr;
         node; node = node->NextSibling()) {
        if (node->ToText()) text += node->Value();
        else if (node->ToElement()) text += elementText(node->ToElement());
    }
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string decodePath(const std::string& path) {
    std::string decoded;
    decoded.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            const int hi = hexDigit(path[i + 1]);
            const int lo = hexDigit(path[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(path[i]);
    }
    return decoded;
}

std::string normalizePath(const std::string& base, const std::string& href) {
    std::string relative = href.substr(0, href.find_first_of("?#"));
    relative = decodePath(relative);
    std::string combined;
    if (!relative.empty() && relative[0] == '/') {
        combined = relative.substr(1);
    } else {
        combined = base + relative;
    }

    std::vector<std::string> components;
    std::istringstream parts(combined);
    std::string component;
    while (std::getline(parts, component, '/')) {
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            if (!components.empty()) components.pop_back();
        } else {
            components.push_back(component);
        }
    }

    std::string result;
    for (const auto& part : components) {
        if (!result.empty()) result += '/';
        result += part;
    }
    return result;
}

std::string dirname(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

// Collapses runs of whitespace (TOC labels often span several source lines).
std::string collapseSpaces(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

XMLElement* findTocNav(XMLElement* element) {
    for (XMLElement* el = element; el; el = el->NextSiblingElement()) {
        if (localName(el->Name()) == "nav" &&
            (hasToken(el->Attribute("epub:type"), "toc") || hasToken(el->Attribute("role"), "doc-toc"))) {
            return el;
        }
        if (XMLElement* found = findTocNav(el->FirstChildElement())) return found;
    }
    return nullptr;
}

} // namespace

EpubParser::EpubParser() = default;

EpubParser::~EpubParser() {
    close();
}

bool EpubParser::open(const std::string& filepath) {
    close();
    m_filepath = AppStorage::toVfsPath(filepath);
    mz_zip_archive* archive = new mz_zip_archive{};
    if (!mz_zip_reader_init_file(archive, m_filepath.c_str(), 0)) {
        delete archive;
        close();
        return false;
    }
    m_archive = archive;
    if (!parseContainer() || !parseOpf()) {
        close();
        return false;
    }
    return true;
}

void EpubParser::close() {
    if (m_archive) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(m_archive));
        delete static_cast<mz_zip_archive*>(m_archive);
        m_archive = nullptr;
    }
    m_filepath.clear();
    m_manifest.clear();
    m_spine.clear();
    m_navPath.clear();
    m_ncxPath.clear();
    m_opfPath.clear();
    m_opfDir.clear();
    m_coverPath.clear();
    m_title.clear();
    m_author.clear();
}

std::vector<std::string> EpubParser::getChapterList() const {
    return m_spine;
}

char* EpubParser::getFileContent(const std::string& internalPath, size_t& outSize) {
    outSize = 0;
    if (!m_archive) return nullptr;

    mz_zip_archive& archive = *static_cast<mz_zip_archive*>(m_archive);
    const int located = mz_zip_reader_locate_file(&archive, internalPath.c_str(), nullptr, 0);
    if (located < 0) return nullptr;

    const mz_uint index = static_cast<mz_uint>(located);
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, index, &stat) ||
        stat.m_uncomp_size > 4 * 1024 * 1024 ||
        stat.m_comp_size > 4 * 1024 * 1024) {
        return nullptr;
    }

    const size_t outputSize = static_cast<size_t>(stat.m_uncomp_size);
    void* output = epubAllocate(outputSize ? outputSize : 1);
    if (!output) return nullptr;

    bool extracted = false;
    if (stat.m_method == 0) {
        extracted = mz_zip_reader_extract_to_mem(&archive, index, output, outputSize, 0);
    } else if (stat.m_method == MZ_DEFLATED) {
        size_t compressedSize = 0;
        void* compressed = mz_zip_reader_extract_to_heap(
            &archive, index, &compressedSize, MZ_ZIP_FLAG_COMPRESSED_DATA);
        if (compressed) {
            const size_t result = tinfl_decompress_mem_to_mem(
                output, outputSize, compressed, compressedSize, 0);
            extracted = result == outputSize;
            mz_free(compressed);
        }
    }

    if (!extracted) {
        free(output);
        return nullptr;
    }
    outSize = outputSize;
    return static_cast<char*>(output);
}

bool EpubParser::parseContainer() {
    size_t size = 0;
    char* buffer = getFileContent("META-INF/container.xml", size);
    if (!buffer) return false;

    XMLDocument doc;
    const XMLError error = doc.Parse(buffer, size);
    free(buffer);
    if (error != XML_SUCCESS) return false;

    XMLElement* container = doc.RootElement();
    XMLElement* rootfiles = child(container, "rootfiles");
    for (XMLElement* rootfile = child(rootfiles, "rootfile"); rootfile;
         rootfile = rootfile->NextSiblingElement()) {
        const char* mediaType = rootfile->Attribute("media-type");
        const char* fullPath = rootfile->Attribute("full-path");
        if (mediaType && fullPath &&
            std::string(mediaType) == "application/oebps-package+xml") {
            m_opfPath = normalizePath("", fullPath);
            m_opfDir = dirname(m_opfPath);
            return !m_opfPath.empty();
        }
    }
    return false;
}

bool EpubParser::parseOpf() {
    if (m_opfPath.empty()) return false;

    size_t size = 0;
    char* buffer = getFileContent(m_opfPath, size);
    if (!buffer) return false;

    XMLDocument doc;
    const XMLError error = doc.Parse(buffer, size);
    free(buffer);
    if (error != XML_SUCCESS) return false;

    XMLElement* package = doc.RootElement();
    if (!package) return false;

    std::string coverId;
    XMLElement* metadata = child(package, "metadata");
    for (XMLElement* el = metadata ? metadata->FirstChildElement() : nullptr;
         el; el = el->NextSiblingElement()) {
        const std::string name = localName(el->Name());
        if (name == "title" && m_title.empty()) m_title = elementText(el);
        if (name == "creator" && m_author.empty()) m_author = elementText(el);
        if (name == "meta" && el->Attribute("name", "cover") && el->Attribute("content")) {
            coverId = el->Attribute("content");
        }
    }

    XMLElement* manifest = child(package, "manifest");
    if (!manifest) return false;
    for (XMLElement* item = child(manifest, "item"); item;
         item = item->NextSiblingElement()) {
        const char* id = item->Attribute("id");
        const char* href = item->Attribute("href");
        if (!id || !href) continue;

        const std::string resolved = resolvePath(m_opfDir, href);
        m_manifest[id] = resolved;
        if (hasToken(item->Attribute("properties"), "cover-image")) m_coverPath = resolved;
        if (hasToken(item->Attribute("properties"), "nav")) m_navPath = resolved;
        if (item->Attribute("media-type", "application/x-dtbncx+xml")) m_ncxPath = resolved;
    }
    if (m_coverPath.empty() && m_manifest.count(coverId)) m_coverPath = m_manifest[coverId];

    XMLElement* spine = child(package, "spine");
    if (!spine) return false;
    for (XMLElement* itemref = child(spine, "itemref"); itemref;
         itemref = itemref->NextSiblingElement()) {
        const char* idref = itemref->Attribute("idref");
        if (!idref) continue;
        const auto item = m_manifest.find(idref);
        if (item != m_manifest.end()) m_spine.push_back(item->second);
    }

    if (m_coverPath.empty()) {
        for (const auto& item : m_manifest) {
            std::string lower = item.second;
            for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (lower.find("cover") != std::string::npos &&
                (lower.find(".jpg") != std::string::npos ||
                 lower.find(".jpeg") != std::string::npos ||
                 lower.find(".png") != std::string::npos)) {
                m_coverPath = item.second;
                break;
            }
        }
    }

    return !m_spine.empty();
}

std::string EpubParser::resolvePath(const std::string& basePath, const std::string& relPath) {
    return normalizePath(basePath, relPath);
}

std::vector<EpubParser::TocEntry> EpubParser::readToc() {
    // Keeps a pathological TOC from eating the heap.
    const size_t kMaxEntries = 1500;
    std::vector<TocEntry> toc;
    std::map<std::string, int> spineIndex;
    for (size_t i = 0; i < m_spine.size(); ++i) spineIndex.insert(std::make_pair(m_spine[i], (int)i));

    auto addEntry = [&](const std::string& baseDir, const char* href, const std::string& label, int depth) {
        if (!href || label.empty() || toc.size() >= kMaxEntries) return;
        const auto spine = spineIndex.find(resolvePath(baseDir, href));
        if (spine == spineIndex.end()) return;
        TocEntry entry;
        entry.title = collapseSpaces(label);
        const char* hash = strchr(href, '#');
        if (hash && hash[1]) entry.fragment = hash + 1;
        entry.spineIndex = spine->second;
        entry.depth = depth;
        toc.push_back(entry);
    };

    auto parse = [&](const std::string& path, XMLDocument& doc) {
        if (path.empty()) return false;
        size_t length = 0;
        char* contents = getFileContent(path, length);
        if (!contents) return false;
        const XMLError parsed = doc.Parse(contents, length);
        free(contents);
        return parsed == XML_SUCCESS;
    };

    // EPUB3: <nav epub:type="toc"><ol><li><a href>..</a><ol>..</ol></li></ol>
    {
        XMLDocument nav;
        if (parse(m_navPath, nav)) {
            const std::string baseDir = dirname(m_navPath);
            std::function<void(XMLElement*, int)> walkList = [&](XMLElement* list, int depth) {
                for (XMLElement* li = child(list, "li"); li; li = li->NextSiblingElement()) {
                    if (localName(li->Name()) != "li") continue;
                    XMLElement* link = child(li, "a");
                    if (link) addEntry(baseDir, link->Attribute("href"), elementText(link), depth);
                    walkList(child(li, "ol"), depth + 1);
                }
            };
            XMLElement* tocNav = findTocNav(nav.RootElement());
            walkList(child(tocNav, "ol"), 0);
        }
    }

    // EPUB2: <navMap><navPoint><navLabel><text>..</text></navLabel><content src/>..
    if (toc.empty()) {
        XMLDocument ncx;
        if (parse(m_ncxPath, ncx)) {
            const std::string baseDir = dirname(m_ncxPath);
            std::function<void(XMLElement*, int)> walkPoints = [&](XMLElement* parent, int depth) {
                for (XMLElement* point = child(parent, "navPoint"); point; point = point->NextSiblingElement()) {
                    if (localName(point->Name()) != "navPoint") continue;
                    XMLElement* content = child(point, "content");
                    addEntry(baseDir, content ? content->Attribute("src") : nullptr,
                             elementText(child(child(point, "navLabel"), "text")), depth);
                    walkPoints(point, depth + 1);
                }
            };
            walkPoints(child(ncx.RootElement(), "navMap"), 0);
        }
    }

    toc.shrink_to_fit();
    return toc;
}
