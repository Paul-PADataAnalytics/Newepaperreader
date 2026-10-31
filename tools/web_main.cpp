#ifdef main
#undef main
#endif

#include <emscripten.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "reader/AppStorage.h"

void setup();
void loop();

namespace {

// The simulated SD card. The page mounts it on IndexedDB before main() runs
// (see tools/web_shell.html), so settings, reading positions and uploaded
// books survive reloads. Native builds treat the working directory as the
// SD root, so the firmware sees /sdcard/books as /books.
const char* kSdRoot = "/sdcard";
// Read-only demo content packaged by tools/build_web.sh.
const char* kBundleRoot = "/bundle";

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool copyFile(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

// Copies bundled files that are missing from the SD card. Existing files are
// left alone so the user's progress and deletions are respected.
void seedFrom(const std::string& from, const std::string& to) {
    mkdir(to.c_str(), 0777);
    DIR* dir = opendir(from.c_str());
    if (!dir) return;
    while (dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string src = from + "/" + name;
        std::string dst = to + "/" + name;
        struct stat st;
        if (stat(src.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            seedFrom(src, dst);
        } else if (!exists(dst)) {
            copyFile(src, dst);
        }
    }
    closedir(dir);
}

} // namespace

extern "C" {

// Called by the page after it writes uploaded books into /sdcard/books.
EMSCRIPTEN_KEEPALIVE void web_library_changed() {
    AppStorage::markLibraryDirty();
}

}

int main() {
    const std::string sd = kSdRoot;
    const std::string bundle = kBundleRoot;
    mkdir(kSdRoot, 0777);
    // Firmware assets (the font) are restored on every start; demo books
    // and pictures only on the first, so ones the user deletes stay gone.
    seedFrom(bundle + "/data", sd + "/data");
    const std::string seededMarker = sd + "/data/.web_seeded";
    if (!exists(seededMarker)) {
        seedFrom(bundle, sd);
        if (FILE* f = fopen(seededMarker.c_str(), "w")) fclose(f);
    }
    chdir(kSdRoot);

    setup();
    emscripten_set_main_loop(loop, 0, 1);
    return 0;
}
