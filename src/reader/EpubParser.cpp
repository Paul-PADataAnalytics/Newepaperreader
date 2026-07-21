#include "EpubParser.h"
#include <Arduino.h>
#include <miniz.h>
#include <tinyxml2.h>
#include <iostream>
#include <cstdio>
#include <cstring>

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
    outSize = 0;
    printf("EPUB DEBUG: getFileContent('%s')\n", internalPath.c_str());
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_reader_init_file(&zip_archive, m_filepath.c_str(), 0)) {
        printf("EPUB DEBUG: mz_zip_reader_init_file failed for '%s'\n", m_filepath.c_str());
        std::cerr << "Failed to open zip file: " << m_filepath << std::endl;
        return nullptr;
    }

    void* p = nullptr;
    int located = mz_zip_reader_locate_file(&zip_archive, internalPath.c_str(), nullptr, 0);
    mz_uint targetIndex = (located >= 0) ? static_cast<mz_uint>(located) : 0xFFFFFFFF;
    mz_zip_archive_file_stat stat;
    if (targetIndex == 0xFFFFFFFF) {
        printf("EPUB DEBUG: locate failed for '%s'\n", internalPath.c_str());
    }
    if (targetIndex != 0xFFFFFFFF && mz_zip_reader_file_stat(&zip_archive, targetIndex, &stat)) {
        printf("MINIZ DEBUG: '%s' uncomp=%llu comp=%llu\n",
               internalPath.c_str(),
               stat.m_uncomp_size,
               stat.m_comp_size);
    }

    if (targetIndex != 0xFFFFFFFF) {
        if (stat.m_method == 0) {
            outSize = static_cast<size_t>(stat.m_uncomp_size);
            p = malloc(outSize ? outSize : 1);
            if (p) {
                bool ok = mz_zip_reader_extract_to_mem(&zip_archive, targetIndex, p, outSize, 0);
                if (!ok) {
                    mz_zip_error err = mz_zip_get_last_error(&zip_archive);
                    printf("MINIZ DEBUG: store extract failed out=%u error=%d (%s)\n",
                           static_cast<unsigned>(outSize),
                           (int)err,
                           mz_zip_get_error_string(err));
                    free(p);
                    p = nullptr;
                    outSize = 0;
                }
            }
        } else if (stat.m_method == MZ_DEFLATED) {
            size_t compSize = 0;
            void* compBuf = mz_zip_reader_extract_to_heap(&zip_archive, targetIndex, &compSize, MZ_ZIP_FLAG_COMPRESSED_DATA);
            if (compBuf) {
                outSize = static_cast<size_t>(stat.m_uncomp_size);
                p = malloc(outSize ? outSize : 1);
                if (p) {
                    size_t result = tinfl_decompress_mem_to_mem(p, outSize, compBuf, compSize, 0);
                    if (result == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
                        printf("MINIZ DEBUG: tinfl_decompress_mem_to_mem failed comp=%u out=%u\n",
                               static_cast<unsigned>(compSize),
                               static_cast<unsigned>(outSize));
                        free(p);
                        p = nullptr;
                        outSize = 0;
                    }
                } else {
                    printf("MINIZ DEBUG: output alloc failed out=%u\n", static_cast<unsigned>(outSize));
                    outSize = 0;
                }
                mz_free(compBuf);
            } else {
                mz_zip_error err = mz_zip_get_last_error(&zip_archive);
                printf("MINIZ DEBUG: comp extract failed error=%d (%s)\n",
                       (int)err,
                       mz_zip_get_error_string(err));
            }
        }

        if (!p) {
            outSize = 0;
        }
    }
    
    if (!p) {
        std::cerr << "Failed to extract file: " << internalPath << std::endl;
        outSize = 0;
    }

    mz_zip_reader_end(&zip_archive);
    return static_cast<char*>(p);
}

bool EpubParser::parseContainer() {
    printf("EPUB DEBUG: parseContainer start\n");
    size_t size = 0;
    char* containerBuffer = getFileContent("META-INF/container.xml", size);
    if (!containerBuffer) {
        printf("EPUB DEBUG: parseContainer extraction failed\n");
        std::cerr << "META-INF/container.xml not found or empty." << std::endl;
        return false;
    }
    std::string containerContent(containerBuffer, size);
    mz_free(containerBuffer);

    XMLDocument* doc = new XMLDocument();
    XMLError err = doc->Parse(containerContent.c_str(), containerContent.size());
    if (err != XML_SUCCESS) {
        printf("EPUB DEBUG: parseContainer XML parse failed\n");
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
                printf("EPUB DEBUG: parseContainer ok, OPF='%s'\n", m_opfPath.c_str());
                return true;
            }
        }
        rootfile = rootfile->NextSiblingElement("rootfile");
    }

    std::cerr << "No valid OPF rootfile found in container.xml." << std::endl;
    printf("EPUB DEBUG: parseContainer no rootfile\n");
    delete doc;
    return false;
}

bool EpubParser::parseOpf() {
    printf("EPUB DEBUG: parseOpf start '%s'\n", m_opfPath.c_str());
    if (m_opfPath.empty()) return false;

    size_t size = 0;
    char* opfBuffer = getFileContent(m_opfPath, size);
    if (!opfBuffer) {
        printf("EPUB DEBUG: parseOpf extraction failed\n");
        std::cerr << "OPF file not found: " << m_opfPath << std::endl;
        return false;
    }
    std::string opfContent(opfBuffer, size);
    mz_free(opfBuffer);

    XMLDocument* doc = new XMLDocument();
    XMLError err = doc->Parse(opfContent.c_str(), opfContent.size());
    if (err != XML_SUCCESS) {
        printf("EPUB DEBUG: parseOpf XML parse failed\n");
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
    printf("EPUB DEBUG: parseOpf ok chapters=%u\n", static_cast<unsigned>(m_spine.size()));
    return true;
}

std::string EpubParser::resolvePath(const std::string& basePath, const std::string& relPath) {
    // A simple path resolver. basePath is expected to be a directory (ending with /)
    // For a more robust solution, we'd handle "../" and "./".
    // Here we just append, assuming relPath is relative to basePath without traversing up.
    return basePath + relPath;
}
