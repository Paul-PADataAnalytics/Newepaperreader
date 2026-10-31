#pragma once

#include <cstring>
#include <vector>
#include <emscripten.h>

typedef unsigned char Uint8;
typedef unsigned int Uint32;

struct SDL_Window {};

struct SDL_Renderer {
    Uint8 red = 255;
    Uint8 green = 255;
    Uint8 blue = 255;
    Uint8 alpha = 255;
};

struct SDL_Texture {
    int width;
    int height;
    std::vector<Uint8> pixels;
};

struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding;
    int x;
    int y;
};

union SDL_Event {
    Uint32 type;
    SDL_MouseButtonEvent button;
};

enum {
    SDL_INIT_VIDEO = 0x00000020,
    SDL_WINDOWPOS_UNDEFINED = 0,
    SDL_WINDOW_SHOWN = 0x00000004,
    SDL_RENDERER_ACCELERATED = 0x00000002,
    SDL_PIXELFORMAT_ARGB8888 = 0x16362004,
    SDL_TEXTUREACCESS_STREAMING = 1,
    SDL_QUIT = 0x00000100,
    SDL_MOUSEBUTTONDOWN = 0x00000401
};

static SDL_Event webSDLPendingEvent = {};
static bool webSDLHasPendingEvent = false;
static SDL_Window webSDLWindow;
static SDL_Renderer webSDLRenderer;
static SDL_Texture webSDLTexture = {};

extern "C" EMSCRIPTEN_KEEPALIVE void webSDL_pushPointer(int x, int y) {
    if (webSDLHasPendingEvent) return;
    webSDLPendingEvent.button.type = SDL_MOUSEBUTTONDOWN;
    webSDLPendingEvent.button.button = 1;
    webSDLPendingEvent.button.state = 1;
    webSDLPendingEvent.button.clicks = 1;
    webSDLPendingEvent.button.x = x;
    webSDLPendingEvent.button.y = y;
    webSDLHasPendingEvent = true;
}

inline int SDL_Init(Uint32) {
    EM_ASM({
        const canvas = Module.canvas;
        if (canvas.dataset.webSdlInputInstalled) return;
        canvas.dataset.webSdlInputInstalled = "true";
        canvas.addEventListener("pointerdown", (event) => {
            if (!event.isPrimary || (event.pointerType === "mouse" && event.button !== 0)) return;
            event.preventDefault();
            const rect = canvas.getBoundingClientRect();
            const x = Math.floor((event.clientX - rect.left) * canvas.width / rect.width);
            const y = Math.floor((event.clientY - rect.top) * canvas.height / rect.height);
            Module._webSDL_pushPointer(x, y);
            canvas.focus();
        });
    });
    return 0;
}

inline const char* SDL_GetError() {
    return "WebAssembly canvas mock";
}

inline SDL_Window* SDL_CreateWindow(const char*, int, int, int width, int height, Uint32) {
    EM_ASM({
        Module.canvas.width = $0;
        Module.canvas.height = $1;
    }, width, height);
    webSDLTexture.width = width;
    webSDLTexture.height = height;
    webSDLTexture.pixels.resize(static_cast<size_t>(width) * height * 4);
    return &webSDLWindow;
}

inline SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, Uint32) {
    return &webSDLRenderer;
}

inline SDL_Texture* SDL_CreateTexture(SDL_Renderer*, Uint32, int, int width, int height) {
    webSDLTexture.width = width;
    webSDLTexture.height = height;
    webSDLTexture.pixels.resize(static_cast<size_t>(width) * height * 4);
    return &webSDLTexture;
}

inline int SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 red, Uint8 green,
                                  Uint8 blue, Uint8 alpha) {
    renderer->red = red;
    renderer->green = green;
    renderer->blue = blue;
    renderer->alpha = alpha;
    return 0;
}

inline int SDL_RenderClear(SDL_Renderer* renderer) {
    EM_ASM({
        const canvas = Module.canvas;
        const context = canvas.getContext("2d");
        context.fillStyle = `rgba($0, $1, $2, ${$3 / 255})`;
        context.fillRect(0, 0, canvas.width, canvas.height);
    }, renderer->red, renderer->green, renderer->blue, renderer->alpha);
    return 0;
}

inline int SDL_UpdateTexture(SDL_Texture* texture, const void*, const void* pixels, int pitch) {
    const Uint8* source = static_cast<const Uint8*>(pixels);
    const size_t rowBytes = static_cast<size_t>(texture->width) * 4;
    for (int row = 0; row < texture->height; ++row) {
        std::memcpy(&texture->pixels[static_cast<size_t>(row) * rowBytes],
                    source + static_cast<size_t>(row) * pitch, rowBytes);
    }
    return 0;
}

inline int SDL_RenderCopy(SDL_Renderer*, SDL_Texture* texture, const void*, const void*) {
    EM_ASM({
        const width = $1;
        const height = $2;
        const bytes = width * height * 4;
        const pixels = new Uint8ClampedArray(HEAPU8.subarray($0, $0 + bytes));
        Module.canvas.getContext("2d").putImageData(new ImageData(pixels, width, height), 0, 0);
    }, texture->pixels.data(), texture->width, texture->height);
    return 0;
}

inline void SDL_RenderPresent(SDL_Renderer*) {}

inline int SDL_PollEvent(SDL_Event* event) {
    if (!webSDLHasPendingEvent) return 0;
    *event = webSDLPendingEvent;
    webSDLHasPendingEvent = false;
    return 1;
}
