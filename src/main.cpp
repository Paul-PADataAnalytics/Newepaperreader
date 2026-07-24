#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
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
        printf("Failed to load Roboto-Regular.ttf\n");
    }
#endif

    // Initialize App Launcher
    Launcher::getInstance().init();
    
    // Start BLE background system service continuously on system startup
    AppComm::init();

    Launcher::getInstance().drawMenu();
}

static uint32_t lastTouchActivityTime = 0;
static bool isSystemSleeping = false;

static void drawSleepScreen() {
    bool wasPortrait = DisplayHAL::isPortrait();
    DisplayHAL::setPortrait(false); // Sleep screen is rendered landscape

    std::vector<std::string> rawImages;

#ifdef NATIVE_TESTING
    DIR* d = opendir("data/images");
    if (!d) d = opendir("images");
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.length() > 4 && name.substr(name.length() - 4) == ".raw") {
                rawImages.push_back("data/images/" + name);
            }
        }
        closedir(d);
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

void loop() {
    // BOOT Button (GPIO 0) manual sleep / wake toggle
#ifndef NATIVE_TESTING
    static uint32_t lastBootBtnTime = 0;
    if (digitalRead(GPIO_NUM_0) == LOW) {
        uint32_t btnNow = millis();
        if ((btnNow - lastBootBtnTime) > 400) {
            lastBootBtnTime = btnNow;
            isSystemSleeping = !isSystemSleeping;
            if (isSystemSleeping) {
                drawSleepScreen();
            } else {
                DisplayHAL::powerOn();
                if (Launcher::getInstance().getActiveApp()) {
                    Launcher::getInstance().getActiveApp()->draw();
                } else {
                    Launcher::getInstance().drawMenu();
                }
            }
            return;
        }
    }
#endif
    processSerialCommands();

    uint32_t now = millis();
    if (lastTouchActivityTime == 0) {
        lastTouchActivityTime = now;
    }

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
        gotTouchEvent = true;
        if (ev.phase == TouchPhase::DOWN) {
            touchDownWaiting = true;
            tx = ev.x;
            ty = ev.y;
        } else {
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
        if (isSystemSleeping) {
            // Wake up from sleep on touch!
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
        lastTouchActivityTime = now;
    }

    if (isSystemSleeping) {
#ifndef NATIVE_TESTING
        gpio_wakeup_enable(static_cast<gpio_num_t>(TOUCH_INT), GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_light_sleep_start();
        delay(10);
#else
        DisplayHAL::handleEvents();
        if (DisplayHAL::windowShouldClose()) exit(0);
        usleep(100000);
#endif
        return;
    }

    if ((now - lastTouchActivityTime) >= 30000) {
        isSystemSleeping = true;
        drawSleepScreen();
        DisplayHAL::powerOff();
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
