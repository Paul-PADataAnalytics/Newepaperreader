#include "FileManagerApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "Launcher.h"
#include "reader/AppStorage.h"
#include "ui/UIFramework.h"
#include <cstdio>

extern uint8_t* framebuffer;
extern TypographyEngine typography;

namespace {
constexpr int ITEMS_PER_PAGE = LIB_MAIN_H / LIB_ROW_H;
}

void FileManagerApp::onCreate() {
    DisplayHAL::setPortrait(false);
    m_state = State::BROWSE;
    m_pendingOperation = PendingOperation::NONE;
    m_page = 0;
    m_sourceDirectory = "/";
    m_status.clear();
    m_browser.setRoot("/");
}

void FileManagerApp::onDestroy() {
}

void FileManagerApp::draw() {
    if (m_state == State::BROWSE || m_state == State::DESTINATION) {
        drawBrowser();
    } else if (m_state == State::ACTIONS) {
        drawActions();
    } else {
        drawDeleteConfirmation();
    }
}

void FileManagerApp::drawBrowser() {
    drawListLayout(m_state == State::DESTINATION ? "Choose Folder" : "File Browser",
                   m_state == State::DESTINATION);
}

void FileManagerApp::drawListLayout(const char* title, bool selectingDestination) {
    UIFramework::performFullScreenDraw(framebuffer, [this, title, selectingDestination]() {
        const int w = DisplayHAL::getWidth();
        const uint8_t fg = UIFramework::getForegroundColor();

        typography.setFontSize(40.0f);
        typography.renderText(title, 70, 5, framebuffer, fg);

        typography.setFontSize(22.0f);
        std::string headerText = m_browser.getCurrentPath();
        if (!m_status.empty()) {
            headerText += " - " + m_status;
        }
        headerText = truncateToWidth(headerText, 515);
        typography.renderText(headerText, 330, 17, framebuffer, fg);
        DisplayHAL::drawHLine(0, LIB_TOP_H - 1, w, fg, framebuffer);

        DisplayHAL::drawRect(LIB_SIDE_X, LIB_MAIN_Y, LIB_SIDE_W, LIB_MAIN_H, fg, framebuffer);
        const char* browseButtons[4] = {"Up", "SD Root", "Refresh", "Exit"};
        const char* destinationButtons[4] = {"Up", "SD Root", "Choose Here", "Cancel"};
        const char** buttons = selectingDestination ? destinationButtons : browseButtons;
        int buttonHeight = LIB_MAIN_H / 4;
        for (int i = 0; i < 4; i++) {
            int buttonY = LIB_MAIN_Y + i * buttonHeight;
            UIFramework::drawButton(framebuffer, LIB_SIDE_X + 10, buttonY + 10,
                                    LIB_SIDE_W - 20, buttonHeight - 20, buttons[i]);
        }

        DisplayHAL::fillRect(0, LIB_MAIN_Y, LIB_MAIN_W, LIB_MAIN_H, 0xFF, framebuffer);
        int pagingButtonWidth = LIB_PAGING_W - 20;
        int pagingButtonHeight = LIB_MAIN_H / 2 - 20;

        int upY = LIB_MAIN_Y + 10;
        UIFramework::drawButton(framebuffer, 25, upY, pagingButtonWidth, pagingButtonHeight, "/\\");

        int downY = LIB_MAIN_Y + LIB_MAIN_H / 2 + 10;
        UIFramework::drawButton(framebuffer, 25, downY, pagingButtonWidth, pagingButtonHeight, "\\/");

        const std::vector<FileInfo>& files = m_browser.getFiles();
        int startIndex = m_page * ITEMS_PER_PAGE;
        typography.setFontSize(34.0f);
        for (int row = 0; row < ITEMS_PER_PAGE && startIndex + row < (int)files.size(); row++) {
            const FileInfo& file = files[startIndex + row];
            int rowY = LIB_MAIN_Y + row * LIB_ROW_H + 22;
            std::string label = file.isDirectory ? "[DIR] " + file.name : file.name;
            label = truncateToWidth(label, LIB_LIST_W - 170);
            std::string detail = file.isDirectory ? "Folder" : formatSize(file.size);
            int detailWidth = typography.measureText(detail);
            typography.renderText(label, LIB_LIST_X + 20, rowY, framebuffer, fg);
            typography.renderText(detail, LIB_LIST_X + LIB_LIST_W - detailWidth - 20,
                                  rowY, framebuffer, fg);
            if (row < ITEMS_PER_PAGE - 1) {
                DisplayHAL::drawHLine(LIB_LIST_X + 10,
                                      LIB_MAIN_Y + (row + 1) * LIB_ROW_H,
                                      LIB_LIST_W - 20, 0x99, framebuffer);
            }
        }

        if (files.empty()) {
            typography.setFontSize(34.0f);
            typography.renderText("This folder is empty.", LIB_LIST_X + 20,
                                  LIB_MAIN_Y + 50, framebuffer, fg);
        }
    });
}

