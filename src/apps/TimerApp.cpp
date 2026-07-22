#include "TimerApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include <stdio.h>
#include <time.h>
#include <string>
#include <cstring>

#ifdef NATIVE_TESTING
#include <chrono>
#else
#include <Arduino.h>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

void TimerApp::onCreate() {
    activeMode = TimerMode::CLOCK;
    lastClockSec = -1;

    stopwatchState = StopwatchState::STOPPED;
    stopwatchStartMs = 0;
    stopwatchElapsedMs = 0;
    lastStopwatchSec = -1;

    countdownState = CountdownState::STOPPED;
    countdownHours = 0;
    countdownMinutes = 0;
    countdownSeconds = 0;
    countdownRemainingSecs = 0;
    countdownStartMs = 0;
    countdownElapsedMsBeforePause = 0;
    lastCountdownSec = -1;
    
    draw();
}

void TimerApp::onDestroy() {
    DisplayHAL::clear();
    memset(framebuffer, 0xFF, DisplayHAL::getWidth() * DisplayHAL::getHeight() / 2);
}

uint32_t TimerApp::get_current_ms() const {
#ifndef NATIVE_TESTING
    return millis();
#else
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
#endif
}

void TimerApp::update() {
    if (activeMode == TimerMode::CLOCK) {
        time_t rawtime;
        time(&rawtime);
        struct tm* timeinfo = localtime(&rawtime);
        int currentSec = timeinfo ? timeinfo->tm_sec : 0;
        if (currentSec != lastClockSec) {
            lastClockSec = currentSec;
            draw();
        }
    } else if (activeMode == TimerMode::STOPWATCH) {
        if (stopwatchState == StopwatchState::RUNNING) {
            uint32_t currentElapsedMs = get_current_ms() - stopwatchStartMs;
            int currentSec = currentElapsedMs / 1000;
            if (currentSec != lastStopwatchSec) {
                lastStopwatchSec = currentSec;
                draw();
            }
        }
    } else if (activeMode == TimerMode::COUNTDOWN) {
        if (countdownState == CountdownState::RUNNING) {
            uint32_t segmentElapsedMs = get_current_ms() - countdownStartMs;
            uint32_t totalElapsedMs = countdownElapsedMsBeforePause + segmentElapsedMs;
            int elapsedSecs = totalElapsedMs / 1000;
            int targetSecs = countdownHours * 3600 + countdownMinutes * 60 + countdownSeconds;
            int remaining = targetSecs - elapsedSecs;
            if (remaining <= 0) {
                remaining = 0;
                countdownState = CountdownState::STOPPED;
                countdownElapsedMsBeforePause = 0;
                countdownRemainingSecs = 0;
                lastCountdownSec = 0; // Trigger draw and show TIME'S UP!
                draw();
            } else if (remaining != lastCountdownSec) {
                lastCountdownSec = remaining;
                draw();
            }
        }
    }
}

void TimerApp::draw() {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    static int tickCount = 0;
    tickCount++;
    // Periodically (every 30 ticks) perform a full E-Ink clear to prevent ghosting/burn-in
    if (tickCount >= 30) {
        tickCount = 0;
        DisplayHAL::clear();
    }

    // Clear the entire framebuffer
    UIFramework::clearArea(framebuffer, 0, 0, w, h);
    
    // Draw mode selector tabs
    drawModeSelector();
    
    // Draw content of the active mode
    if (activeMode == TimerMode::CLOCK) {
        drawClockMode();
    } else if (activeMode == TimerMode::STOPWATCH) {
        drawStopwatchMode();
    } else if (activeMode == TimerMode::COUNTDOWN) {
        drawCountdownMode();
    }
    
    // Refresh display
    DisplayHAL::display(framebuffer);
}

void TimerApp::drawButtonWithText(int x, int y, int w, int h, const char* label, bool active) {
    uint8_t bgColor = active ? 0x99 : 0xFF; // Active is dark gray, inactive is white
    uint8_t fgColor = 0x00;                 // Black border
    
    // Draw background
    DisplayHAL::fillRect(x, y, w, h, bgColor, framebuffer);
    // Draw border
    DisplayHAL::drawRect(x, y, w, h, fgColor, framebuffer);
    
    // Draw text centered
    typography.setFontSize(28.0f);
    int textW = typography.measureText(label);
    int tx = x + (w - textW) / 2;
    int ty = y + (h - 22) / 2;
    
    typography.renderText(label, tx, ty, framebuffer);
}

void TimerApp::drawModeSelector() {
    drawButtonWithText(70, 15, 190, 60, "Clock", activeMode == TimerMode::CLOCK);
    drawButtonWithText(280, 15, 190, 60, "Stopwatch", activeMode == TimerMode::STOPWATCH);
    drawButtonWithText(490, 15, 190, 60, "Countdown", activeMode == TimerMode::COUNTDOWN);
    drawButtonWithText(700, 15, 190, 60, "Exit", false);
}

