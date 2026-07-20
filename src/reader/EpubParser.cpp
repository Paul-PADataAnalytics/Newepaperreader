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

std::string EpubParser::getFileContent(const std::string& internalPath) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_file(&zip_archive, m_filepath.c_str(), 0)) {
        std::cerr << "Failed to open zip file: " << m_filepath << std::endl;
        return "";
    }

    size_t uncomp_size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(&zip_archive, internalPath.c_str(), &uncomp_size, 0);
    
    std::string content;
    if (p) {
        content.assign(static_cast<const char*>(p), uncomp_size);
        mz_free(p);
    } else {
        std::cerr << "Failed to extract file: " << internalPath << std::endl;
    }

    mz_zip_reader_end(&zip_archive);
    return content;
}

bool EpubParser::parseContainer() {
    std::string containerContent = getFileContent("META-INF/container.xml");
    if (containerContent.empty()) {
        std::cerr << "META-INF/container.xml not found or empty." << std::endl;
        return false;
    }

    XMLDocument doc;
    XMLError err = doc.Parse(containerContent.c_str(), containerContent.size());
    if (err != XML_SUCCESS) {
        std::cerr << "Failed to parse container.xml" << std::endl;
        return false;
    }

    XMLElement* container = doc.FirstChildElement("container");
    if (!container) return false;

    XMLElement* rootfiles = container->FirstChildElement("rootfiles");
    if (!rootfiles) return false;

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
                return true;
            }
        }
        rootfile = rootfile->NextSiblingElement("rootfile");
    }

    std::cerr << "No valid OPF rootfile found in container.xml." << std::endl;
    return false;
}

bool EpubParser::parseOpf() {
    if (m_opfPath.empty()) return false;

    std::string opfContent = getFileContent(m_opfPath);
    if (opfContent.empty()) {
        std::cerr << "OPF file not found: " << m_opfPath << std::endl;
        return false;
    }

    XMLDocument doc;
    XMLError err = doc.Parse(opfContent.c_str(), opfContent.size());
    if (err != XML_SUCCESS) {
        std::cerr << "Failed to parse OPF file." << std::endl;
        return false;
    }

    XMLElement* package = doc.FirstChildElement("package");
    if (!package) return false;

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
        return false;
    }

    return true;
}

std::string EpubParser::resolvePath(const std::string& basePath, const std::string& relPath) {
    // A simple path resolver. basePath is expected to be a directory (ending with /)
    // For a more robust solution, we'd handle "../" and "./".
    // Here we just append, assuming relPath is relative to basePath without traversing up.
    return basePath + relPath;
}
