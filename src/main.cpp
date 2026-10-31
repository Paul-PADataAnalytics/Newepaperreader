#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <soc/io_mux_reg.h>
#include <soc/gpio_periph.h>
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

#include <algorithm>
#include <vector>
#include <string>
#include <cmath>

#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include "reader/AppStorage.h"
#include "Launcher.h"
#include "EReaderApp.h"
#include "AppComm.h"
#include "PowerManager.h"
#include "SystemSettings.h"

#include "embedded_font.h"

uint8_t *framebuffer;
TypographyEngine typography;

#ifndef NATIVE_TESTING
// ---------------------------------------------------------------------------
// Touch interrupt path: a GPIO ISR on CPU1 wakes a high-priority task that
// reads the GT911 and pushes coordinates into a small queue. The main loop
// blocks on that queue while idle, so the CPU sleeps until something happens.
// ---------------------------------------------------------------------------
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

// BUTTON_1 debounce state (see checkButton1DebouncedPress()).
static bool button1DownWaiting = false;
static uint32_t lastButton1UpTime = 0;

static QueueHandle_t touchEventQueue = nullptr;
static TaskHandle_t touchReaderTaskHandle = nullptr;
static SemaphoreHandle_t touchReadMutex = nullptr;
static volatile uint32_t s_touchWakeups = 0;
static volatile uint32_t s_touchDowns = 0;
// Light-sleep residency, reported by STATUS.
static uint32_t s_lightSleeps = 0;
static uint32_t s_timerWakes = 0;
static uint64_t s_lightSleepUs = 0;
static int64_t s_statsSinceUs = 0;

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
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(touchReadMutex, portMAX_DELAY);
        s_touchWakeups++;

        int tx = -1, ty = -1;
        bool down = DisplayHAL::getTouch(tx, ty);
        if (down) s_touchDowns++;
        // The GT911 pulses INT at its scan rate (~100Hz) even with nothing
        // touching it. Only forward state changes and held presses, so an
        // idle panel doesn't wake the main loop 100 times a second.
        static bool lastDown = false;
        bool forward = down || lastDown;
        lastDown = down;
        if (!forward) {
            xSemaphoreGive(touchReadMutex);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        TouchEvent ev{};
        ev.x = static_cast<int16_t>(tx);
        ev.y = static_cast<int16_t>(ty);
        ev.phase = down ? TouchPhase::DOWN : TouchPhase::UP;

        if (touchEventQueue) {
            xQueueSend(touchEventQueue, &ev, 0);
        }
        xSemaphoreGive(touchReadMutex);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
#endif

#ifdef NATIVE_TESTING
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

// EReaderApp is always APP_READER (index 0).
static EReaderApp* activeReader() {
    if (Launcher::getInstance().getActiveAppIndex() != APP_READER) return nullptr;
    return static_cast<EReaderApp*>(Launcher::getInstance().getActiveApp());
}

void exitToSystemLauncher() {
    Launcher::getInstance().requestExit();
}

void launchSettingsApp() {
    Launcher::getInstance().requestLaunch(APP_SETTINGS);
}

static void enterLockMode(PowerManager::SleepReason reason);
static uint32_t s_testTimerWakeSec = 0;
#ifndef NATIVE_TESTING
static uint32_t s_usbSleepTestUntil = 0;
#endif

// ---------------------------------------------------------------------------
// Serial console (development): "T x y" raw touch, "TL x y" touch in
// on-screen coordinates, "S" screenshot, "SLEEP", "STATUS".
// ---------------------------------------------------------------------------
void processSerialCommands() {
#ifndef NATIVE_TESTING
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("T ") || cmd.startsWith("TL ")) {
        bool logical = cmd.startsWith("TL ");
        int firstSpace = cmd.indexOf(' ');
        int secondSpace = cmd.indexOf(' ', firstSpace + 1);
        if (secondSpace != -1) {
            int x = cmd.substring(firstSpace + 1, secondSpace).toInt();
            int y = cmd.substring(secondSpace + 1).toInt();
            if (logical) {
                // Deterministic test tap at on-screen coordinates, straight
                // to the UI (bypasses the GT911 press/release debouncing).
                PowerManager::noteUserActivity();
                PowerManager::boostCpu();
                Serial.printf("Handling touch at %d, %d\n", x, y);
                Launcher::getInstance().handleTouch(x, y);
            } else {
                DisplayHAL::injectTouch(x, y);
                if (touchReaderTaskHandle) xTaskNotifyGive(touchReaderTaskHandle);
            }
        }
    } else if (cmd == "S") {
        Serial.println("SCREENSHOT_START");
        Serial.println("P5");
        Serial.printf("%d %d\n", DisplayHAL::getWidth(), DisplayHAL::getHeight());
        Serial.println("255");
        Serial.write(framebuffer, 960 * 540 / 2);
        Serial.println();
        Serial.println("SCREENSHOT_END");
    } else if (cmd == "SLEEP") {
        enterLockMode(PowerManager::SLEEP_DEV_COMMAND);
    } else if (cmd.startsWith("SLEEPT ")) {
        // Development: sleep, but also wake on a timer so the full
        // sleep/resume cycle can be tested without pressing the button.
        s_testTimerWakeSec = (uint32_t)cmd.substring(7).toInt();
        enterLockMode(PowerManager::SLEEP_DEV_COMMAND);
    } else if (cmd.startsWith("USBSLEEP ")) {
        // Development: allow idle light sleep for n seconds despite the USB
        // host, to measure battery-like idle behaviour. The console is
        // unreachable meanwhile and for a while after. Counters restart.
        s_usbSleepTestUntil = (millis() + (uint32_t)cmd.substring(9).toInt() * 1000u) | 1u;
        s_lightSleeps = s_timerWakes = 0;
        s_lightSleepUs = 0;
        s_statsSinceUs = esp_timer_get_time();
        Serial.println("USBSLEEP started");
    } else if (cmd == "EPDT") { // TEMP diagnostic
        epd_poweron(); delay(20); uint32_t on = analogReadMilliVolts(BATT_PIN);
        epd_poweroff(); delay(300); uint32_t off = analogReadMilliVolts(BATT_PIN);
        epd_poweroff_all(); delay(300); uint32_t all = analogReadMilliVolts(BATT_PIN);
        epd_poweroff(); delay(300); uint32_t off2 = analogReadMilliVolts(BATT_PIN);
        Serial.printf("EPDT on=%u off=%u all=%u off2=%u slpsel0=%d slpsel12=%d slpsel13=%d slpsel47=%d\n",
                      (unsigned)on, (unsigned)off, (unsigned)all, (unsigned)off2,
                      (int)REG_GET_BIT(GPIO_PIN_MUX_REG[0], SLP_SEL), (int)REG_GET_BIT(GPIO_PIN_MUX_REG[12], SLP_SEL),
                      (int)REG_GET_BIT(GPIO_PIN_MUX_REG[13], SLP_SEL), (int)REG_GET_BIT(GPIO_PIN_MUX_REG[47], SLP_SEL));
    } else if (cmd == "STATUS") {
        Serial.printf("STATUS lastSleep=[%s] usbHost=%d gt911=%d/%d touch=%u/%u batt=%.3fV (%d%%) usb=%d cpu=%uMHz idle=%ums heap=%u psram=%u ble=%d conn=%d app=%d\n",
                      PowerManager::lastSleepSummary(), PowerManager::isUsbHostActive() ? 1 : 0,
                      DisplayHAL::touchInitOk() ? 1 : 0, DisplayHAL::touchResponds() ? 1 : 0,
                      (unsigned)s_touchWakeups, (unsigned)s_touchDowns,
                      DisplayHAL::getBatteryVoltage(), DisplayHAL::getBatteryPercent(),
                      PowerManager::isUsbConnected() ? 1 : 0, (unsigned)getCpuFrequencyMhz(),
                      (unsigned)PowerManager::msSinceActivity(), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram(), AppComm::isInitialized() ? 1 : 0,
                      AppComm::isConnected() ? 1 : 0, Launcher::getInstance().getActiveAppIndex());
        uint64_t upUs = esp_timer_get_time() - s_statsSinceUs;
        Serial.printf("STATUS lightSleep n=%u timerWakes=%u slept=%ums of %ums (%u%%)\n",
                      (unsigned)s_lightSleeps, (unsigned)s_timerWakes, (unsigned)(s_lightSleepUs / 1000),
                      (unsigned)(upUs / 1000), (unsigned)(upUs ? s_lightSleepUs * 100 / upUs : 0));
    }
#endif
}