void TimerApp::drawClockMode() {
    int w = DisplayHAL::getWidth();
    typography.setFontSize(80.0f);
    
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    
    char timeStr[32];
    if (timeinfo) {
        sprintf(timeStr, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        sprintf(timeStr, "00:00:00");
    }
    
    int textW = typography.measureText(timeStr);
    int tx = (w - textW) / 2;
    int ty = 220;
    
    typography.renderText(timeStr, tx, ty, framebuffer);
    
    // Draw date below
    typography.setFontSize(32.0f);
    char dateStr[64];
    if (timeinfo) {
        strftime(dateStr, sizeof(dateStr), "%A, %B %d, %Y", timeinfo);
    } else {
        sprintf(dateStr, "Date Unavailable");
    }
    
    int dateW = typography.measureText(dateStr);
    int dx = (w - dateW) / 2;
    int dy = 340;
    
    typography.renderText(dateStr, dx, dy, framebuffer);
}

void TimerApp::drawStopwatchMode() {
    int w = DisplayHAL::getWidth();
    uint32_t elapsedMs = stopwatchElapsedMs;
    if (stopwatchState == StopwatchState::RUNNING) {
        elapsedMs = get_current_ms() - stopwatchStartMs;
    }
    
    uint32_t hours = elapsedMs / 3600000;
    uint32_t mins = (elapsedMs % 3600000) / 60000;
    uint32_t secs = (elapsedMs % 60000) / 1000;
    
    typography.setFontSize(80.0f);
    char timeStr[32];
    sprintf(timeStr, "%02d:%02d:%02d", (int)hours, (int)mins, (int)secs);
    
    int textW = typography.measureText(timeStr);
    int tx = (w - textW) / 2;
    int ty = 220;
    
    typography.renderText(timeStr, tx, ty, framebuffer);
    
    // Bottom buttons
    const char* startPauseLabel = (stopwatchState == StopwatchState::RUNNING) ? "Pause" : "Start";
    drawButtonWithText(260, 380, 200, 60, startPauseLabel, false);
    drawButtonWithText(500, 380, 200, 60, "Reset", false);
}

void TimerApp::drawCountdownMode() {
    int w = DisplayHAL::getWidth();
    int displayHours = countdownHours;
    int displayMinutes = countdownMinutes;
    int displaySeconds = countdownSeconds;
    
    if (countdownState == CountdownState::RUNNING) {
        uint32_t segmentElapsedMs = get_current_ms() - countdownStartMs;
        uint32_t totalElapsedMs = countdownElapsedMsBeforePause + segmentElapsedMs;
        int elapsedSecs = totalElapsedMs / 1000;
        int targetSecs = countdownHours * 3600 + countdownMinutes * 60 + countdownSeconds;
        int remaining = targetSecs - elapsedSecs;
        if (remaining < 0) remaining = 0;
        
        displayHours = remaining / 3600;
        displayMinutes = (remaining % 3600) / 60;
        displaySeconds = remaining % 60;
    } else if (countdownState == CountdownState::PAUSED) {
        int elapsedSecs = countdownElapsedMsBeforePause / 1000;
        int targetSecs = countdownHours * 3600 + countdownMinutes * 60 + countdownSeconds;
        int remaining = targetSecs - elapsedSecs;
        if (remaining < 0) remaining = 0;
        
        displayHours = remaining / 3600;
        displayMinutes = (remaining % 3600) / 60;
        displaySeconds = remaining % 60;
    }
    
    char timeStr[32];
    sprintf(timeStr, "%02d:%02d:%02d", displayHours, displayMinutes, displaySeconds);
    
    typography.setFontSize(80.0f);
    int textW = typography.measureText(timeStr);
    int tx = (w - textW) / 2;
    int ty = 220;
    typography.renderText(timeStr, tx, ty, framebuffer);
    
    if (countdownState == CountdownState::STOPPED) {
        // Draw + and - adjust buttons
        drawButtonWithText(200, 140, 80, 50, "+", false);
        drawButtonWithText(200, 300, 80, 50, "-", false);
        
        drawButtonWithText(440, 140, 80, 50, "+", false);
        drawButtonWithText(440, 300, 80, 50, "-", false);
        
        drawButtonWithText(680, 140, 80, 50, "+", false);
        drawButtonWithText(680, 300, 80, 50, "-", false);
        
        typography.setFontSize(20.0f);
        int lblH = typography.measureText("Hours");
        typography.renderText("Hours", 240 - lblH / 2, 105, framebuffer);
        
        int lblM = typography.measureText("Minutes");
        typography.renderText("Minutes", 480 - lblM / 2, 105, framebuffer);
        
        int lblS = typography.measureText("Seconds");
        typography.renderText("Seconds", 720 - lblS / 2, 105, framebuffer);
    }
    
    // Check if countdown completed (time's up feedback)
    if (countdownState == CountdownState::STOPPED && countdownHours == 0 && countdownMinutes == 0 && countdownSeconds == 0 && lastCountdownSec == 0) {
        typography.setFontSize(32.0f);
        int infoW = typography.measureText("TIME'S UP!");
        typography.renderText("TIME'S UP!", (w - infoW) / 2, 110, framebuffer);
    }
    
    const char* startPauseLabel = (countdownState == CountdownState::RUNNING) ? "Pause" : "Start";
    drawButtonWithText(260, 380, 200, 60, startPauseLabel, false);
    
    const char* resetLabel = (countdownState == CountdownState::STOPPED) ? "Clear" : "Reset";
    drawButtonWithText(500, 380, 200, 60, resetLabel, false);
}

void TimerApp::handleTouch(int x, int y) {
    if (y >= 15 && y <= 75) {
        if (x >= 70 && x <= 260) {
            if (activeMode != TimerMode::CLOCK) {
                activeMode = TimerMode::CLOCK;
                lastClockSec = -1;
                draw();
            }
            return;
        } else if (x >= 280 && x <= 470) {
            if (activeMode != TimerMode::STOPWATCH) {
                activeMode = TimerMode::STOPWATCH;
                lastStopwatchSec = -1;
                draw();
            }
            return;
        } else if (x >= 490 && x <= 680) {
            if (activeMode != TimerMode::COUNTDOWN) {
                activeMode = TimerMode::COUNTDOWN;
                lastCountdownSec = -1;
                draw();
            }
            return;
        } else if (x >= 700 && x <= 890) {
            onDestroy();
            extern void exitToSystemLauncher();
            exitToSystemLauncher();
            return;
        }
    }

    if (activeMode == TimerMode::STOPWATCH) {
        if (x >= 260 && x <= 460 && y >= 380 && y <= 440) {
            if (stopwatchState == StopwatchState::RUNNING) {
                stopwatchState = StopwatchState::PAUSED;
                stopwatchElapsedMs = get_current_ms() - stopwatchStartMs;
            } else {
                if (stopwatchState == StopwatchState::STOPPED) {
                    stopwatchStartMs = get_current_ms();
                    stopwatchElapsedMs = 0;
                } else if (stopwatchState == StopwatchState::PAUSED) {
                    stopwatchStartMs = get_current_ms() - stopwatchElapsedMs;
                }
                stopwatchState = StopwatchState::RUNNING;
            }
            lastStopwatchSec = -1;
            draw();
        } else if (x >= 500 && x <= 700 && y >= 380 && y <= 440) {
            stopwatchState = StopwatchState::STOPPED;
            stopwatchStartMs = 0;
            stopwatchElapsedMs = 0;
            lastStopwatchSec = -1;
            draw();
        }
    } else if (activeMode == TimerMode::COUNTDOWN) {
        if (countdownState == CountdownState::STOPPED) {
            if (x >= 200 && x <= 280 && y >= 140 && y <= 190) {
                countdownHours = (countdownHours + 1) % 100;
                lastCountdownSec = -1;
                draw();
            } else if (x >= 200 && x <= 280 && y >= 300 && y <= 350) {
                countdownHours = (countdownHours + 99) % 100;
                lastCountdownSec = -1;
                draw();
            } else if (x >= 440 && x <= 520 && y >= 140 && y <= 190) {
                countdownMinutes = (countdownMinutes + 1) % 60;
                lastCountdownSec = -1;
                draw();
            } else if (x >= 440 && x <= 520 && y >= 300 && y <= 350) {
                countdownMinutes = (countdownMinutes + 59) % 60;
                lastCountdownSec = -1;
                draw();
            } else if (x >= 680 && x <= 760 && y >= 140 && y <= 190) {
                countdownSeconds = (countdownSeconds + 1) % 60;
                lastCountdownSec = -1;
                draw();
            } else if (x >= 680 && x <= 760 && y >= 300 && y <= 350) {
                countdownSeconds = (countdownSeconds + 59) % 60;
                lastCountdownSec = -1;
                draw();
            }
        }

        if (x >= 260 && x <= 460 && y >= 380 && y <= 440) {
            if (countdownState == CountdownState::RUNNING) {
                countdownState = CountdownState::PAUSED;
                countdownElapsedMsBeforePause += (get_current_ms() - countdownStartMs);
            } else {
                int totalSecs = countdownHours * 3600 + countdownMinutes * 60 + countdownSeconds;
                if (totalSecs > 0) {
                    if (countdownState == CountdownState::STOPPED) {
                        countdownStartMs = get_current_ms();
                        countdownElapsedMsBeforePause = 0;
                        countdownRemainingSecs = totalSecs;
                    } else if (countdownState == CountdownState::PAUSED) {
                        countdownStartMs = get_current_ms();
                    }
                    countdownState = CountdownState::RUNNING;
                }
            }
            lastCountdownSec = -1;
            draw();
        } else if (x >= 500 && x <= 700 && y >= 380 && y <= 440) {
            if (countdownState == CountdownState::STOPPED) {
                countdownHours = 0;
                countdownMinutes = 0;
                countdownSeconds = 0;
            } else {
                countdownState = CountdownState::STOPPED;
                countdownElapsedMsBeforePause = 0;
            }
            lastCountdownSec = -1;
            draw();
        }
    }
}
