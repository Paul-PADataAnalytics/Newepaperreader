#include "SystemSettings.h"
#include "reader/AppStorage.h"
#include <ArduinoJson.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
#else
#include <cstdio>
#endif

namespace {

SystemSettingsData g_settings;
const char* kSettingsPath = "/data/settings.json";

bool readFile(const std::string& path, std::string& out) {
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
#else
    if (!SD.exists(path.c_str())) return false;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    while (f.available()) {
        char buf[256];
        int n = f.read((uint8_t*)buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, n);
    }
    f.close();
    return true;
#endif
}

bool writeFile(const std::string& path, const std::string& data) {
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
#else
    if (!SD.exists("/data")) SD.mkdir("/data");
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    f.write((const uint8_t*)data.data(), data.size());
    f.close();
    return true;
#endif
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

namespace SystemSettings {

SystemSettingsData& get() { return g_settings; }

void load() {
    std::string json;
    if (!readFile(AppStorage::toRuntimePath(kSettingsPath), json)) return;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return;

    SystemSettingsData d;
    d.sleepTimeoutMin = clampInt(doc["sleepTimeoutMin"] | d.sleepTimeoutMin, 0, 240);
    d.alwaysOn = doc["alwaysOn"] | d.alwaysOn;
    d.stayAwakeOnUsb = doc["stayAwakeOnUsb"] | d.stayAwakeOnUsb;
    d.bleEnabled = doc["bleEnabled"] | d.bleEnabled;
    d.idleSleep = doc["idleSleep"] | d.idleSleep;
    d.imageBrightness = clampInt(doc["imageBrightness"] | d.imageBrightness, 0, 3);
    d.darkMode = doc["darkMode"] | d.darkMode;
    float font = doc["readingFontSize"] | d.readingFontSize;
    if (font < 12.0f || font > 60.0f) font = 28.0f;
    d.readingFontSize = font;
    if (doc["sleepScreen"].is<int>()) {
        d.sleepScreen = clampInt(doc["sleepScreen"].as<int>(), SLEEP_SCREEN_COVER, SLEEP_SCREEN_SIMPLE);
    } else if (doc["sleepShowsImages"].is<bool>() && !doc["sleepShowsImages"].as<bool>()) {
        // Older setting: "Book & battery" card.
        d.sleepScreen = SLEEP_SCREEN_SIMPLE;
    }
    d.fullRefreshEvery = clampInt(doc["fullRefreshEvery"] | d.fullRefreshEvery, 1, 50);
    d.tzOffsetMin = clampInt(doc["tzOffsetMin"] | d.tzOffsetMin, -14 * 60, 14 * 60);
    g_settings = d;
}

bool save() {
    JsonDocument doc;
    doc["sleepTimeoutMin"] = g_settings.sleepTimeoutMin;
    doc["alwaysOn"] = g_settings.alwaysOn;
    doc["stayAwakeOnUsb"] = g_settings.stayAwakeOnUsb;
    doc["bleEnabled"] = g_settings.bleEnabled;
    doc["idleSleep"] = g_settings.idleSleep;
    doc["imageBrightness"] = g_settings.imageBrightness;
    doc["darkMode"] = g_settings.darkMode;
    doc["readingFontSize"] = g_settings.readingFontSize;
    doc["sleepScreen"] = g_settings.sleepScreen;
    doc["fullRefreshEvery"] = g_settings.fullRefreshEvery;
    doc["tzOffsetMin"] = g_settings.tzOffsetMin;
    std::string out;
    serializeJsonPretty(doc, out);
    return writeFile(AppStorage::toRuntimePath(kSettingsPath), out);
}

void applyTimezone() {
    // POSIX TZ offsets are inverted: UTC+1 is written "UTC-01:00".
    int off = g_settings.tzOffsetMin;
    char tz[32];
    int a = off < 0 ? -off : off;
    snprintf(tz, sizeof(tz), "UTC%c%02d:%02d", off > 0 ? '-' : '+', a / 60, a % 60);
    setenv("TZ", tz, 1);
    tzset();
}

float imageGamma() {
    // Values < 1 lift mid-tones. E-ink renders the middle of the 16-level
    // ramp noticeably darker than an LCD, so photos look muddy without this.
    static const float kGamma[] = {1.0f, 0.8f, 0.65f, 0.5f};
    return kGamma[clampInt(g_settings.imageBrightness, 0, 3)];
}

const char* imageBrightnessLabel(int level) {
    static const char* kLabels[] = {"Original", "Light", "Lighter", "Lightest"};
    return kLabels[clampInt(level, 0, 3)];
}

} // namespace SystemSettings
