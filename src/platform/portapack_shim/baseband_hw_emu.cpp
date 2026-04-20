// EmuHem baseband hardware stubs
// Provides no-op implementations for SGPIO, RSSI, and I2S, plus an
// SDL3-backed audio DMA sink, that the baseband processing pipeline references.

#include "baseband_sgpio.hpp"
#include "rssi.hpp"
#include "rssi_dma.hpp"
#include "audio_dma.hpp"
#include "ch.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

// ============================================================================
// SGPIO -- Serial GPIO RF interface (no-op in emulator)
// ============================================================================

namespace baseband {

void SGPIO::init() {}
void SGPIO::configure(const Direction /*direction*/) {}
// streaming_enable/disable/is_enabled are inline in the header
// and operate on LPC_SGPIO stub registers (harmless no-ops)

}  // namespace baseband

// ============================================================================
// RSSI -- RF signal strength measurement
// ============================================================================

namespace rf {
namespace rssi {

void init() {}
void start() {}
void stop() {}

namespace dma {

static std::array<rf::rssi::sample_t, 400> g_rssi_buffer{};
static bool g_rssi_enabled = false;

void init() {}

void allocate(size_t /*buffer_count*/, size_t /*items_per_buffer*/) {}

void free() {}

void enable() {
    g_rssi_enabled = true;
}

bool is_enabled() {
    return g_rssi_enabled;
}

void disable() {
    g_rssi_enabled = false;
}

rf::rssi::buffer_t wait_for_buffer() {
    // Sleep to avoid spinning, check for thread termination
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (chThdShouldTerminate()) {
        return {nullptr, 0};
    }

    // Return a buffer of zeros (no real RF signal strength)
    std::memset(g_rssi_buffer.data(), 0, g_rssi_buffer.size());
    return {g_rssi_buffer.data(), g_rssi_buffer.size()};
}

}  // namespace dma
}  // namespace rssi
}  // namespace rf

// ============================================================================
// Audio DMA -- routes firmware audio to an SDL3 playback stream.
//
// The firmware pipeline (AudioOutput::fill_audio_buffer) calls
//   tx_empty_buffer()        -> writes samples -> next tx_empty_buffer()
// so we push the previous buffer contents to SDL on each call. The returned
// buffer is reused in place; callers fill it completely before asking again.
// ============================================================================

namespace audio {
namespace dma {

// Firmware's transfer_samples is 32; give each of the staging slots room for
// that plus some headroom. Two slots so we can double-buffer cheaply.
static constexpr size_t kSamplesPerBuffer = 32;
static constexpr size_t kTxSlots = 2;
static audio::sample_t g_tx_buffers[kTxSlots][kSamplesPerBuffer]{};
static audio::sample_t g_rx_buffer[kSamplesPerBuffer]{};
static size_t g_tx_slot = 0;

static SDL_AudioStream* g_audio_stream = nullptr;
static std::atomic<bool> g_audio_inited{false};
static bool g_has_pending_tx = false;

void init_audio_in() {}

void init_audio_out() {
    if (g_audio_inited.load()) return;

    // EMUHEM_NO_AUDIO_OUT=1 disables SDL audio output (samples silently drop).
    // Used by integration tests to avoid back-to-back CoreAudio device
    // contention when multiple emuhem processes run in sequence — the
    // default playback device handle doesn't always release fast enough.
    if (const char* no_audio = std::getenv("EMUHEM_NO_AUDIO_OUT");
        no_audio && std::string_view{no_audio} == "1") {
        std::fprintf(stderr,
            "[EmuHem] audio::dma::init_audio_out: SDL audio disabled by EMUHEM_NO_AUDIO_OUT\n");
        g_audio_inited.store(true);  // pretend we're up so write() early-returns
        return;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq = 48000;

    g_audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

    if (!g_audio_stream) {
        std::fprintf(stderr,
            "[EmuHem] audio::dma::init_audio_out: SDL_OpenAudioDeviceStream failed: %s\n",
            SDL_GetError());
        return;
    }

    SDL_ResumeAudioStreamDevice(g_audio_stream);
    g_audio_inited.store(true);
    std::fprintf(stderr,
        "[EmuHem] audio::dma::init_audio_out: SDL audio stream opened (48kHz stereo s16)\n");
}

void disable() {
    if (!g_audio_inited.load()) return;
    if (g_audio_stream) {
        SDL_DestroyAudioStream(g_audio_stream);
        g_audio_stream = nullptr;
    }
    g_audio_inited.store(false);
    g_has_pending_tx = false;
    std::fprintf(stderr, "[EmuHem] audio::dma::disable: SDL audio stream closed\n");
}

void shrink_tx_buffer(bool /*shrink*/) {}

void beep_start(uint32_t /*freq*/, uint32_t /*sample_rate*/, uint32_t /*beep_duration_ms*/) {}
void beep_stop() {}

audio::buffer_t tx_empty_buffer() {
    // Push the previously filled slot to SDL before handing out a fresh one.
    if (g_audio_stream && g_has_pending_tx) {
        const size_t prev = g_tx_slot;
        SDL_PutAudioStreamData(g_audio_stream, g_tx_buffers[prev],
                               sizeof(g_tx_buffers[prev]));
    }
    g_tx_slot = (g_tx_slot + 1) % kTxSlots;
    auto* slot = g_tx_buffers[g_tx_slot];
    std::memset(slot, 0, sizeof(g_tx_buffers[g_tx_slot]));
    g_has_pending_tx = true;
    return {slot, kSamplesPerBuffer};
}

audio::buffer_t rx_empty_buffer() {
    std::memset(g_rx_buffer, 0, sizeof(g_rx_buffer));
    return {g_rx_buffer, kSamplesPerBuffer};
}

}  // namespace dma
}  // namespace audio