void FileManagerApp::drawActions() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        const int w = DisplayHAL::getWidth();
        const uint8_t fg = UIFramework::getForegroundColor();

        typography.setFontSize(40.0f);
        typography.renderText("File Options", 70, 5, framebuffer, fg);
        typography.setFontSize(24.0f);
        typography.renderText(truncateToWidth(m_selectedFile.name, 520), 330, 16,
                              framebuffer, fg);
        DisplayHAL::drawHLine(0, LIB_TOP_H - 1, w, fg, framebuffer);

        const char* labels[4] = {"Move", "Copy", "Delete", "Cancel"};
        for (int i = 0; i < 4; i++) {
            int y = 85 + i * 105;
            UIFramework::drawButton(framebuffer, 230, y, 500, 80, labels[i]);
        }
        if (!m_status.empty()) {
            typography.setFontSize(22.0f);
            std::string status = truncateToWidth(m_status, 850);
            int statusWidth = typography.measureText(status);
            typography.renderText(status, (w - statusWidth) / 2, 505, framebuffer, fg);
        }
    });
}

void FileManagerApp::drawDeleteConfirmation() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        const int w = DisplayHAL::getWidth();
        const uint8_t fg = UIFramework::getForegroundColor();
        typography.setFontSize(42.0f);
        std::string prompt = "Delete " + truncateToWidth(m_selectedFile.name, 600) + "?";
        int promptWidth = typography.measureText(prompt);
        typography.renderText(prompt, (w - promptWidth) / 2, 130, framebuffer, fg);
        typography.setFontSize(24.0f);
        const char* warning = "This cannot be undone.";
        int warningWidth = typography.measureText(warning);
        typography.renderText(warning, (w - warningWidth) / 2, 210, framebuffer, fg);
        UIFramework::drawButton(framebuffer, 180, 310, 260, 90, "Delete");
        UIFramework::drawButton(framebuffer, 520, 310, 260, 90, "Cancel");
    });
}

