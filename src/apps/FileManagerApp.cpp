#include "FileManagerApp.h"
#include "reader/AppStorage.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

extern uint8_t* framebuffer;

namespace {
using namespace UI;

constexpr int CAPTION_H = 60;
constexpr int LIST_TOP = HEADER_H + CAPTION_H;
const Rect kCancelPick(W - MARGIN - 116, HEADER_H + 9, 116, 42);

// Action sheet (bottom card over the list).
constexpr int SHEET_H = 468;
constexpr int SHEET_TOP = H - SHEET_H;
constexpr int SHEET_BTN_H = 68;
constexpr int SHEET_BTN_GAP = 14;
constexpr int SHEET_BTN_Y = SHEET_TOP + 124;
const char* const kSheetLabels[] = {"Copy to...", "Move to...", "Delete", "Cancel"};
constexpr int SHEET_BTN_COUNT = 4;
Rect sheetButton(int i) {
    return Rect(MARGIN, SHEET_BTN_Y + i * (SHEET_BTN_H + SHEET_BTN_GAP), W - 2 * MARGIN, SHEET_BTN_H);
}

// Delete confirmation dialog.
constexpr int DLG_W = W - 2 * 40;
constexpr int DLG_H = 300;
const Rect kDialog((W - DLG_W) / 2, (H - DLG_H) / 2, DLG_W, DLG_H);
constexpr int DLG_BTN_W = (DLG_W - 3 * 24) / 2;
const Rect kDlgCancel(kDialog.x + 24, kDialog.y + DLG_H - 88, DLG_BTN_W, 64);
const Rect kDlgDelete(kDialog.x + 48 + DLG_BTN_W, kDialog.y + DLG_H - 88, DLG_BTN_W, 64);

std::string lowerExtension(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext;
}

UIIconId fileIcon(const std::string& name) {
    std::string ext = lowerExtension(name);
    if (ext == "epub" || ext == "txt" || ext == "md" || ext == "rtf") return ICON_BOOK;
    if (ext == "jpg" || ext == "jpeg") return ICON_IMAGE;
    if (ext == "bmk") return ICON_BOOKMARK;
    return ICON_COUNT;
}

std::string fileKind(const std::string& name) {
    std::string ext = lowerExtension(name);
    if (ext == "epub") return "EPUB book";
    if (ext == "txt") return "Text document";
    if (ext == "md") return "Markdown document";
    if (ext == "rtf") return "Rich text document";
    if (ext == "jpg" || ext == "jpeg") return "JPEG picture";
    if (ext == "bmk") return "Bookmarks";
    if (ext.empty()) return "File";
    for (char& c : ext) c = (char)toupper((unsigned char)c);
    return ext + " file";
}

// Generic "document" glyph in the same 52px slot listRow uses for icons.
void documentIcon(int x, int y) {
    const int l = x + 11, r = x + 41, t = y + 5, b = y + 47, f = 11;
    line(l, t, r - f, t, 3, BLACK);
    line(r - f, t, r, t + f, 3, BLACK);
    line(r, t + f, r, b, 3, BLACK);
    line(r, b, l, b, 3, BLACK);
    line(l, b, l, t, 3, BLACK);
    line(r - f, t, r - f, t + f, 2, BLACK);
    line(r - f, t + f, r, t + f, 2, BLACK);
}

// Folders first, then case-insensitive natural order ("part 2" < "part 10").
bool entryLess(const FileInfo& a, const FileInfo& b) {
    if (a.isDirectory != b.isDirectory) return a.isDirectory;
    const std::string& x = a.name;
    const std::string& y = b.name;
    size_t i = 0, j = 0;
    while (i < x.size() && j < y.size()) {
        if (isdigit((unsigned char)x[i]) && isdigit((unsigned char)y[j])) {
            size_t si = i, sj = j;
            while (si < x.size() && x[si] == '0') si++;
            while (sj < y.size() && y[sj] == '0') sj++;
            size_t ei = si, ej = sj;
            while (ei < x.size() && isdigit((unsigned char)x[ei])) ei++;
            while (ej < y.size() && isdigit((unsigned char)y[ej])) ej++;
            if (ei - si != ej - sj) return ei - si < ej - sj;
            int c = x.compare(si, ei - si, y, sj, ej - sj);
            if (c != 0) return c < 0;
            i = ei;
            j = ej;
            continue;
        }
        int ca = tolower((unsigned char)x[i]);
        int cb = tolower((unsigned char)y[j]);
        if (ca != cb) return ca < cb;
        i++;
        j++;
    }
    return x.size() - i < y.size() - j;
}

std::string folderName(const std::string& path) {
    if (path.empty() || path == "/") return "SD Card";
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

void FileManagerApp::onCreate() {
    m_state = State::BROWSE;
    m_pendingOperation = PendingOperation::NONE;
    m_page = 0;
    m_sourceDirectory = "/";
    m_status.clear();
    m_browser.setRoot("/");
    reloadEntries();
}

void FileManagerApp::onDestroy() {
}

// ---------------------------------------------------------------------------
// Model helpers
// ---------------------------------------------------------------------------

void FileManagerApp::reloadEntries() {
    m_entries.clear();
    for (const FileInfo& f : m_browser.getFiles()) {
        if (m_state == State::DESTINATION && !f.isDirectory) continue;
        m_entries.push_back(f);
    }
    std::sort(m_entries.begin(), m_entries.end(), entryLess);
    int pages = pageCount();
    if (m_page >= pages) m_page = pages - 1;
    if (m_page < 0) m_page = 0;
}

int FileManagerApp::rowsPerPage() const {
    int all = UI::rowsPerPage(false, CAPTION_H);
    if ((int)m_entries.size() <= all) return all;
    return UI::rowsPerPage(true, CAPTION_H);
}

int FileManagerApp::pageCount() const {
    int per = rowsPerPage();
    int pages = ((int)m_entries.size() + per - 1) / per;
    return pages < 1 ? 1 : pages;
}

std::string FileManagerApp::headerTitle() const {
    return folderName(m_browser.getCurrentPath());
}

std::string FileManagerApp::headerAction() const {
    return m_state == State::DESTINATION ? "Paste here" : "";
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void FileManagerApp::draw() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() { drawScreen(); });
}

void FileManagerApp::drawScreen() {
    header(headerTitle(), true, headerAction());
    drawCaption();
    drawList();
    if (m_state == State::ACTIONS) drawActionSheet();
    else if (m_state == State::CONFIRM_DELETE) drawDeleteDialog();
}

void FileManagerApp::drawCaption() {
    int ty = HEADER_H + (CAPTION_H - lineHeight(FONT_SMALL)) / 2;
    int right = W - MARGIN;
    if (m_state == State::DESTINATION) {
        button(kCancelPick, "Cancel", ButtonStyle::SUBTLE, FONT_SMALL);
        right = kCancelPick.x - 16;
    } else {
        std::string count = std::to_string(m_entries.size()) + (m_entries.size() == 1 ? " item" : " items");
        textRight(count, right, ty, FONT_SMALL, DARK);
        right -= textWidth(count, FONT_SMALL) + 20;
    }
    int maxW = right - MARGIN;

    if (!m_status.empty()) {
        text(ellipsize(m_status, maxW, FONT_SMALL, true), MARGIN, ty, FONT_SMALL, BLACK, true);
    } else if (m_state == State::DESTINATION) {
        std::string verb = m_pendingOperation == PendingOperation::MOVE ? "Move " : "Copy ";
        text(ellipsize(verb + m_selectedFile.name, maxW, FONT_SMALL, true), MARGIN, ty, FONT_SMALL, INK, true);
    } else {
        std::string path = m_browser.getCurrentPath();
        // Keep the deepest part of a long path visible.
        if (textWidth(path, FONT_SMALL) > maxW) {
            while (path.size() > 1 && textWidth("..." + path, FONT_SMALL) > maxW) {
                path.erase(0, 1);
                while (!path.empty() && ((unsigned char)path[0] & 0xC0) == 0x80) path.erase(0, 1);
            }
            path = "..." + path;
        }
        text(path, MARGIN, ty, FONT_SMALL, DARK);
    }
    hRule(LIST_TOP - 1, LIGHT);
}

void FileManagerApp::drawList() {
    if (m_entries.empty()) {
        int cy = (LIST_TOP + H) / 2 - 40;
        if (m_state == State::DESTINATION) {
            emptyState(cy, "No folders here",
                       "Tap \"Paste here\" to put " + m_selectedFile.name + " in this folder.");
        } else {
            emptyState(cy, "This folder is empty",
                       "Files copied to this folder on the SD card will show up here.");
        }
        return;
    }

    int per = rowsPerPage();
    int pages = pageCount();
    int start = m_page * per;
    for (int i = 0; i < per && start + i < (int)m_entries.size(); i++) {
        const FileInfo& f = m_entries[start + i];
        int y = LIST_TOP + i * ROW_H;
        if (f.isDirectory) {
            listRow(y, f.name, "", "", true, -1.0f, ICON_FOLDER);
            continue;
        }
        UIIconId id = fileIcon(f.name);
        // listRow only knows the kit's icons: reserve the slot with one and
        // replace it with a plain document glyph for other file types.
        listRow(y, f.name, fileKind(f.name), formatSize(f.size), false, -1.0f,
                id == ICON_COUNT ? ICON_BOOK : id);
        if (id == ICON_COUNT) {
            fillRect(MARGIN, y + (ROW_H - 52) / 2, 52, 52, WHITE);
            documentIcon(MARGIN, y + (ROW_H - 52) / 2);
        }
    }
    if (pages > 1) pager(m_page, pages);
}

void FileManagerApp::drawActionSheet() {
    fillRoundRect(0, SHEET_TOP, W, SHEET_H + 40, 28, WHITE);
    strokeRoundRect(0, SHEET_TOP, W, SHEET_H + 40, 28, 2, BLACK);
    fillRoundRect(W / 2 - 30, SHEET_TOP + 12, 60, 6, 3, LIGHT);

    UIIconId id = fileIcon(m_selectedFile.name);
    int x = MARGIN;
    if (id != ICON_COUNT) icon(id, MARGIN, SHEET_TOP + 38, 56);
    else documentIcon(MARGIN + 2, SHEET_TOP + 40);
    x += 72;
    int maxW = W - MARGIN - x;
    text(ellipsize(m_selectedFile.name, maxW, FONT_BODY, true), x, SHEET_TOP + 38, FONT_BODY, BLACK, true);
    text(ellipsize(fileKind(m_selectedFile.name) + "  \xC2\xB7  " + formatSize(m_selectedFile.size), maxW, FONT_SMALL),
         x, SHEET_TOP + 76, FONT_SMALL, DARK);

    for (int i = 0; i < SHEET_BTN_COUNT; i++) {
        ButtonStyle style = i == SHEET_BTN_COUNT - 1 ? ButtonStyle::SUBTLE : ButtonStyle::SECONDARY;
        button(sheetButton(i), kSheetLabels[i], style, FONT_BODY - 2);
    }
}

void FileManagerApp::drawDeleteDialog() {
    const Rect& d = kDialog;
    fillRoundRect(d.x + 6, d.y + 8, d.w, d.h, 22, MID);
    fillRoundRect(d.x, d.y, d.w, d.h, 22, WHITE);
    strokeRoundRect(d.x, d.y, d.w, d.h, 22, 2, BLACK);

    int x = d.x + 28, w = d.w - 56;
    text("Delete file?", x, d.y + 28, FONT_TITLE - 2, BLACK, true);
    int y = textWrapped(m_selectedFile.name, x, d.y + 86, w, FONT_BODY, INK, 2, 6);
    textWrapped("It will be permanently removed from the SD card.", x, y + 8, w, FONT_SMALL, DARK, 2, 6);

    button(kDlgCancel, "Cancel", ButtonStyle::SECONDARY, FONT_BODY - 2);
    button(kDlgDelete, "Delete", ButtonStyle::PRIMARY, FONT_BODY - 2);
}

// Overlays are drawn with a fast partial update of the whole body: the list
// underneath is redrawn identically, so only the overlay area actually
// changes on the panel (no full-screen flash).
void FileManagerApp::showOverlay() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
        drawCaption();
        drawList();
        if (m_state == State::ACTIONS) drawActionSheet();
        else if (m_state == State::CONFIRM_DELETE) drawDeleteDialog();
    });
}