void setup() {
    PowerManager::restoreAfterWake();
#ifndef NATIVE_TESTING
    Serial.begin(115200);
    Serial.println("Starting LilyGO EPD47 E-Reader System v3.0.0...");
#else
    printf("Starting LilyGO EPD47 E-Reader System v3.0.0 (Native Mock)...\n");
#endif

    DisplayHAL::init();

#ifndef NATIVE_TESTING
    touchEventQueue = xQueueCreate(8, sizeof(TouchEvent));
    touchReadMutex = xSemaphoreCreateMutex();
    if (!touchEventQueue || !touchReadMutex) {
        Serial.println("[power] touch queue/mutex allocation failed");
        return;
    }
    xTaskCreatePinnedToCore(touchReaderTask, "touchReader", 4096, nullptr, 24,
                            &touchReaderTaskHandle, 1);

    // NOTE: GPIO0 is reserved for the EPD driver's CFG_STR line - never
    // reconfigure it here (see note in DisplayHAL::init()).

    // BUTTON_1 (GPIO21): dedicated sleep/wake button, polled from loop().
    rtc_gpio_deinit(static_cast<gpio_num_t>(BUTTON_1));
    pinMode(BUTTON_1, INPUT_PULLUP);
    gpio_pullup_en(static_cast<gpio_num_t>(BUTTON_1));
    // The press that woke us may still be held when loop() starts; it must
    // be released before it can count as a new "sleep" press.
    if (digitalRead(BUTTON_1) == LOW) button1DownWaiting = true;

    gpio_install_isr_service(0);
    gpio_set_intr_type(static_cast<gpio_num_t>(TOUCH_INT), GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(TOUCH_INT), touchIsrHandler, nullptr);
#endif

    DisplayHAL::frontBuffer = DisplayHAL::allocateFramebuffer();
    DisplayHAL::backBuffer = DisplayHAL::allocateFramebuffer();
    framebuffer = DisplayHAL::frontBuffer;
    if (!framebuffer || !DisplayHAL::backBuffer) {
        printf("Failed to allocate framebuffers!\n");
        return;
    }
    memset(DisplayHAL::frontBuffer, 0xFF, 960 * 540 / 2);
    memset(DisplayHAL::backBuffer, 0xFF, 960 * 540 / 2);

    // The whole system is portrait-only.
    DisplayHAL::setPortrait(true);

    AppStorage::initialize();
    SystemSettings::load();
    SystemSettings::applyTimezone();
    DisplayHAL::setDarkMode(SystemSettings::get().darkMode);
    EReaderApp::setReadingFontSize(SystemSettings::get().readingFontSize);
    UIFramework::init();

#ifndef NATIVE_TESTING
    // The embedded copy is memory-mapped from flash: no SD read, no PSRAM
    // copy - noticeably faster boot (and every wake is a boot).
    if (!typography.loadFontFromMemory(data_Roboto_Regular_ttf, data_Roboto_Regular_ttf_len, 36.0f)) {
        typography.loadFont("/sd/data/Roboto-Regular.ttf", 36.0f);
    }
#else
    if (!typography.loadFont("data/Roboto-Regular.ttf", 32)) {
        if (!typography.loadFont("Roboto-Regular.ttf", 32)) {
            printf("Failed to load Roboto-Regular.ttf\n");
        }
    }
#endif

    Launcher::getInstance().init();

    if (SystemSettings::get().bleEnabled) {
        AppComm::init();
    }

    bool resumed = false;
#ifndef NATIVE_TESTING
    // Waking from lock mode (BUTTON_1 ext0) restores the app that was open,
    // and for the e-reader the exact page. Any other reset boots to home.
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("Wake cause: %d\n", (int)cause);
    if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_TIMER) {
        AppStorage::SavedSystemState saved = AppStorage::loadSystemState();
        if (saved.valid && saved.appIndex >= 0 && saved.appIndex < APP_COUNT) {
            if (saved.appIndex == APP_READER && saved.internalState == 1 && !saved.path.empty()) {
                Launcher::getInstance().launchApp(APP_READER, false);
                EReaderApp* reader = activeReader();
                if (reader) {
                    reader->resumeAtPath(saved.path);
                    resumed = true;
                }
            } else {
                Launcher::getInstance().launchApp(saved.appIndex);
                resumed = true;
            }
        }
    }