void FileManagerApp::handleTouch(int x, int y) {
    if (m_state == State::ACTIONS) {
        if (x >= 230 && x <= 730) {
            if (y >= 85 && y <= 165) {
                beginDestinationSelection(PendingOperation::MOVE);
            } else if (y >= 190 && y <= 270) {
                beginDestinationSelection(PendingOperation::COPY);
            } else if (y >= 295 && y <= 375) {
                m_state = State::CONFIRM_DELETE;
                draw();
            } else if (y >= 400 && y <= 480) {
                m_state = State::BROWSE;
                m_status.clear();
                draw();
            }
        }
        return;
    }

    if (m_state == State::CONFIRM_DELETE) {
        if (y >= 310 && y <= 400) {
            if (x >= 180 && x <= 440) {
                deleteSelectedFile();
            } else if (x >= 520 && x <= 780) {
                m_state = State::ACTIONS;
                draw();
            }
        }
        return;
    }

    bool selectingDestination = m_state == State::DESTINATION;
    if (x >= LIB_SIDE_X && y >= LIB_MAIN_Y) {
        int buttonIndex = (y - LIB_MAIN_Y) / (LIB_MAIN_H / 4);
        if (buttonIndex == 0) {
            goUp();
        } else if (buttonIndex == 1) {
            m_browser.setRoot("/");
            m_page = 0;
            m_status.clear();
            draw();
        } else if (buttonIndex == 2) {
            if (selectingDestination) {
                finishOperation();
            } else {
                m_browser.refresh();
                m_page = 0;
                m_status = "Folder refreshed.";
                draw();
            }
        } else if (buttonIndex == 3) {
            if (selectingDestination) {
                cancelDestinationSelection();
            } else {
                extern void exitToSystemLauncher();
                exitToSystemLauncher();
            }
        }
        return;
    }

    if (x <= LIB_PAGING_W && y >= LIB_MAIN_Y) {
        if (y < LIB_MAIN_Y + LIB_MAIN_H / 2) {
            if (m_page > 0) m_page--;
        } else if ((m_page + 1) * ITEMS_PER_PAGE < (int)m_browser.getFiles().size()) {
            m_page++;
        }
        m_status.clear();
        draw();
        return;
    }

    if (x > LIB_PAGING_W && x < LIB_SIDE_X && y >= LIB_MAIN_Y) {
        int row = (y - LIB_MAIN_Y) / LIB_ROW_H;
        int index = m_page * ITEMS_PER_PAGE + row;
        const std::vector<FileInfo>& files = m_browser.getFiles();
        if (index < 0 || index >= (int)files.size()) return;
        if (files[index].isDirectory) {
            enterDirectory(files[index].name);
        } else if (selectingDestination) {
            m_status = "Choose a folder using 'Choose Here'.";
            draw();
        } else {
            selectFile(files[index]);
        }
    }
}

void FileManagerApp::selectFile(const FileInfo& file) {
    m_selectedFile = file;
    m_sourceDirectory = m_browser.getCurrentPath();
    m_status.clear();
    m_state = State::ACTIONS;
    draw();
}

void FileManagerApp::beginDestinationSelection(PendingOperation operation) {
    m_pendingOperation = operation;
    m_browser.setRoot("/");
    m_page = 0;
    m_status = operation == PendingOperation::MOVE
        ? "Select where to move the file."
        : "Select where to copy the file.";
    m_state = State::DESTINATION;
    draw();
}

void FileManagerApp::finishOperation() {
    std::string source = AppStorage::toRuntimePath(m_selectedFile.path);
    std::string destination = AppStorage::toRuntimePath(destinationPath());
    std::string error;
    bool success = m_pendingOperation == PendingOperation::MOVE
        ? AppStorage::moveFile(source, destination, error)
        : AppStorage::copyFile(source, destination, error);

    if (!success) {
        m_status = error;
        draw();
        return;
    }

    m_browser.setRoot(m_sourceDirectory.c_str());
    m_page = 0;
    m_status = m_pendingOperation == PendingOperation::MOVE
        ? "File moved successfully."
        : "File copied successfully.";
    m_pendingOperation = PendingOperation::NONE;
    m_state = State::BROWSE;
    draw();
}

void FileManagerApp::cancelDestinationSelection() {
    m_browser.setRoot(m_sourceDirectory.c_str());
    m_page = 0;
    m_status.clear();
    m_pendingOperation = PendingOperation::NONE;
    m_state = State::ACTIONS;
    draw();
}

void FileManagerApp::deleteSelectedFile() {
    std::string error;
    bool success = AppStorage::deleteFile(
        AppStorage::toRuntimePath(m_selectedFile.path), error);
    m_browser.setRoot(m_sourceDirectory.c_str());
    m_page = 0;
    m_status = success ? "File deleted successfully." : error;
    m_state = success ? State::BROWSE : State::ACTIONS;
    draw();
}

void FileManagerApp::enterDirectory(const std::string& name) {
    m_browser.enterDirectory(name.c_str());
    m_page = 0;
    m_status.clear();
    draw();
}

void FileManagerApp::goUp() {
    m_browser.goUp();
    m_page = 0;
    m_status.clear();
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

std::string FileManagerApp::truncateToWidth(const std::string& text, int maxWidth) {
    if (typography.measureText(text) <= maxWidth) return text;
    std::string truncated = text;
    while (truncated.size() > 3 && typography.measureText(truncated + "...") > maxWidth) {
        truncated.pop_back();
    }
    return truncated + "...";
}
