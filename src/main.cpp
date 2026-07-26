#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "utilities.h"
#else
#include <stdio.h>
#include <unistd.h>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <dirent.h>
#endif

#include <vector>
#include <string>

#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include "Launcher.h"
#include "EReaderApp.h"
#include "AppComm.h"

#include "embedded_font.h"

uint8_t *framebuffer;
TypographyEngine typography;

#ifndef NATIVE_TESTING
// ---------------------------------------------------------------------------
// Touch interrupt path: a GPIO ISR on CPU1 wakes a high-priority task that
// reads the GT911 and pushes coordinates into a small queue. The main loop
// drains the queue, so a tap is handled as soon as possible even when loop()
// is busy rendering an EPD frame.
// ---------------------------------------------------------------------------
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <driver/gpio.h>

static QueueHandle_t touchEventQueue = nullptr;
static TaskHandle_t touchReaderTaskHandle = nullptr;

// BaseType_t flags used from ISR -> task and task -> main loop.
enum class TouchPhase : uint8_t {
    NONE = 0,
    DOWN = 1,
    UP   = 2
};

struct TouchEvent {
    int16_t x;
    int16_t y;
    TouchPhase phase;
};

static void IRAM_ATTR touchIsrHandler(void *arg) {
    if (touchReaderTaskHandle) {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(touchReaderTaskHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

static void touchReaderTask(void *param) {
    (void)param;
    for (;;) {
        // Block until the GT911 INT line wakes us.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int tx = -1, ty = -1;
        bool down = DisplayHAL::getTouch(tx, ty);
        TouchEvent ev{};
        ev.x = static_cast<int16_t>(tx);
        ev.y = static_cast<int16_t>(ty);
        ev.phase = down ? TouchPhase::DOWN : TouchPhase::UP;

        if (touchEventQueue) {
            xQueueSendFromISR(touchEventQueue, &ev, nullptr);
        }

        // Very small guard delay so we don't I2C hammer the controller while
        // the finger is still down; the next edge will wake us again.
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
#endif

#ifdef NATIVE_TESTING
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
#endif

static unsigned long lastTouchTime = 0;

// True while a book page is actively being read (EReaderApp's STATE_READ),
// as opposed to its library/file list view or any other app. EReaderApp is
// always app index 0 (see Launcher::init()). Used to exempt active reading
// from the 30s inactivity auto-lock timeout, and to decide what to put in
// the deep-sleep resume breadcrumb. Cross-platform (no ESP-IDF dependency),
// so it's available in both native and hardware builds.
static bool isCurrentlyReadingBook() {
    if (Launcher::getInstance().getActiveAppIndex() != 0) return false;
    Application* active = Launcher::getInstance().getActiveApp();
    if (!active) return false;
    EReaderApp* reader = static_cast<EReaderApp*>(active);
    return reader->isReadingBook();
}

void exitToSystemLauncher() {
    Launcher::getInstance().exitCurrentApp();
}

void launchSettingsApp() {
    Launcher::getInstance().switchToApp(2);
}

void processSerialCommands() {
#ifndef NATIVE_TESTING
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.startsWith("T ")) {
            int firstSpace = cmd.indexOf(' ');
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            if (secondSpace != -1) {
                int x = cmd.substring(firstSpace + 1, secondSpace).toInt();
                int y = cmd.substring(secondSpace + 1).toInt();
                DisplayHAL::injectTouch(x, y);
            }
        } else if (cmd == "S") {
            Serial.println("SCREENSHOT_START");
            Serial.println("P5");
            Serial.println("960 540");
            Serial.println("255");
            Serial.write(framebuffer, 960 * 540 / 2);
            Serial.println();
            Serial.println("SCREENSHOT_END");
        }
    }
#endif
}

void setup() {
#ifndef NATIVE_TESTING
    Serial.begin(115200);
    Serial.println("Starting LilyGO EPD47 E-Reader System v2.2.0...");
#else
    printf("Starting LilyGO EPD47 E-Reader System v2.2.0 (Native Mock)...\n");
#endif

    DisplayHAL::init();

#ifndef NATIVE_TESTING
    // Start the touch interrupt reader on CPU1 before anything else so taps
    // can be queued while the system is booting/drawing the first frame.
    touchEventQueue = xQueueCreate(8, sizeof(TouchEvent));
    xTaskCreatePinnedToCore(touchReaderTask, "touchReader", 4096, nullptr, 24,
                            &touchReaderTaskHandle, 1);

    pinMode(TOUCH_INT, INPUT_PULLUP);
    gpio_pullup_en(static_cast<gpio_num_t>(TOUCH_INT));
    // NOTE: GPIO0 is reserved for the EPD driver's CFG_STR line - never
    // reconfigure it here (see note in DisplayHAL::init()).

    // Manual sleep/wake toggle button. BUTTON_1 (GPIO21 on this S3 board,
    // per LilyGo-EPD47's utilities.h) is a genuinely free, dedicated button
    // pin - not shared with SD/I2C/touch/CFG_STR. Polled only (no ISR), so
    // unlike TOUCH_INT it never needs its interrupt-type register shared
    // with an app-level edge handler.
    pinMode(BUTTON_1, INPUT_PULLUP);
    gpio_pullup_en(static_cast<gpio_num_t>(BUTTON_1));

    gpio_install_isr_service(0);
    gpio_set_intr_type(static_cast<gpio_num_t>(TOUCH_INT), GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(TOUCH_INT), touchIsrHandler,
                         nullptr);
#endif

    DisplayHAL::frontBuffer = DisplayHAL::allocateFramebuffer();
    DisplayHAL::backBuffer = DisplayHAL::allocateFramebuffer();
    framebuffer = DisplayHAL::frontBuffer;
    if (!framebuffer || !DisplayHAL::backBuffer) {
#ifndef NATIVE_TESTING
        Serial.println("Failed to allocate framebuffers!");
#else
        printf("Failed to allocate framebuffers!\n");
#endif
        return;
    }

    memset(DisplayHAL::frontBuffer, 0xFF, 960 * 540 / 2);
    memset(DisplayHAL::backBuffer, 0xFF, 960 * 540 / 2);
    DisplayHAL::powerOn();
    DisplayHAL::clear();
    
    AppStorage::initialize();
    UIFramework::init();
    
    // Load default font for system launcher and apps
#ifndef NATIVE_TESTING
    if (!typography.loadFont("/sd/data/Roboto-Regular.ttf", 36.0f)) {
        typography.loadFontFromMemory(data_Roboto_Regular_ttf, data_Roboto_Regular_ttf_len, 36.0f);
    }
#else
    if (!typography.loadFont("data/Roboto-Regular.ttf", 32)) {
        if (!typography.loadFont("Roboto-Regular.ttf", 32)) {
            printf("Failed to load Roboto-Regular.ttf\n");
        }
    }
#endif

    // Initialize App Launcher
    Launcher::getInstance().init();
    
    // Start BLE background system service continuously on system startup
    AppComm::init();

#ifndef NATIVE_TESTING
    // If we just woke from our own deep-sleep lock mode (BUTTON_1 ext0
    // wakeup - see saveBreadcrumbAndEnterLockMode()), restore the breadcrumb
    // saved right before the chip reset: resume the last active app (and,
    // for the e-reader, jump straight to the exact book) instead of always
    // landing back on the main launcher menu. Any other reset cause (first
    // power-on, USB reset, firmware flash, crash) boots normally.
    bool resumedFromLock = false;
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        AppStorage::SavedSystemState saved = AppStorage::loadSystemState();
        if (saved.valid && saved.appIndex >= 0) {
            DisplayHAL::setPortrait(saved.isPortrait);
            if (saved.appIndex == 0 && saved.internalState == 1 && !saved.path.empty()) {
                // EReaderApp (index 0), was mid-book - skip its default
                // library draw and jump straight into the saved page.
                Launcher::getInstance().launchApp(0, false);
                EReaderApp* reader = static_cast<EReaderApp*>(Launcher::getInstance().getActiveApp());
                if (reader) {
                    reader->resumeAtPath(saved.path);
                    resumedFromLock = true;
                }
            } else {
                Launcher::getInstance().launchApp(saved.appIndex);
                resumedFromLock = true;
            }
        }
    }
    if (!resumedFromLock) {
        Launcher::getInstance().drawMenu();
    }
#else
    Launcher::getInstance().drawMenu();
#endif
}

static uint32_t lastTouchActivityTime = 0;
static bool isSystemSleeping = false;

#ifndef NATIVE_TESTING
// Edge-triggered, debounced BUTTON_1 press detector for the manual lock
// trigger. Mirrors the touch tap-debounce style in loop(): fires once on
// the down edge, then ignores the line until it's been seen released for a
// short debounce window (protects against switch bounce).
//
// State is FILE-SCOPE (not function-local static), matching the documented
// "BOOT Debounce Trap" lesson - though for deep sleep specifically there is
// no "wake without reboot" path to worry about: every wake is a full chip
// reset, so these statics simply reinitialize fresh on each boot.
static bool button1DownWaiting = false;
static uint32_t lastButton1UpTime = 0;
const uint32_t BUTTON1_DEBOUNCE_MS = 50;

static bool checkButton1DebouncedPress(uint32_t now) {
    bool rawPressed = (digitalRead(BUTTON_1) == LOW); // active-low, INPUT_PULLUP
    bool firedThisCall = false;

    if (rawPressed) {
        if (!button1DownWaiting && (now - lastButton1UpTime) >= BUTTON1_DEBOUNCE_MS) {
            button1DownWaiting = true;
            firedThisCall = true;
        }
    } else {
        if (button1DownWaiting) {
            lastButton1UpTime = now;
        }
        button1DownWaiting = false;
    }
    return firedThisCall;
}
#endif

static void drawSleepScreen() {
    bool wasPortrait = DisplayHAL::isPortrait();
    DisplayHAL::setPortrait(false); // Sleep screen is rendered landscape

    std::vector<std::string> rawImages;

#ifdef NATIVE_TESTING
    const char* testDirs[] = {"data/images", "images", "."};
    for (const char* dirPath : testDirs) {
        DIR* d = opendir(dirPath);
        if (d) {
            struct dirent* entry;
            while ((entry = readdir(d)) != nullptr) {
                std::string name = entry->d_name;
                if (name.length() > 4 && name.substr(name.length() - 4) == ".raw") {
                    rawImages.push_back(std::string(dirPath) + "/" + name);
                }
            }
            closedir(d);
            if (!rawImages.empty()) break;
        }
    }
#else
    File dir = SD.open("/images");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            std::string name = file.name();
            if (name.length() > 4 && name.substr(name.length() - 4) == ".raw") {
                std::string fullPath;
                if (name.find("/images/") == 0) {
                    fullPath = name;
                } else if (name.length() > 0 && name[0] == '/') {
                    fullPath = "/images" + name;
                } else {
                    fullPath = "/images/" + name;
                }
                rawImages.push_back(fullPath);
            }
            file = dir.openNextFile();
        }
    }
#endif

    bool imageLoaded = false;

    if (!rawImages.empty()) {
        static size_t sleepImageIdx = 0;
        size_t attempts = rawImages.size();
        
        UIFramework::performFullScreenDraw(framebuffer, [&]() {
            for (size_t a = 0; a < attempts; a++) {
                std::string selectedImage = rawImages[(sleepImageIdx + a) % rawImages.size()];
#ifdef NATIVE_TESTING
                FILE* f = fopen(selectedImage.c_str(), "rb");
                if (f) {
                    size_t readBytes = fread(framebuffer, 1, 960 * 540 / 2, f);
                    fclose(f);
                    if (readBytes == 960 * 540 / 2) {
                        imageLoaded = true;
                        sleepImageIdx = (sleepImageIdx + a + 1) % rawImages.size();
                        break;
                    }
                }
#else
                File f = SD.open(selectedImage.c_str(), FILE_READ);
                if (f) {
                    size_t readBytes = f.read(framebuffer, 960 * 540 / 2);
                    f.close();
                    if (readBytes == 960 * 540 / 2) {
                        imageLoaded = true;
                        sleepImageIdx = (sleepImageIdx + a + 1) % rawImages.size();
                        break;
                    }
                }
#endif
            }

            if (!imageLoaded) {
                int w = DisplayHAL::getWidth();
                int h = DisplayHAL::getHeight();
                typography.setFontSize(48.0f);
                std::string sleepMsg = "Sleeping zzzz";
                int tw = typography.measureText(sleepMsg);
                int tx = (w - tw) / 2;
                int ty = (h - 48) / 2;
                typography.renderText(sleepMsg, tx, ty, framebuffer, 0x00);
            }
        });
    } else {
        UIFramework::performFullScreenDraw(framebuffer, []() {
            int w = DisplayHAL::getWidth();
            int h = DisplayHAL::getHeight();
            typography.setFontSize(48.0f);
            std::string sleepMsg = "Sleeping zzzz";
            int tw = typography.measureText(sleepMsg);
            int tx = (w - tw) / 2;
            int ty = (h - 48) / 2;
            typography.renderText(sleepMsg, tx, ty, framebuffer, 0x00);
        });
    }

    DisplayHAL::setPortrait(wasPortrait);
}