#endif
    if (!resumed) {
        Launcher::getInstance().drawMenu();
    }
    PowerManager::noteUserActivity();
}

#ifndef NATIVE_TESTING
// Edge-triggered, debounced BUTTON_1 press detector. State is file-scope;
// every deep-sleep wake is a full reset, so it reinitialises on each boot.
const uint32_t BUTTON1_DEBOUNCE_MS = 50;

static bool checkButton1DebouncedPress(uint32_t now) {
    bool rawPressed = (digitalRead(BUTTON_1) == LOW);
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

// ---------------------------------------------------------------------------
// Sleep screen
// ---------------------------------------------------------------------------

// Reads one pixel from a raw physical-layout (960x540) framebuffer image.
static inline uint8_t rawPhysicalPixel(const uint8_t* buf, int x, int y) {
    uint8_t b = buf[(y * 960 + x) >> 1];
#ifndef NATIVE_TESTING
    return (x & 1) ? (b >> 4) : (b & 0x0F);
#else
    return (x & 1) ? (b & 0x0F) : (b >> 4);
#endif
}

static std::vector<std::string> listSleepImages() {
    std::vector<std::string> rawImages;
#ifdef NATIVE_TESTING
    const char* testDirs[] = {"data/images", "images"};
    for (const char* dirPath : testDirs) {
        DIR* d = opendir(dirPath);
        if (!d) continue;
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
#else
    File dir = SD.open("/images");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            std::string name = file.name();
            size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            if (name.length() > 4 && name.substr(name.length() - 4) == ".raw" && name[0] != '.') {
                rawImages.push_back("/images/" + name);
            }
            file = dir.openNextFile();
        }
    }
#endif
    return rawImages;
}

