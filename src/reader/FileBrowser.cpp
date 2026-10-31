#include "FileBrowser.h"
#include "AppStorage.h"

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef NATIVE_TESTING
#include "ff.h"
#endif

FileBrowser::FileBrowser() : currentPath("/") {
}

void FileBrowser::setRoot(const char* path) {
    currentPath = path;
    refresh();
}

bool FileBrowser::refresh() {
    loadDirectory(currentPath.c_str());
    return true;
}

std::vector<FileInfo>& FileBrowser::getFiles() {
    return files;
}

void FileBrowser::enterDirectory(const char* dirName) {
    if (currentPath.back() != '/') {
        currentPath += "/";
    }
    currentPath += dirName;
    refresh();
}

void FileBrowser::goUp() {
    if (currentPath == "/") return;
    
    size_t lastSlash = currentPath.find_last_of('/');
    if (lastSlash == 0) {
        currentPath = "/";
    } else if (lastSlash != std::string::npos) {
        currentPath = currentPath.substr(0, lastSlash);
    } else {
        currentPath = "/";
    }
    refresh();
}

std::string FileBrowser::getCurrentPath() const {
    return currentPath;
}

void FileBrowser::loadDirectory(const char* path) {
    files.clear();
    if (!listDirectory(path, [this](const FileInfo& info) { files.push_back(info); })) {
        printf("FileBrowser::loadDirectory - Failed to open directory: %s\n", path);
    }
}

namespace {

std::string joinPath(const std::string& dir, const std::string& name) {
    std::string full = dir;
    if (full.empty() || full.back() != '/') full += "/";
    return full + name;
}

#ifndef NATIVE_TESTING
// The SD library keeps its FatFs drive number private; a pointer-to-member
// taken through a derived class is the standard-conforming way to read it.
struct SdDriveAccess : public fs::SDFS {
    static int number() {
        uint8_t fs::SDFS::*member = &SdDriveAccess::_pdrv;
        const uint8_t pdrv = SD.*member;
        return pdrv == 0xFF ? -1 : pdrv;
    }
};

// "0:/books" style FatFs path for an SD-root path, or "" if not mounted.
std::string fatPath(const std::string& sdPath) {
    const int drive = SdDriveAccess::number();
    if (drive < 0) return "";
    return std::to_string(drive) + ":" + (sdPath.empty() ? "/" : sdPath);
}
#endif

} // namespace

bool FileBrowser::listDirectory(const std::string& sdPath,
                                const std::function<void(const FileInfo&)>& onEntry) {
#ifndef NATIVE_TESTING
    const std::string fat = fatPath(sdPath);
    if (!fat.empty()) {
        FF_DIR dir;
        if (f_opendir(&dir, fat.c_str()) != FR_OK) return false;
        FILINFO fno;
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            if (fno.fname[0] == '.') continue;
            FileInfo info;
            info.name = fno.fname;
            info.path = joinPath(sdPath, info.name);
            info.isDirectory = (fno.fattrib & AM_DIR) != 0;
            info.size = info.isDirectory ? 0 : (size_t)fno.fsize;
            info.mtime = ((uint32_t)fno.fdate << 16) | fno.ftime;
            onEntry(info);
        }
        f_closedir(&dir);
        return true;
    }
    const std::string hostPath = AppStorage::toVfsPath(sdPath.empty() ? "/" : sdPath);
#else
    std::string hostPath = sdPath;
    if (!hostPath.empty() && hostPath[0] == '/') hostPath = hostPath.substr(1);
    if (hostPath.empty()) hostPath = ".";
#endif
    DIR* d = opendir(hostPath.c_str());
    if (!d) return false;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.') continue;
        struct stat st;
#ifdef NATIVE_TESTING
        if (lstat((hostPath + "/" + name).c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
#else
        if (stat((hostPath + "/" + name).c_str(), &st) != 0) continue;
#endif
        FileInfo info;
        info.name = name;
        info.path = joinPath(sdPath, name);
        info.isDirectory = S_ISDIR(st.st_mode);
        info.size = info.isDirectory ? 0 : (size_t)st.st_size;
        info.mtime = (uint32_t)st.st_mtime;
        onEntry(info);
    }
    closedir(d);
    return true;
}

bool FileBrowser::fileStamp(const std::string& sdPath, uint32_t& size, uint32_t& mtime) {
#ifndef NATIVE_TESTING
    const std::string fat = fatPath(sdPath);
    if (!fat.empty()) {
        FILINFO fno;
        if (f_stat(fat.c_str(), &fno) != FR_OK) return false;
        size = (uint32_t)fno.fsize;
        mtime = ((uint32_t)fno.fdate << 16) | fno.ftime;
        return true;
    }
#endif
    struct stat st;
    if (stat(AppStorage::toVfsPath(AppStorage::toRuntimePath(sdPath)).c_str(), &st) != 0) return false;
    size = (uint32_t)st.st_size;
    mtime = (uint32_t)st.st_mtime;
    return true;
}

std::vector<FileInfo> FileBrowser::scanRecursive(const char* rootPath, std::function<void()> onProgress, int maxDepth) {
    std::vector<FileInfo> out;
    m_scanSucceeded = true;
    scanRecursiveHelper(rootPath, out, 0, maxDepth, onProgress);
    return out;
}

void FileBrowser::scanRecursiveHelper(const std::string& path, std::vector<FileInfo>& out, int depth, int maxDepth, const std::function<void()>& onProgress) {
    if (depth > maxDepth) {
        m_scanSucceeded = false;
        return;
    }
    if (onProgress) onProgress();

    std::vector<std::string> subdirectories;
    const bool listed = listDirectory(path, [&](const FileInfo& info) {
        if (onProgress) onProgress();
        out.push_back(info);
        if (info.isDirectory) subdirectories.push_back(info.path);
    });
    if (!listed) {
        m_scanSucceeded = false;
        return;
    }
    // Descend after the listing is closed: keeps a single directory handle
    // open at a time however deep the tree is.
    for (const std::string& subdirectory : subdirectories) {
        scanRecursiveHelper(subdirectory, out, depth + 1, maxDepth, onProgress);
    }
}
