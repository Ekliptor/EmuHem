// EmuHem virtual baseband DMA.
// Replaces firmware/common/baseband_dma.cpp. Samples are supplied by a
// pluggable emuhem::IQSource (noise generator by default, or a file-backed
// source when EMUHEM_IQ_FILE is set). The producer loop is paced via
// std::chrono::steady_clock so that buffer delivery tracks the configured
// baseband sample rate (set by radio::set_baseband_rate).
//
// Phase 5.3: each freshly-produced buffer is also fanned out to any
// connected rtl_tcp server clients (EMUHEM_RTL_TCP_SERVER=host:port), and
// upstream tuning (sample rate / frequency / gain) is forwarded to the
// active source via its on_* hooks.

#include "baseband_dma.hpp"
#include "ch.h"
#include "iq_source.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr size_t kBufferSamples = 8192;
constexpr size_t kTransfersPerBuffer = 4;
constexpr size_t kTransferSamples = kBufferSamples / kTransfersPerBuffer;  // 2048

std::array<baseband::sample_t, kBufferSamples> g_buffer{};
size_t g_transfer_index = 0;
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_sample_rate{0};  // 0 = unknown, fall back to fixed sleep

std::mutex g_source_mutex;
std::unique_ptr<emuhem::IQSource> g_source;
std::unique_ptr<emuhem::IQSink> g_sink;
std::unique_ptr<emuhem::RtlTcpServer> g_server;

// Direction set by firmware via configure(). RX: produce samples via
// g_source and feed to the processor. TX: processor fills the buffer and
// we drain it to g_sink.
std::atomic<baseband::Direction> g_direction{baseband::Direction::Receive};

// TX drain pipelining: the slice returned from wait_for_buffer() is filled
// by the processor's execute() after we return. We therefore can only
// drain the *previous* slice on the next call. Tracked here.
baseband::sample_t* g_tx_pending_slice = nullptr;
size_t g_tx_pending_count = 0;

// Realtime pacing deadline. Reset by configure().
std::chrono::steady_clock::time_point g_next_deadline{};
bool g_deadline_valid = false;

// Server-only producer: runs a parallel source read when a server client is
// connected AND the baseband DMA is not producing (no active firmware app).
// This lets EmuHem act as a pure I/Q source for gqrx/SDR++/GNU Radio without
// needing to launch a firmware receiver app.
std::thread g_server_pump_thread;
std::atomic<bool> g_server_pump_stop{false};

void ensure_source_locked() {
    if (!g_source) {
        g_source = emuhem::make_default_source();
        std::fprintf(stderr, "[EmuHem] baseband dma: source = %s\n", g_source->name());
    }
}

void ensure_sink_locked() {
    if (!g_sink) {
        g_sink = emuhem::make_default_sink();
        std::fprintf(stderr, "[EmuHem] baseband dma: sink = %s\n", g_sink->name());
    }
}

void pace_for_transfer() {
    const uint32_t rate = g_sample_rate.load();
    const auto now = std::chrono::steady_clock::now();

    if (rate == 0) {
        // Unknown rate: conservative 2ms pacing (matches Phase 3 behavior).
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        g_deadline_valid = false;
        return;
    }

    const auto per_transfer = std::chrono::nanoseconds(
        static_cast<int64_t>(kTransferSamples) * 1'000'000'000LL / rate);

    if (!g_deadline_valid) {
        g_next_deadline = now + per_transfer;
        g_deadline_valid = true;
        // No sleep on first call so the thread spins up immediately.
        return;
    }

    if (g_next_deadline > now) {
        std::this_thread::sleep_until(g_next_deadline);
        g_next_deadline += per_transfer;
    } else {
        // Producer has fallen behind; clamp deadline forward so we don't
        // burst-generate catch-up buffers after a long pause.
        g_next_deadline = now + per_transfer;
    }
}

}  // namespace