static bool readRawImage(const std::string& path, uint8_t* out) {
    const size_t want = 960 * 540 / 2;
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t n = fread(out, 1, want, f);
    fclose(f);
    return n == want;
#else
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    size_t n = f.read(out, want);
    f.close();
    return n == want;
#endif
}

#ifndef NATIVE_TESTING
RTC_DATA_ATTR static uint32_t s_sleepImageIdx = 0;
#else
static uint32_t s_sleepImageIdx = 0;
#endif

static void drawSleepScreen(const std::string& bookTitle) {
    DisplayHAL::setPortrait(true);
    std::vector<std::string> images;
    if (SystemSettings::get().sleepShowsImages) images = listSleepImages();

    uint8_t* raw = nullptr;
    if (!images.empty()) {
        raw = DisplayHAL::allocateFramebuffer();
        bool ok = false;
        for (size_t a = 0; raw && a < images.size() && !ok; a++) {
            size_t idx = (s_sleepImageIdx + a) % images.size();
            if (readRawImage(images[idx], raw)) {
                ok = true;
                s_sleepImageIdx = (uint32_t)(idx + 1);
            }
        }
        if (!ok && raw) {
            DisplayHAL::freeFramebuffer(raw);
            raw = nullptr;
        }
    }

    // Same brightening curve the image viewer uses, applied to the 16 levels.
    uint8_t lut[16];
    float gamma = SystemSettings::imageGamma();
    for (int i = 0; i < 16; i++) lut[i] = (uint8_t)lroundf(15.0f * powf(i / 15.0f, gamma));

    int battery = DisplayHAL::getBatteryPercent();

    UIFramework::performFullScreenDraw(framebuffer, [&]() {
        using namespace UI;
        if (raw) {
            // Raw files are stored in the panel's physical landscape layout;
            // map into the portrait framebuffer (logical (lx,ly) is
            // physical (ly, 539 - lx)).
            for (int ly = 0; ly < H; ly++) {
                for (int lx = 0; lx < W; lx++) {
                    DisplayHAL::setPixel(lx, ly, lut[rawPhysicalPixel(raw, ly, 539 - lx)], framebuffer);
                }
            }
            int bh = 92;
            int by = H - bh - 28;
            fillRoundRect(MARGIN, by, W - 2 * MARGIN, bh, 18, WHITE);
            strokeRoundRect(MARGIN, by, W - 2 * MARGIN, bh, 18, 2, BLACK);
            textCentered("Sleeping", W / 2, by + 12, FONT_BODY, BLACK, true);
            textCentered("Press the side button to wake  \xC2\xB7  " + std::to_string(battery) + "%",
                         W / 2, by + 52, FONT_CAPTION, DARK);
        } else {
            icon(ICON_BOOK, (W - 160) / 2, 230, 160);
            textCentered("Sleeping", W / 2, 420, 52.0f, BLACK, true);
            if (!bookTitle.empty()) {
                textCentered("You were reading", W / 2, 520, FONT_SMALL, DARK);
                textWrapped(bookTitle, MARGIN * 2, 556, W - MARGIN * 4, FONT_BODY + 2, BLACK, 2, 6, true);
            }
            hRule(H - 190, LIGHT, MARGIN * 3, W - MARGIN * 3, 2);
            batteryIcon(W / 2 - 50, H - 150, battery, false, BLACK);
            text(std::to_string(battery) + "%", W / 2 - 8, H - 156, FONT_SMALL, INK, true);
            textCentered("Press the side button to wake", W / 2, H - 100, FONT_SMALL, DARK);
        }
    });

    if (raw) DisplayHAL::freeFramebuffer(raw);
}

