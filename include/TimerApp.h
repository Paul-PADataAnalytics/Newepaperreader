#pragma once

#include "Application.h"
#include <stdint.h>

enum class TimerMode {
    CLOCK,
    STOPWATCH,
    COUNTDOWN
};

enum class StopwatchState {
    STOPPED,
    RUNNING,
    PAUSED
};

enum class CountdownState {
    STOPPED,
    RUNNING,
    PAUSED
};

class TimerApp : public Application {
private:
    TimerMode activeMode = TimerMode::CLOCK;

    // Clock state
    int lastClockSec = -1;

    // Stopwatch state
    StopwatchState stopwatchState = StopwatchState::STOPPED;
    uint32_t stopwatchStartMs = 0;
    uint32_t stopwatchElapsedMs = 0;
    int lastStopwatchSec = -1;

    // Countdown state
    CountdownState countdownState = CountdownState::STOPPED;
    int countdownHours = 0;
    int countdownMinutes = 0;
    int countdownSeconds = 0;
    int countdownRemainingSecs = 0;
    uint32_t countdownStartMs = 0;
    uint32_t countdownElapsedMsBeforePause = 0;
    int lastCountdownSec = -1;

    // Helper functions
    void drawButtonWithText(int x, int y, int w, int h, const char* label, bool active = false);
    void drawModeSelector();
    void drawClockMode();
    void drawStopwatchMode();
    void drawCountdownMode();
    void drawFast();
    uint32_t get_current_ms() const;

public:
    TimerApp() = default;
    virtual ~TimerApp() override = default;

    virtual void onCreate() override;
    virtual void onDestroy() override;
    virtual void update() override;
    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
};