#ifndef NATIVE_TESTING
// Deep-sleep "lock mode": saves a small resume breadcrumb, shows the sleep
// screen, powers off the panel, then puts the whole chip into deep sleep
// with ONLY BUTTON_1 armed as a wake source (no touch-wake at all - this is
// intentional, so the device can be put in a pocket and only wakes on a
// deliberate button press). esp_deep_sleep_start() NEVER RETURNS: the chip
// fully resets and re-runs setup() from scratch, which detects the
// ESP_SLEEP_WAKEUP_EXT0 cause and restores this breadcrumb.
static void saveBreadcrumbAndEnterLockMode() {
    AppStorage::SavedSystemState state;
    state.appIndex = Launcher::getInstance().getActiveAppIndex();
    state.isPortrait = DisplayHAL::isPortrait();
    state.internalState = 0;
    state.path = "";

    if (state.appIndex == 0) {
        // EReaderApp is always app index 0 (see Launcher::init()).
        EReaderApp* reader = static_cast<EReaderApp*>(Launcher::getInstance().getActiveApp());
        if (reader && reader->isReadingBook()) {
            // Force-persist the exact current page position now, rather than
            // relying on whatever the last touch-driven page turn happened
            // to save - guarantees resume lands on the precise page being
            // read at the moment of locking.
            reader->saveCurrentPosition();
            state.internalState = 1; // matches EReaderApp::STATE_READ
            state.path = reader->getCurrentBookPath();
        }
    }
    AppStorage::saveSystemState(state);

    drawSleepScreen();
    DisplayHAL::powerOff();

    // A deep-sleep wakeup source must be configured via the RTC_GPIO
    // peripheral - the regular digital GPIO matrix (and its gpio_wakeup_enable
    // used by the old light-sleep design) is powered down during deep sleep.
    // rtc_gpio_pullup_en/pulldown_dis configure the RTC-domain pull resistor
    // (separate from gpio_pullup_en, which only affects the digital domain),
    // and esp_sleep_enable_ext0_wakeup arms a single RTC-capable pin - BUTTON_1
    // (GPIO21) qualifies since RTC GPIOs on this S3 chip are GPIO0-21. Level 0
    // = wake when the line reads LOW (active-low button with pullup).
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(BUTTON_1));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(BUTTON_1));
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(BUTTON_1), 0);

    esp_deep_sleep_start(); // never returns
}
#endif