void FileManagerApp::closeOverlay() {
    m_state = State::BROWSE;
    showOverlay();
}

// ---------------------------------------------------------------------------
// Touch & navigation
// ---------------------------------------------------------------------------

bool FileManagerApp::onBack() {
    switch (m_state) {
    case State::ACTIONS:
    case State::CONFIRM_DELETE:
        closeOverlay();
        return true;
    case State::DESTINATION:
        if (m_browser.getCurrentPath() != "/") goUp();
        else cancelDestinationSelection();
        return true;
    case State::BROWSE:
    default:
        if (m_browser.getCurrentPath() == "/") return false;
        goUp();
        return true;
    }
}

void FileManagerApp::handleTouch(int x, int y) {
    switch (m_state) {
    case State::ACTIONS:
        handleActionsTouch(x, y);
        break;
    case State::CONFIRM_DELETE:
        handleDeleteTouch(x, y);
        break;
    default:
        handleBrowseTouch(x, y);
        break;
    }
}

void FileManagerApp::handleActionsTouch(int x, int y) {
    if (y < SHEET_TOP) {
        if (y >= HEADER_H) closeOverlay();
        return;
    }
    for (int i = 0; i < SHEET_BTN_COUNT; i++) {
        if (!sheetButton(i).contains(x, y)) continue;
        if (i == 0) {
            beginDestinationSelection(PendingOperation::COPY);
        } else if (i == 1) {
            beginDestinationSelection(PendingOperation::MOVE);
        } else if (i == 2) {
            m_state = State::CONFIRM_DELETE;
            // Remove the sheet and show the dialog in one partial update.
            showOverlay();
        } else {
            closeOverlay();
        }
        return;
    }
}

