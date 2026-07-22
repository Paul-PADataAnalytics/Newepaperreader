#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include <string>
#include <vector>

class ImageViewerApp : public Application {
public:
    ImageViewerApp() = default;
    virtual ~ImageViewerApp();

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual void update() override;

private:
    enum State {
        STATE_BROWSER,
        STATE_CONVERTING,
        STATE_VIEW
    };

    State m_state = STATE_BROWSER;
    FileBrowser m_fileBrowser;
    std::vector<FileInfo> m_files;
    int m_page = 0;
    int m_selectedIndex = -1;

    // Conversion state machine
    int m_conversionStep = 0; // 0=idle, 1=loading, 2=decoding, 3=converting, 4=saving
    int m_conversionLine = 0;
    int m_progress = 0;
    std::string m_statusMsg = "";
    
    // File paths
    std::string m_selectedPath = "";
    std::string m_cachePath = "";

    // Image buffers (allocated in PSRAM where appropriate)
    uint8_t* m_fileBuffer = nullptr;
    size_t m_fileBufferSize = 0;
    unsigned char* m_rgbData = nullptr;
    int m_imgWidth = 0;
    int m_imgHeight = 0;
    int m_imgChannels = 0;
    uint8_t* m_convertedFrame = nullptr;

    void updateFileList();
    void drawBrowser();
    void drawConverting();
    void drawView();

    void startConversion(const std::string& path);
    void freeBuffers();
};