void loop() {
    // NOTE: GPIO0 is hard-wired to the EPD driver's CFG_STR line on this
    // board (see note in DisplayHAL::init()) and must never be read/driven
    // as a general-purpose "BOOT button" here - doing so breaks the EPD's
    // config-strobe and silently kills all display updates.
    processSerialCommands();

    uint32_t now = millis();
    if (lastTouchActivityTime == 0) {
        lastTouchActivityTime = now;
    }

    // Manual lock button (BUTTON_1 / GPIO21): triggers deep-sleep lock mode
    // immediately (see saveBreadcrumbAndEnterLockMode() below). The wake
    // side is handled entirely by setup() after the resulting chip reset -
    // loop() never runs again until then.
    bool button1Pressed = false;
#ifndef NATIVE_TESTING
    button1Pressed = checkButton1DebouncedPress(now);
#endif

    // Edge-triggered tap detector: fire once on the transition from
    // "not touching" to "touching", then ignore every sample until the finger
    // lifts and the line stays clear for a short debounce window.
    // This makes the first tap register as fast as possible while
    // preventing one long press or a bouncing lift from firing twice.
    static bool touchDownWaiting = false;
    static bool touchFired = false;
    static uint32_t lastUpTime = 0;
    const uint32_t TAP_DEBOUNCE_MS = 50;

    int tx = -1, ty = -1;
    bool gotTouchEvent = false;

#ifndef NATIVE_TESTING
    // Drain touch events queued by the interrupt-driven reader task on CPU1.
    // The task gives us one DOWN event per tap and an UP event when the finger
    // lifts. We fire exactly once on the first DOWN after a clear line, then
    // ignore everything until an UP has been seen.
    TouchEvent ev{};

    while (touchEventQueue && xQueueReceive(touchEventQueue, &ev, 0) == pdTRUE) {
        if (ev.phase == TouchPhase::DOWN) {
            // Real press - always counts as activity.
            gotTouchEvent = true;
            touchDownWaiting = true;
            tx = ev.x;
            ty = ev.y;
        } else {
            // UP events fire on every GT911 INT pulse, including electrical
            // noise/heartbeat pulses with no real touch. Only count this as
            // user activity (and reset the inactivity/sleep timer) if it's
            // the release of a press we actually saw start - otherwise the
            // 30s inactivity sleep timer would be reset every noise pulse
            // and never elapse.
            if (touchDownWaiting) {
                gotTouchEvent = true;
            }
            touchDownWaiting = false;
            touchFired = false;
            lastUpTime = now;
        }
    }
#else
    gotTouchEvent = DisplayHAL::getTouch(tx, ty);
    if (gotTouchEvent) {
        touchDownWaiting = true;
    } else {
        if (touchDownWaiting) {
            touchDownWaiting = false;
            touchFired = false;
            lastUpTime = now;
        }
    }
#endif

    if (gotTouchEvent) {
        lastTouchActivityTime = now;
    }

    bool touched = touchDownWaiting && (static_cast<int32_t>(now - lastUpTime) >= TAP_DEBOUNCE_MS);

    if (touched) {
        lastTouchActivityTime = now;
    }

    if (isSystemSleeping) {
        // Only reachable in NATIVE_TESTING now. On real hardware "sleeping"
        // means deep-sleep lock mode (see saveBreadcrumbAndEnterLockMode()
        // below), which never returns to loop() at all - the chip fully
        // resets and setup() handles the wake side after reboot. This
        // NATIVE_TESTING-only branch is a lightweight simulated sleep/wake so
        // other app logic can still be exercised without real ESP-IDF deep
        // sleep APIs; touch always "wakes" it here purely for dev
        // convenience (unlike real hardware, where only the button wakes).
#ifdef NATIVE_TESTING
        if (touched) {
            isSystemSleeping = false;
            lastTouchActivityTime = now;
            lastTouchTime = 0; // Reset debounce timer to zero for instant waking touch responsiveness

            DisplayHAL::powerOn();

            if (Launcher::getInstance().getActiveApp()) {
                Launcher::getInstance().getActiveApp()->draw();
            } else {
                Launcher::getInstance().drawMenu();
            }
            return; // Consume touch for waking up
        }
        DisplayHAL::handleEvents();
        if (DisplayHAL::windowShouldClose()) exit(0);
        usleep(100000);
#endif
        return;
    }

    // Reading-mode exemption: don't let the 30s inactivity timer auto-lock
    // the device while a book page is actively displayed (e.g. mid-read with
    // no touches for a while is normal, not "left unattended"). A manual
    // BUTTON_1 press still always locks the device, even while reading.
    bool inReadingPageView = isCurrentlyReadingBook();
    bool inactivityTimeout = !inReadingPageView && ((now - lastTouchActivityTime) >= 30000);

    if (button1Pressed || inactivityTimeout) {
#ifndef NATIVE_TESTING
        saveBreadcrumbAndEnterLockMode(); // never returns - chip resets
#else
        isSystemSleeping = true;
        drawSleepScreen();
        DisplayHAL::powerOff();
#endif
        return;
    }

    uint32_t loopStartTime = millis();

    Launcher::getInstance().loop();

#ifndef NATIVE_TESTING
    // Handle the queued tap once, immediately, after the main loop processing.
    if (touchDownWaiting && !touchFired) {
        touchFired = true;
        Serial.printf("Handling touch at %d, %d\n", tx, ty);
        Launcher::getInstance().handleTouch(tx, ty);
    }
#else
    if (gotTouchEvent && !touchFired) {
        touchFired = true;
        printf("Handling touch at %d, %d\n", tx, ty);
        Launcher::getInstance().handleTouch(tx, ty);
    }
    if (!gotTouchEvent) {
        touchFired = false;
    }

    DisplayHAL::handleEvents();
    if (DisplayHAL::windowShouldClose()) {
        exit(0);
    }
    usleep(100000); // 100ms
#endif

    uint32_t loopEndTime = millis();
    // If the loop/touch handler took longer than 200ms (e.g. an EPD refresh occurred),
    // drain the touch queue so we don't accidentally process taps that happened 
    // *while* the screen was updating.
    if ((loopEndTime - loopStartTime) > 200) {
#ifndef NATIVE_TESTING
        TouchEvent dump;
        while (touchEventQueue && xQueueReceive(touchEventQueue, &dump, 0) == pdTRUE) { }
#endif
        touchDownWaiting = false;
        touchFired = false;
    }
}

#ifdef NATIVE_TESTING
#include <string.h>
int main(int argc, char** argv) {
    const char* dumpPgm = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug-screen") == 0) {
            DisplayHAL::setDebugScreen(true);
        } else if (strcmp(argv[i], "--dump-pgm") == 0 && i + 1 < argc) {
            dumpPgm = argv[++i];
        }
    }
    setup();
    if (dumpPgm) {
        FILE* f = fopen(dumpPgm, "wb");
        if (f) {
            fprintf(f, "P5\n960 540\n255\n");
            fwrite(DisplayHAL::frontBuffer, 1, 960 * 540 / 2, f);
            fclose(f);
            printf("Dumped framebuffer PGM to %s\n", dumpPgm);
        }
        return 0;
    }
    while (true) {
        loop();
    }
    return 0;
}
#endif
