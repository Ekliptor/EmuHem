// EmuHem -- PortaPack Mayhem Firmware Emulator
// Entry point: runs firmware EventDispatcher in a thread, SDL render loop on main thread.

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>

#include "ch.h"
#include "portapack.hpp"
#include "portapack_persistent_memory.hpp"
#include "event_m0.hpp"
#include "irq_lcd_frame.hpp"
#include "irq_rtc.hpp"
#include "theme.hpp"
#include "ui_navigation.hpp"
#include "i2cdevmanager.hpp"

// Declared in phase2_stubs.cpp
extern ui::SystemView* system_view_ptr;


// Input control functions (from irq_controls_emu.cpp)
extern void emu_set_switches(uint8_t mask);
extern void emu_adjust_encoder(int delta);

static constexpr int LCD_WIDTH = 240;
static constexpr int LCD_HEIGHT = 320;
static constexpr int SCALE = 2;

static std::atomic<bool> g_firmware_running{false};
static std::atomic<bool> g_quit_requested{false};

// Timer thread: signals EVT_MASK_LCD_FRAME_SYNC at ~60Hz
static void frame_sync_thread_fn() {
    while (!g_quit_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (g_firmware_running.load()) {
            EventDispatcher::events_flag(EVT_MASK_LCD_FRAME_SYNC);
        }
    }
}

// Timer thread: signals EVT_MASK_RTC_TICK at 1Hz
static void rtc_tick_thread_fn() {
    while (!g_quit_requested.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_firmware_running.load()) {
            EventDispatcher::events_flag(EVT_MASK_RTC_TICK);
        }
    }
}

// Firmware thread: runs the full firmware init + event loop
static void firmware_thread_fn() {
    // Register this thread with the ChibiOS shim
    chSysInit();

    std::fprintf(stdout, "[EmuHem] Firmware thread started\n");

    // Initialize portapack (our stub version)
    auto status = portapack::init();
    if (status != portapack::init_status_t::INIT_SUCCESS) {
        std::fprintf(stderr, "[EmuHem] portapack::init() failed\n");
        return;
    }

    // LCD frame sync and RTC -- these store chThdSelf() for event signaling
    lcd_frame_sync_configure();
    rtc_interrupt_enable();

    // Set theme
    Theme::SetTheme(static_cast<Theme::ThemeId>(
        portapack::persistent_memory::ui_theme_id()));

    // Create the firmware UI
    static ui::Context context;
    static ui::SystemView system_view{
        context,
        portapack::display.screen_rect()};
    system_view_ptr = &system_view;

    // Create event dispatcher
    EventDispatcher event_dispatcher{&system_view, context};
    portapack::setEventDispatcherToUSBSerial(&event_dispatcher);

    g_firmware_running.store(true);

    std::fprintf(stdout, "[EmuHem] Entering firmware event loop\n");

    // Run the firmware's main event loop
    // This blocks until EventDispatcher::request_stop() is called
    event_dispatcher.run();

    g_firmware_running.store(false);
    std::fprintf(stdout, "[EmuHem] Firmware thread exiting\n");
}

// Translate SDL key events to PortaPack switch bits
static uint8_t sdl_key_to_switch_mask(SDL_Keycode key, bool pressed) {
    static uint8_t current_mask = 0;
    uint8_t bit = 0;

    switch (key) {
        case SDLK_RIGHT:  bit = (1 << 0); break; // Switch::Right
        case SDLK_LEFT:   bit = (1 << 1); break; // Switch::Left
        case SDLK_DOWN:   bit = (1 << 2); break; // Switch::Down
        case SDLK_UP:     bit = (1 << 3); break; // Switch::Up
        case SDLK_RETURN: bit = (1 << 4); break; // Switch::Sel
        case SDLK_BACKSPACE: bit = (1 << 5); break; // Switch::Dfu
        case SDLK_ESCAPE:
            // Escape = Back = Left + Up
            if (pressed)
                current_mask |= (1 << 1) | (1 << 3);
            else
                current_mask &= ~((1 << 1) | (1 << 3));
            return current_mask;
        default: return current_mask;
    }

    if (pressed)
        current_mask |= bit;
    else
        current_mask &= ~bit;

    return current_mask;
}

int main(int /*argc*/, char* /*argv*/[]) {
    std::fprintf(stdout, "[EmuHem] Starting PortaPack Mayhem Emulator...\n");

    // Initialize SDL3 (must be on main thread for macOS)
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[EmuHem] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "EmuHem - PortaPack Mayhem Emulator",
        LCD_WIDTH * SCALE, LCD_HEIGHT * SCALE, 0);
    if (!window) {
        std::fprintf(stderr, "[EmuHem] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::fprintf(stderr, "[EmuHem] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        LCD_WIDTH, LCD_HEIGHT);
    if (!texture) {
        std::fprintf(stderr, "[EmuHem] SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    std::fprintf(stdout, "[EmuHem] SDL window created (%dx%d @ %dx)\n",
                 LCD_WIDTH, LCD_HEIGHT, SCALE);

    // Start timer threads
    std::thread frame_sync_thread(frame_sync_thread_fn);
    std::thread rtc_tick_thread(rtc_tick_thread_fn);

    // Start firmware thread
    std::thread firmware_thread(firmware_thread_fn);

    // Give firmware time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::fprintf(stdout, "[EmuHem] Entering SDL render loop\n");

    // Main SDL event + render loop (must be on main thread for macOS)
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_KEY_DOWN: {
                    uint8_t mask = sdl_key_to_switch_mask(event.key.key, true);
                    emu_set_switches(mask);
                    if (g_firmware_running.load()) {
                        EventDispatcher::events_flag(EVT_MASK_SWITCHES);
                    }
                    break;
                }

                case SDL_EVENT_KEY_UP: {
                    uint8_t mask = sdl_key_to_switch_mask(event.key.key, false);
                    emu_set_switches(mask);
                    if (g_firmware_running.load()) {
                        EventDispatcher::events_flag(EVT_MASK_SWITCHES);
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_WHEEL: {
                    int delta = static_cast<int>(event.wheel.y);
                    emu_adjust_encoder(delta);
                    if (g_firmware_running.load()) {
                        EventDispatcher::events_flag(EVT_MASK_ENCODER);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Copy framebuffer to SDL texture
        if (portapack::IO::is_dirty()) {
            void* pixels;
            int pitch;
            if (SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
                const uint16_t* fb = portapack::IO::get_framebuffer();
                for (int y = 0; y < LCD_HEIGHT; y++) {
                    std::memcpy(
                        static_cast<uint8_t*>(pixels) + y * pitch,
                        fb + y * LCD_WIDTH,
                        LCD_WIDTH * sizeof(uint16_t));
                }
                SDL_UnlockTexture(texture);
            }
            portapack::IO::clear_dirty();
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_FRect dest = {0.0f, 0.0f,
                          static_cast<float>(LCD_WIDTH * SCALE),
                          static_cast<float>(LCD_HEIGHT * SCALE)};
        SDL_RenderTexture(renderer, texture, nullptr, &dest);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    // Shutdown
    std::fprintf(stdout, "[EmuHem] Shutting down...\n");

    g_quit_requested.store(true);

    if (g_firmware_running.load()) {
        EventDispatcher::request_stop();
        // Signal the event loop to wake up and check is_running
        EventDispatcher::events_flag(EVT_MASK_LCD_FRAME_SYNC);
    }

    firmware_thread.join();
    frame_sync_thread.join();
    rtc_tick_thread.join();

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::fprintf(stdout, "[EmuHem] Done.\n");
    return 0;
}
