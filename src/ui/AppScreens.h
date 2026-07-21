#pragma once

#include <stdint.h>

namespace AppScreens {

// Shared loading scaffold for transitions between views.
void drawLoading(uint8_t* framebuffer, const char* title);

} // namespace AppScreens
