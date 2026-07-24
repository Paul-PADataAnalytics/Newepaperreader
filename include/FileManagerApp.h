#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include <string>

class FileManagerApp : public Application {
public:
    FileManagerApp() = default;
    virtual ~FileManagerApp() override = default;

    void onCreate() override;
    void onDestroy() override;
    void draw() override;
    void handleTouch(int x, int y) override;

private:
    enum class State {
        BROWSE,
        ACTIONS,
        DESTINATION,
        CONFIRM_DELETE
    };

    enum class PendingOperation {
        NONE,
        COPY,
        MOVE
    };

    State m_state = State::BROWSE;
    PendingOperation m_pendingOperation = PendingOperation::NONE;
    FileBrowser m_browser;
    FileInfo m_selectedFile;
    int m_page = 0;
    std::string m_sourceDirectory = "/";
    std::string m_status;

    void drawBrowser();
    void drawActions();
    void drawDeleteConfirmation();
    void drawListLayout(const char* title, bool selectingDestination);
    void selectFile(const FileInfo& file);
    void beginDestinationSelection(PendingOperation operation);
    void finishOperation();
    void cancelDestinationSelection();
    void deleteSelectedFile();
    void enterDirectory(const std::string& name);
    void goUp();
    std::string destinationPath() const;
    static std::string formatSize(size_t bytes);
    static std::string truncateToWidth(const std::string& text, int maxWidth);
};