void FileManagerApp::handleDeleteTouch(int x, int y) {
    if (kDlgDelete.contains(x, y)) {
        deleteSelectedFile();
    } else if (kDlgCancel.contains(x, y) || (!kDialog.contains(x, y) && y >= HEADER_H)) {
        closeOverlay();
    }
}

void FileManagerApp::handleBrowseTouch(int x, int y) {
    bool selectingDestination = m_state == State::DESTINATION;

    if (selectingDestination && hitHeaderAction(x, y, headerAction())) {
        finishOperation();
        return;
    }
    if (y < HEADER_H) return;

    if (y < LIST_TOP) {
        if (selectingDestination && kCancelPick.contains(x, y)) cancelDestinationSelection();
        return;
    }

    int pages = pageCount();
    if (pages > 1) {
        int dir = hitPager(x, y, m_page, pages);
        if (dir != 0) {
            m_page += dir;
            m_status.clear();
            draw();
            return;
        }
        if (y >= H - FOOTER_H) return;
    }

    int per = rowsPerPage();
    int row = (y - LIST_TOP) / ROW_H;
    if (row < 0 || row >= per) return;
    int index = m_page * per + row;
    if (index >= (int)m_entries.size()) return;

    // Copy: navigation replaces m_entries.
    FileInfo entry = m_entries[index];
    if (entry.isDirectory) {
        enterDirectory(entry.name);
    } else if (!selectingDestination) {
        selectFile(entry);
    }
}