namespace baseband {
namespace dma {

void init() {
    // No hardware DMA to initialize. Source is lazily created on first
    // wait_for_buffer() so env vars read at emulator startup still apply.
}

void configure(
    baseband::sample_t* const /*buffer_base*/,
    const baseband::Direction direction) {
    // Use our internal buffer regardless of what firmware passes in.
    g_transfer_index = 0;
    std::memset(g_buffer.data(), 0, sizeof(g_buffer));
    g_deadline_valid = false;
    g_direction.store(direction);
    g_tx_pending_slice = nullptr;
    g_tx_pending_count = 0;
    std::fprintf(stderr, "[EmuHem] baseband dma: direction = %s\n",
                 direction == baseband::Direction::Transmit ? "TX" : "RX");
}

void enable(const baseband::Direction /*direction*/) {
    g_enabled.store(true);
}

bool is_enabled() {
    return g_enabled.load();
}

void disable() {
    g_enabled.store(false);
    g_deadline_valid = false;

    // Final TX flush: drain any pending slice the processor just filled so we
    // don't lose the last 2048 samples on a fast stop.
    if (g_direction.load() == baseband::Direction::Transmit && g_tx_pending_slice) {
        std::lock_guard<std::mutex> lk(g_source_mutex);
        ensure_sink_locked();
        g_sink->write(g_tx_pending_slice, g_tx_pending_count);
        g_tx_pending_slice = nullptr;
        g_tx_pending_count = 0;
    }
}

void preload_source() {
    std::lock_guard<std::mutex> lk(g_source_mutex);
    ensure_source_locked();
    // Eagerly instantiate a TX sink when the user passed --iq-tx-* so the
    // file is created (and the user sees the log line) before the first TX
    // processor runs. Without this, a typo in --iq-tx-file would only
    // surface after navigating into a TX app.
    if (std::getenv("EMUHEM_IQ_TX_FILE") || std::getenv("EMUHEM_IQ_TX_SOAPY")) {
        ensure_sink_locked();
    }

    // Diagnostic: EMUHEM_TX_TEST=<sample_count> exercises the TX pipeline at
    // startup so the sink write path can be validated without launching a
    // firmware TX app. Writes a ramp pattern (re=i, im=-i, wrapped to int8).
    if (const char* spec = std::getenv("EMUHEM_TX_TEST"); spec && *spec) {
        const size_t count = static_cast<size_t>(std::strtoul(spec, nullptr, 10));
        if (count > 0) {
            ensure_sink_locked();
            std::vector<baseband::sample_t> ramp(count);
            for (size_t i = 0; i < count; ++i) {
                const int8_t re = static_cast<int8_t>((i * 3) & 0xFF);
                const int8_t im = static_cast<int8_t>(-static_cast<int>(i * 5) & 0xFF);
                ramp[i] = {re, im};
            }
            g_sink->write(ramp.data(), count);
            std::fprintf(stderr, "[EmuHem] TX_TEST: wrote %zu ramp samples via sink\n", count);
        }
    }

    // Diagnostic: EMUHEM_NCO_TEST=<rate_hz>:<tuned_hz> drives the NCO hooks
    // at startup so the shift computation can be verified without launching
    // a firmware app. Logs the shift in Hz and the 32-bit phase increment.
    if (const char* spec = std::getenv("EMUHEM_NCO_TEST"); spec && *spec) {
        const char* colon = std::strchr(spec, ':');
        if (colon) {
            const uint32_t rate = static_cast<uint32_t>(std::strtoul(spec, nullptr, 10));
            const uint64_t tuned = std::strtoull(colon + 1, nullptr, 10);
            std::fprintf(stderr, "[EmuHem] NCO_TEST: rate=%u tuned=%llu\n",
                         rate, static_cast<unsigned long long>(tuned));
            if (g_source) {
                g_source->on_sample_rate_changed(rate);
                g_source->on_center_frequency_changed(tuned);
                // Also exercise the read path so NCO actually rotates samples.
                std::array<baseband::sample_t, 8> probe{};
                g_source->read(probe.data(), probe.size());
                std::fprintf(stderr, "[EmuHem] NCO_TEST: first samples: ");
                for (auto& s : probe) {
                    std::fprintf(stderr, "(%d,%d) ", s.real(), s.imag());
                }
                std::fprintf(stderr, "\n");
            }
        } else {
            std::fprintf(stderr, "[EmuHem] NCO_TEST: expected rate:tuned format, got '%s'\n", spec);
        }
    }

    if (!g_server) {
        g_server = emuhem::make_default_server();
        if (g_server) {
            g_server_pump_stop.store(false);
            g_server_pump_thread = std::thread([] {
                std::array<baseband::sample_t, kTransferSamples> scratch{};
                auto next = std::chrono::steady_clock::now();
                while (!g_server_pump_stop.load()) {
                    if (g_enabled.load()) {
                        // Firmware DMA is producing; it handles fan-out.
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        next = std::chrono::steady_clock::now();
                        continue;
                    }
                    if (!g_server || !g_server->has_clients()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        next = std::chrono::steady_clock::now();
                        continue;
                    }
                    const uint32_t rate = g_sample_rate.load();
                    const uint32_t pace_rate = rate ? rate : 2'400'000;
                    {
                        std::lock_guard<std::mutex> lk(g_source_mutex);
                        ensure_source_locked();
                        g_source->read(scratch.data(), scratch.size());
                        g_server->push(scratch.data(), scratch.size());
                    }
                    next += std::chrono::nanoseconds(
                        static_cast<int64_t>(kTransferSamples) * 1'000'000'000LL / pace_rate);
                    const auto now = std::chrono::steady_clock::now();
                    if (next > now) {
                        std::this_thread::sleep_until(next);
                    } else {
                        next = now;
                    }
                }
            });
            g_server_pump_thread.detach();
        }
    }
}

void set_sample_rate(uint32_t rate) {
    const uint32_t prev = g_sample_rate.exchange(rate);
    if (prev != rate) {
        std::fprintf(stderr, "[EmuHem] baseband dma: sample rate = %u Hz\n", rate);
        g_deadline_valid = false;
        // Forward to the active source AND sink so network-backed endpoints re-tune.
        std::lock_guard<std::mutex> lk(g_source_mutex);
        if (g_source) g_source->on_sample_rate_changed(rate);
        if (g_sink) g_sink->on_sample_rate_changed(rate);
    }
}

baseband::buffer_t wait_for_buffer() {
    pace_for_transfer();

    if (!g_enabled.load()) {
        return {nullptr, 0};
    }
    if (chThdShouldTerminate()) {
        return {nullptr, 0};
    }

    auto* slice = &g_buffer[g_transfer_index * kTransferSamples];

    if (g_direction.load() == baseband::Direction::Transmit) {
        // TX: drain the previous slice (which the processor filled after our
        // previous return) into the sink, then hand out a fresh zeroed slice
        // for the processor to write into.
        std::lock_guard<std::mutex> lk(g_source_mutex);
        ensure_sink_locked();
        if (g_tx_pending_slice) {
            g_sink->write(g_tx_pending_slice, g_tx_pending_count);
            // Also fan the transmitted I/Q to any rtl_tcp listeners — handy
            // for visualizing our own TX output in gqrx/SDR++ without extra
            // hardware.
            if (g_server && g_server->has_clients()) {
                g_server->push(g_tx_pending_slice, g_tx_pending_count);
            }
        }
        std::memset(slice, 0, kTransferSamples * sizeof(baseband::sample_t));
        g_tx_pending_slice = slice;
        g_tx_pending_count = kTransferSamples;
    } else {
        // RX (unchanged): fill the slice from the source, fan to server.
        std::lock_guard<std::mutex> lk(g_source_mutex);
        ensure_source_locked();
        g_source->read(slice, kTransferSamples);
        if (g_server && g_server->has_clients()) {
            g_server->push(slice, kTransferSamples);
        }
    }

    const size_t current = g_transfer_index;
    g_transfer_index = (g_transfer_index + 1) % kTransfersPerBuffer;

    return {&g_buffer[current * kTransferSamples], kTransferSamples};
}

}  // namespace dma
}  // namespace baseband

// C-linkage bridges for main_emu.cpp and phase2_stubs.cpp so those files
// don't need to include baseband_dma.hpp / iq_source.hpp.
extern "C" void emuhem_preload_iq_source() {
    baseband::dma::preload_source();
}

extern "C" void emuhem_baseband_set_sample_rate(uint32_t hz) {
    baseband::dma::set_sample_rate(hz);
}

// Forward tuning state from the radio shims to the active I/Q source AND sink
// so the rtl_tcp client / Soapy device on either end re-tunes.
extern "C" void emuhem_iq_set_center_frequency(uint64_t hz) {
    std::lock_guard<std::mutex> lk(g_source_mutex);
    if (g_source) g_source->on_center_frequency_changed(hz);
    if (g_sink) g_sink->on_center_frequency_changed(hz);
}

extern "C" void emuhem_iq_set_tuner_gain_tenths_db(int32_t tenths_db) {
    std::lock_guard<std::mutex> lk(g_source_mutex);
    if (g_source) g_source->on_tuner_gain_changed(tenths_db);
    // Firmware does not distinguish TX gain from RX gain at this bridge — the
    // direction is already latched in the baseband DMA. Forward the same
    // value; TX sinks apply it to the TX chain, RX sources to the RX chain.
    if (g_sink) g_sink->on_tx_gain_changed(tenths_db);
}
