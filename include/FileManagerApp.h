#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include <string>
#include <vector>

class FileManagerApp : public Application {
public:
    FileManagerApp() = default;
    virtual ~FileManagerApp() override = default;

    void onCreate() override;
    void onDestroy() override;
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

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
    // Sorted view of m_browser's listing (folders first; folders only while
    // choosing a destination).
    std::vector<FileInfo> m_entries;
    FileInfo m_selectedFile;
    int m_page = 0;
    std::string m_sourceDirectory = "/";
    std::string m_status;

    // Drawing (all draw into the framebuffer; callers flush).
    void drawScreen();
    void drawCaption();
    void drawList();
    void drawActionSheet();
    void drawDeleteDialog();

    // Flushing helpers.
    void showOverlay();
    void closeOverlay();

    void handleBrowseTouch(int x, int y);
    void handleActionsTouch(int x, int y);
    void handleDeleteTouch(int x, int y);

    void reloadEntries();
    int rowsPerPage() const;
    int pageCount() const;
    std::string headerTitle() const;
    std::string headerAction() const;

    void selectFile(const FileInfo& file);
    void beginDestinationSelection(PendingOperation operation);
    void finishOperation();
    void cancelDestinationSelection();
    void deleteSelectedFile();
    void enterDirectory(const std::string& name);
    void goUp();
    std::string destinationPath() const;
    static std::string formatSize(size_t bytes);
};