// ---------------------------------------------------------------------------
// Lock mode (deep sleep)
// ---------------------------------------------------------------------------

#ifdef NATIVE_TESTING
static bool isSystemSleeping = false;
#endif

// Saves a resume breadcrumb, shows the sleep screen, then shuts everything
// down and deep-sleeps with ONLY BUTTON_1 armed (touch never wakes it, so it
// can live in a bag). esp_deep_sleep_start() never returns: waking resets
// the chip and setup() restores the breadcrumb.
static void enterLockMode(PowerManager::SleepReason reason) {
    PowerManager::recordSleep(reason);
    AppStorage::SavedSystemState state;
    state.appIndex = Launcher::getInstance().getActiveAppIndex();
    state.isPortrait = true;
    state.internalState = 0;
    state.path = "";

    std::string bookTitle;
    EReaderApp* reader = activeReader();
    if (reader && reader->isReadingBook()) {
        reader->prepareForSleep();
        state.internalState = 1; // EReaderApp::STATE_READ
        state.path = reader->getCurrentBookPath();
        bookTitle = reader->getCurrentBookTitle();
    }
    AppStorage::saveSystemState(state);

    drawSleepScreen(bookTitle);

#ifndef NATIVE_TESTING
    Serial.println("Entering deep sleep (BUTTON_1 wakes)");
    Serial.flush();
    AppComm::deinit();
    PowerManager::prepareHardwareForDeepSleep();

    // ext0 is level-triggered: don't arm it while the button is still held
    // or the chip would wake straight back up.
    while (digitalRead(BUTTON_1) == LOW) delay(10);
    delay(30);

    rtc_gpio_pullup_en(static_cast<gpio_num_t>(BUTTON_1));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(BUTTON_1));
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(BUTTON_1), 0);
    if (s_testTimerWakeSec > 0) esp_sleep_enable_timer_wakeup((uint64_t)s_testTimerWakeSec * 1000000ULL);
    esp_deep_sleep_start(); // never returns
#else
    printf("NATIVE_TESTING: simulated sleep (click to wake)\n");
    isSystemSleeping = true;
#endif
}

