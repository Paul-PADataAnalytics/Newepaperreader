#pragma once

#ifndef NATIVE_TESTING

void startWiFiSync();
void stopWiFiSync();
void processWiFiSync(); // If needed for yielding or looping

#else

// Mock for native testing
inline void startWiFiSync() {}
inline void stopWiFiSync() {}
inline void processWiFiSync() {}

#endif
