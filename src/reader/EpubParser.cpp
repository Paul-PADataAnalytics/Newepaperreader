#include "EpubParser.h"
#include <miniz.h>
#include <tinyxml2.h>
#include <iostream>

using namespace tinyxml2;

EpubParser::EpubParser() {
}

EpubParser::~EpubParser() {
    close();
}

bool EpubParser::open(const std::string& filepath) {
    m_filepath = filepath;
    m_manifest.clear();
    m_spine.clear();
    m_opfPath.clear();
    m_opfDir.clear();

    if (!parseContainer()) {
        return false;
    }

    if (!parseOpf()) {
        return false;
    }

    return true;
}

void EpubParser::close() {
    m_filepath.clear();
    m_manifest.clear();
    m_spine.clear();
    m_opfPath.clear();
    m_opfDir.clear();
}

std::vector<std::string> EpubParser::getChapterList() const {
    return m_spine;
}

char* EpubParser::getFileContent(const std::string& internalPath, size_t& outSize) {
    mz_zip_archive* zip_archive = (mz_zip_archive*)malloc(sizeof(mz_zip_archive));
    if (!zip_archive) {
        outSize = 0;
        return nullptr;
    }
    memset(zip_archive, 0, sizeof(mz_zip_archive));

    if (!mz_zip_reader_init_file(zip_archive, m_filepath.c_str(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        std::cerr << "Failed to open zip file: " << m_filepath << std::endl;
        outSize = 0;
        free(zip_archive);
        return nullptr;
    }

    printf("MINIZ DEBUG: Total files = %u\n", zip_archive->m_total_files);
    for (mz_uint i = 0; i < zip_archive->m_total_files; i++) {
        char namebuf[256];
        if (mz_zip_reader_get_filename(zip_archive, i, namebuf, sizeof(namebuf))) {
            printf("MINIZ DEBUG: File %u = '%s'\n", i, namebuf);
        }
    }

    void* p = mz_zip_reader_extract_file_to_heap(zip_archive, internalPath.c_str(), &outSize, 0);
    
    if (!p) {
        std::cerr << "Failed to extract file: " << internalPath << std::endl;
        outSize = 0;
    }

    mz_zip_reader_end(zip_archive);
    free(zip_archive);
    return static_cast<char*>(p);
}

bool EpubParser::parseContainer() {
    size_t size = 0;
    char* containerBuffer = getFileContent("META-INF/container.xml", size);
    if (!containerBuffer) {
        std::cerr << "META-INF/container.xml not found or empty." << std::endl;
        return false;
    }
    std::string containerContent(containerBuffer, size);
    free(containerBuffer); // or mz_free depending on macro, free works since we mapped MZ_FREE

    XMLDocument* doc = new XMLDocument();
    XMLError err = doc->Parse(containerContent.c_str(), containerContent.size());
    if (err != XML_SUCCESS) {
        std::cerr << "Failed to parse container.xml" << std::endl;
        delete doc;
        return false;
    }

    XMLElement* container = doc->FirstChildElement("container");
    if (!container) { delete doc; return false; }

    XMLElement* rootfiles = container->FirstChildElement("rootfiles");
    if (!rootfiles) { delete doc; return false; }

    XMLElement* rootfile = rootfiles->FirstChildElement("rootfile");
    while (rootfile) {
        const char* mediaType = rootfile->Attribute("media-type");
        if (mediaType && std::string(mediaType) == "application/oebps-package+xml") {
            const char* fullPath = rootfile->Attribute("full-path");
            if (fullPath) {
                m_opfPath = fullPath;
                size_t slashPos = m_opfPath.find_last_of('/');
                if (slashPos != std::string::npos) {
                    m_opfDir = m_opfPath.substr(0, slashPos + 1);
                } else {
                    m_opfDir = "";
                }
                delete doc;
                return true;
            }
        }
        rootfile = rootfile->NextSiblingElement("rootfile");
    }

    std::cerr << "No valid OPF rootfile found in container.xml." << std::endl;
    delete doc;
    return false;
}

bool EpubParser::parseOpf() {
    if (m_opfPath.empty()) return false;

    size_t size = 0;
    char* opfBuffer = getFileContent(m_opfPath, size);
    if (!opfBuffer) {
        std::cerr << "OPF file not found: " << m_opfPath << std::endl;
        return false;
    }
    std::string opfContent(opfBuffer, size);
    free(opfBuffer);

    XMLDocument* doc = new XMLDocument();
    XMLError err = doc->Parse(opfContent.c_str(), opfContent.size());
    if (err != XML_SUCCESS) {
        std::cerr << "Failed to parse OPF file." << std::endl;
        delete doc;
        return false;
    }

    XMLElement* package = doc->FirstChildElement("package");
    if (!package) { delete doc; return false; }

    XMLElement* manifest = package->FirstChildElement("manifest");
    if (manifest) {
        XMLElement* item = manifest->FirstChildElement("item");
        while (item) {
            const char* id = item->Attribute("id");
            const char* href = item->Attribute("href");
            if (id && href) {
                // Resolve href relative to OPF directory
                m_manifest[id] = m_opfDir + href;
            }
            item = item->NextSiblingElement("item");
        }
    } else {
        std::cerr << "No manifest found in OPF." << std::endl;
        delete doc;
        return false;
    }

    XMLElement* spine = package->FirstChildElement("spine");
    if (spine) {
        XMLElement* itemref = spine->FirstChildElement("itemref");
        while (itemref) {
            const char* idref = itemref->Attribute("idref");
            if (idref) {
                auto it = m_manifest.find(idref);
                if (it != m_manifest.end()) {
                    m_spine.push_back(it->second);
                }
            }
            itemref = itemref->NextSiblingElement("itemref");
        }
    } else {
        std::cerr << "No spine found in OPF." << std::endl;
        delete doc;
        return false;
    }

    delete doc;
    return true;
}

std::string EpubParser::resolvePath(const std::string& basePath, const std::string& relPath) {
    // A simple path resolver. basePath is expected to be a directory (ending with /)
    // For a more robust solution, we'd handle "../" and "./".
    // Here we just append, assuming relPath is relative to basePath without traversing up.
    return basePath + relPath;
}
