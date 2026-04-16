// EmuHem -- PortaPack Mayhem Firmware Emulator
// Entry point for macOS/Windows desktop emulation.

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>

// ChibiOS shim
#include "ch.h"
#include "hal.h"

static constexpr int LCD_WIDTH = 240;
static constexpr int LCD_HEIGHT = 320;
static constexpr int SCALE = 2;

int main(int /*argc*/, char* /*argv*/[]) {
    std::fprintf(stdout, "[EmuHem] Starting PortaPack Mayhem Emulator...\n");

    // Initialize ChibiOS shim
    chSysInit();

    // Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[EmuHem] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window
    SDL_Window* window = SDL_CreateWindow(
        "EmuHem - PortaPack Mayhem Emulator",
        LCD_WIDTH * SCALE,
        LCD_HEIGHT * SCALE,
        0);
    if (!window) {
        std::fprintf(stderr, "[EmuHem] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::fprintf(stderr, "[EmuHem] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create texture for LCD framebuffer
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        LCD_WIDTH, LCD_HEIGHT);
    if (!texture) {
        std::fprintf(stderr, "[EmuHem] SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    std::fprintf(stdout, "[EmuHem] Window created (%dx%d @ %dx scale)\n",
                 LCD_WIDTH, LCD_HEIGHT, SCALE);
    std::fprintf(stdout, "[EmuHem] Phase 1: Black screen. Press ESC or close window to exit.\n");

    // Main event loop
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
                default:
                    break;
            }
        }

        // Clear and render
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Blit the LCD texture (currently black)
        SDL_FRect dest = {0.0f, 0.0f,
                          static_cast<float>(LCD_WIDTH * SCALE),
                          static_cast<float>(LCD_HEIGHT * SCALE)};
        SDL_RenderTexture(renderer, texture, nullptr, &dest);

        SDL_RenderPresent(renderer);

        // ~60fps
        SDL_Delay(16);
    }

    std::fprintf(stdout, "[EmuHem] Shutting down...\n");

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