void requestSleepNow() {
    enterLockMode(PowerManager::SLEEP_USER_MENU);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

#ifndef NATIVE_TESTING
// Light sleep is only worth entering for at least this long.
static const uint32_t MIN_LIGHT_SLEEP_MS = 20;
// Safety net for anything polled from loop() that has no wake source.
static const uint32_t MAX_LIGHT_SLEEP_MS = 60000;

// How long the device can stay in light sleep with nothing but the touch
// panel and the side button able to wake it.
static uint32_t lightSleepBudgetMs() {
    uint32_t ms = MAX_LIGHT_SLEEP_MS;
    ms = std::min(ms, Launcher::getInstance().msUntilUpdate());
    // Wake in time to go to deep sleep when the inactivity timeout runs out.
    uint32_t autoSleep = PowerManager::msUntilAutoSleep();
    if (autoSleep != UINT32_MAX) ms = std::min(ms, autoSleep + 50);
    return ms;
}

// The USB Serial/JTAG link (the development console) stops working once the
// chip light-sleeps, and a USB host powers the device anyway.
static bool usbConsoleNeedsAwake() {
    if (s_usbSleepTestUntil != 0) {
        int32_t left = (int32_t)(s_usbSleepTestUntil - millis());
        if (left > 0) return false;
        // After a USBSLEEP test, give the host time to bring the link back
        // (it may no longer be sending the SOFs isUsbHostActive() looks for).
        if (left > -600000) return true;
        s_usbSleepTestUntil = 0;
    }
    return PowerManager::isUsbHostActive();
}

static bool sleepUntilInput() {
    if (!PowerManager::canLightSleep() || usbConsoleNeedsAwake()) return false;
    const uint32_t budgetMs = lightSleepBudgetMs();
    if (budgetMs < MIN_LIGHT_SLEEP_MS) return false;
    if (!touchReadMutex || xSemaphoreTake(touchReadMutex, 0) != pdTRUE) return false;
    const gpio_num_t touchPin = static_cast<gpio_num_t>(TOUCH_INT);
    const gpio_num_t buttonPin = static_cast<gpio_num_t>(BUTTON_1);
    if (digitalRead(TOUCH_INT) == LOW || digitalRead(BUTTON_1) == LOW ||
        uxQueueMessagesWaiting(touchEventQueue) > 0) {
        xSemaphoreGive(touchReadMutex);
        return false;
    }
    Serial.flush();
    PowerManager::relaxCpu();
    gpio_sleep_sel_dis(touchPin);
    gpio_sleep_sel_dis(buttonPin);
    gpio_intr_disable(touchPin);
    esp_err_t err = gpio_wakeup_enable(touchPin, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) err = gpio_wakeup_enable(buttonPin, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) err = esp_sleep_enable_gpio_wakeup();
    if (err == ESP_OK) err = esp_sleep_enable_timer_wakeup((uint64_t)budgetMs * 1000ULL);
    const int64_t sleepStart = esp_timer_get_time();
    if (err == ESP_OK) err = esp_light_sleep_start();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (err == ESP_OK) {
        s_lightSleeps++;
        s_lightSleepUs += (uint64_t)(esp_timer_get_time() - sleepStart);
        if (cause == ESP_SLEEP_WAKEUP_TIMER) s_timerWakes++;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable(touchPin);
    gpio_wakeup_disable(buttonPin);
    gpio_set_intr_type(touchPin, GPIO_INTR_NEGEDGE);
    gpio_intr_enable(touchPin);
    xSemaphoreGive(touchReadMutex);
    if (err != ESP_OK) {
        Serial.printf("[power] light sleep failed: %s\n", esp_err_to_name(err));
        delay(10);
        return false;
    }
    if (cause == ESP_SLEEP_WAKEUP_GPIO && digitalRead(BUTTON_1) != LOW &&
        touchReaderTaskHandle) {
        // The sleeping CPU missed the falling edge; read the touch now.
        xTaskNotifyGive(touchReaderTaskHandle);
    }
    return true;
}
#endif

void loop() {
    // NOTE: GPIO0 is hard-wired to the EPD driver's CFG_STR line on this
    // board and must never be read/driven as a general-purpose button.
    processSerialCommands();
    AppComm::poll();

    uint32_t now = millis();

    bool button1Pressed = false;
#ifndef NATIVE_TESTING
    button1Pressed = checkButton1DebouncedPress(now);
#endif

    // Edge-triggered tap detector: fire once on the first DOWN after the
    // line has been clear for a short debounce window.
    static bool touchDownWaiting = false;
    static bool touchFired = false;
    static uint32_t lastUpTime = 0;
    const uint32_t TAP_DEBOUNCE_MS = 50;

    int tx = -1, ty = -1;
    bool gotTouchEvent = false;

#ifndef NATIVE_TESTING
    TouchEvent ev{};
    while (touchEventQueue && xQueueReceive(touchEventQueue, &ev, 0) == pdTRUE) {
        if (ev.phase == TouchPhase::DOWN) {
            gotTouchEvent = true;
            touchDownWaiting = true;
            tx = ev.x;
            ty = ev.y;
        } else {
            // GT911 also pulses INT for noise/heartbeats; only the release of
            // a press we actually saw counts as activity.
            if (touchDownWaiting) gotTouchEvent = true;
            touchDownWaiting = false;
            touchFired = false;
            lastUpTime = now;
        }
    }
#else
    gotTouchEvent = DisplayHAL::getTouch(tx, ty);
    if (gotTouchEvent) {
        touchDownWaiting = true;
    } else if (touchDownWaiting) {
        touchDownWaiting = false;
        touchFired = false;
        lastUpTime = now;
    }
#endif

    if (gotTouchEvent) {
        PowerManager::noteUserActivity();
        PowerManager::boostCpu();
    }

#ifdef NATIVE_TESTING
    if (isSystemSleeping) {
        if (gotTouchEvent) {
            isSystemSleeping = false;
            touchFired = true;
            PowerManager::noteUserActivity();
            if (Launcher::getInstance().getActiveApp()) {
                Launcher::getInstance().getActiveApp()->draw();
            } else {
                Launcher::getInstance().drawMenu();
            }
        }
        DisplayHAL::handleEvents();
        if (DisplayHAL::windowShouldClose()) exit(0);
        usleep(10000);
        return;
    }
#endif

    if (button1Pressed || PowerManager::shouldAutoSleep()) {
        enterLockMode(button1Pressed ? PowerManager::SLEEP_BUTTON : PowerManager::SLEEP_TIMEOUT); // hardware: never returns
        return;
    }

    uint32_t loopStartTime = millis();

    Launcher::getInstance().loop();

#ifndef NATIVE_TESTING
    bool touched = touchDownWaiting && (static_cast<int32_t>(now - lastUpTime) >= (int32_t)TAP_DEBOUNCE_MS);
    if (touched && !touchFired) {
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
    if (!gotTouchEvent) touchFired = false;
    DisplayHAL::handleEvents();
    if (DisplayHAL::windowShouldClose()) exit(0);
#endif

    // Taps that landed while a slow e-ink refresh was running are stale.
    if ((millis() - loopStartTime) > 200) {
#ifndef NATIVE_TESTING
        TouchEvent dump;
        while (touchEventQueue && xQueueReceive(touchEventQueue, &dump, 0) == pdTRUE) { }
#endif
        touchDownWaiting = false;
        touchFired = false;
    }

    // Idle: block until a touch arrives (or ~30ms passes for button/BLE/
    // serial polling) instead of spinning the CPU at full speed.
    if (!AppComm::hasPending()) {
        PowerManager::idleTick();
#ifndef NATIVE_TESTING
        TouchEvent peek;
        if (sleepUntilInput()) return;
        if (touchEventQueue) xQueuePeek(touchEventQueue, &peek, pdMS_TO_TICKS(30));
        else delay(30);
#else
        usleep(10000);
#endif
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
        DisplayHAL::dumpVisible(dumpPgm);
        printf("Dumped screen PGM to %s\n", dumpPgm);
        return 0;
    }
    while (true) {
        loop();
    }
    return 0;
}
#endif
