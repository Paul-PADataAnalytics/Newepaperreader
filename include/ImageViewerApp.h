#pragma once

#include "Application.h"
#include "reader/FileBrowser.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pictures: browses JPEGs in /images and shows them full-screen on the
// portrait panel. Each picture is decoded once, aspect-fitted, brightened
// (SystemSettings::imageGamma), Floyd-Steinberg dithered to 16 greys and
// cached as a raw portrait framebuffer under /data/imgcache/.
class ImageViewerApp : public Application {
public:
    ImageViewerApp() = default;
    virtual ~ImageViewerApp();

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual void update() override;
    virtual uint32_t msUntilUpdate() const override { return m_state == STATE_CONVERTING ? 0 : UINT32_MAX; }

    // The full-screen viewer handles every tap itself while its overlay is
    // hidden; the overlay's back chevron goes through the system back.
    virtual bool wantsSystemBack() const override { return m_state != STATE_VIEW || m_overlay; }
    virtual bool onBack() override;

private:
    enum State {
        STATE_BROWSER,
        STATE_CONVERTING,
        STATE_ERROR,
        STATE_VIEW
    };

    enum Step {
        STEP_IDLE,
        STEP_PROBE,   // read the JPEG header, check size/memory limits
        STEP_DECODE,  // stb_image -> 8-bit grayscale
        STEP_RENDER,  // scale + gamma + dither, a band of rows per update()
        STEP_SAVE     // write the cached frame
    };

    State m_state = STATE_BROWSER;
    Step m_step = STEP_IDLE;
    FileBrowser m_fileBrowser;
    std::vector<FileInfo> m_files;
    int m_page = 0;
    int m_index = -1;          // picture shown / being converted
    bool m_rotated = false;    // current picture rendered rotated 90 degrees
    bool m_rotatePref = false; // keep rotating landscape pictures on Prev/Next
    bool m_overlay = false;    // viewer chrome visible

    // Conversion progress
    int m_progress = 0;
    std::string m_status;
    std::string m_errorTitle;
    std::string m_errorMessage;
    std::string m_cachePath;

    // Decoded source (grayscale, 1 byte per pixel)
    uint8_t* m_gray = nullptr;
    int m_srcW = 0;
    int m_srcH = 0;
    int m_srcComp = 0;

    // Render geometry/state (rotated coordinates)
    int m_dstX = 0, m_dstY = 0, m_dstW = 0, m_dstH = 0;
    int m_renderRow = 0;
    std::vector<int> m_tapStartX, m_tapCountX, m_tapOffX;
    std::vector<int> m_tapStartY, m_tapCountY, m_tapOffY;
    std::vector<float> m_tapWeightX, m_tapWeightY;
    std::vector<float> m_rowAcc;
    std::vector<int> m_errCur, m_errNext;
    std::vector<int16_t> m_gammaLut;

    // Finished 540x960 portrait frame (PSRAM on hardware)
    uint8_t* m_frame = nullptr;

    void updateFileList();
    int pageCount() const;

    void drawBrowser();
    void drawConverting(bool fullRefresh);
    void drawProgressArea();
    void drawError();
    void drawViewer();
    void drawOverlay(bool show);

    void openPicture(int index, bool rotated);
    void showRotated(bool rotated);
    void goToBrowser();
    void fail(const std::string& title, const std::string& message);

    bool ensureFrame();
    bool loadCache();
    void saveCache();
    std::string cachePathFor(const FileInfo& file, int brightness, bool rotated) const;
    bool probeSize(const std::string& path, int& w, int& h, int& comp) const;

    void stepProbe();
    void stepDecode();
    void prepareRender();
    void renderRows(int count);

    void freeDecode();
    void freeBuffers();
};