void FileManagerApp::selectFile(const FileInfo& file) {
    m_selectedFile = file;
    m_sourceDirectory = m_browser.getCurrentPath();
    m_state = State::ACTIONS;
    showOverlay();
}

void FileManagerApp::beginDestinationSelection(PendingOperation operation) {
    m_pendingOperation = operation;
    m_state = State::DESTINATION;
    m_browser.setRoot("/");
    m_page = 0;
    m_status.clear();
    reloadEntries();
    draw();
}

void FileManagerApp::finishOperation() {
    std::string source = AppStorage::toRuntimePath(m_selectedFile.path);
    std::string destination = AppStorage::toRuntimePath(destinationPath());
    std::string error;
    bool moving = m_pendingOperation == PendingOperation::MOVE;
    bool success = moving
        ? AppStorage::moveFile(source, destination, error)
        : AppStorage::copyFile(source, destination, error);

    if (!success) {
        m_status = error.empty() ? std::string(moving ? "Move failed." : "Copy failed.") : error;
        draw();
        return;
    }

    std::string target = folderName(m_browser.getCurrentPath());
    m_status = (moving ? "Moved to " : "Copied to ") + target;
    m_pendingOperation = PendingOperation::NONE;
    m_state = State::BROWSE;
    m_browser.setRoot(m_sourceDirectory.c_str());
    m_page = 0;
    reloadEntries();
    draw();
}

void FileManagerApp::cancelDestinationSelection() {
    m_pendingOperation = PendingOperation::NONE;
    m_status.clear();
    // Back to the file's folder with its action sheet open again.
    m_state = State::ACTIONS;
    m_browser.setRoot(m_sourceDirectory.c_str());
    m_page = 0;
    reloadEntries();
    for (size_t i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].name == m_selectedFile.name) {
            m_page = (int)i / rowsPerPage();
            break;
        }
    }
    draw();
}

void FileManagerApp::deleteSelectedFile() {
    std::string error;
    bool success = AppStorage::deleteFile(
        AppStorage::toRuntimePath(m_selectedFile.path), error);
    m_status = success ? "Deleted " + m_selectedFile.name
                       : (error.empty() ? std::string("Delete failed.") : error);
    m_state = State::BROWSE;
    m_browser.setRoot(m_sourceDirectory.c_str());
    reloadEntries();
    draw();
}

void FileManagerApp::enterDirectory(const std::string& name) {
    m_browser.enterDirectory(name.c_str());
    m_page = 0;
    m_status.clear();
    reloadEntries();
    draw();
}

void FileManagerApp::goUp() {
    std::string from = folderName(m_browser.getCurrentPath());
    m_browser.goUp();
    m_page = 0;
    m_status.clear();
    reloadEntries();
    // Land on the page that contains the folder we just left.
    for (size_t i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].isDirectory && m_entries[i].name == from) {
            m_page = (int)i / rowsPerPage();
            break;
        }
    }
    draw();
}

std::string FileManagerApp::destinationPath() const {
    std::string path = m_browser.getCurrentPath();
    if (path.empty()) path = "/";
    if (path.back() != '/') path += "/";
    return path + m_selectedFile.name;
}

std::string FileManagerApp::formatSize(size_t bytes) {
    char text[24];
    if (bytes >= 1024 * 1024) {
        snprintf(text, sizeof(text), "%.1f MB",
                 static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(text, sizeof(text), "%.1f KB",
                 static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(text, sizeof(text), "%u B", static_cast<unsigned>(bytes));
    }
    return text;
}
