// EmuHem core_control -- M4 processor lifecycle management
// Implements processor registry and m4_init() for baseband integration.

#include "core_control.hpp"
#include "baseband_processor.hpp"
#include "event_m4.hpp"
#include "proc_wideband_spectrum.hpp"
#include "proc_am_audio.hpp"
#include "proc_nfm_audio.hpp"
#include "proc_wfm_audio.hpp"
#include "spi_image.hpp"
#include "portapack_shared_memory.hpp"
#include "ch.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace portapack::spi_flash;

// Linker symbol stub (firmware uses it for SPI flash image layout)
extern "C" uint32_t _textend = 0;

// Bridge for M0→M4 signaling (referenced by lpc43xx_cpp.hpp)
Thread* g_m4_event_thread = nullptr;

// ============================================================================
// Processor registry
// ============================================================================

using ProcessorFactory = std::function<std::unique_ptr<BasebandProcessor>()>;

struct ProcessorEntry {
    image_tag_t tag;
    ProcessorFactory factory;
};

static std::vector<ProcessorEntry>& registry() {
    static std::vector<ProcessorEntry> r;
    static bool initialized = false;
    if (!initialized) {
        initialized = true;

        // Wideband spectrum analyzer
        r.push_back({image_tag_wideband_spectrum,
            []() { return std::make_unique<WidebandSpectrum>(); }});

        // Phase 3.5: audio demodulators
        r.push_back({image_tag_am_audio,
            []() { return std::make_unique<NarrowbandAMAudio>(); }});
        r.push_back({image_tag_nfm_audio,
            []() { return std::make_unique<NarrowbandFMAudio>(); }});
        r.push_back({image_tag_wfm_audio,
            []() { return std::make_unique<WidebandFMAudio>(); }});

        // ToDo: register SSTV/AFSK/AIS and other baseband image tags
    }
    return r;
}

static ProcessorFactory* find_processor(const image_tag_t& tag) {
    for (auto& entry : registry()) {
        if (entry.tag == tag) return &entry.factory;
    }
    return nullptr;
}

// ============================================================================
// M4 thread lifecycle
// ============================================================================

static std::thread* g_m4_thread = nullptr;

static void shutdown_m4_thread() {
    if (!g_m4_thread) return;

    std::fprintf(stderr, "[EmuHem] Shutting down M4 thread...\n");

    // Send shutdown message to the M4 event dispatcher
    static ShutdownMessage shutdown_msg;
    shared_memory.baseband_message = &shutdown_msg;

    // Signal the M4 event loop to wake up and process the message
    if (g_m4_event_thread) {
        chEvtSignal(g_m4_event_thread, 1U);  // EVT_MASK_BASEBAND
    }

    g_m4_thread->join();
    delete g_m4_thread;
    g_m4_thread = nullptr;
    g_m4_event_thread = nullptr;

    std::fprintf(stderr, "[EmuHem] M4 thread stopped\n");
}

void m4_init(const image_tag_t tag,
             const portapack::memory::region_t,
             const bool) {
    // Shutdown any previous baseband processor
    shutdown_m4_thread();

    auto* factory_ptr = find_processor(tag);
    if (!factory_ptr) {
        std::fprintf(stderr, "[EmuHem] m4_init: unregistered processor tag\n");
        // Set baseband_ready to avoid run_image() hanging
        shared_memory.set_baseband_ready();
        return;
    }

    std::fprintf(stderr, "[EmuHem] m4_init: starting baseband processor\n");

    auto factory = *factory_ptr;
    g_m4_thread = new std::thread([factory]() {
        // Register this thread with the ChibiOS shim
        chSysInit();

        // Create the processor (constructor auto-starts BasebandThread + RSSIThread)
        auto processor = factory();

        // Create and run the M4 event dispatcher
        // (sets baseband_ready, processes messages until shutdown)
        BasebandEventDispatcher dispatcher{std::move(processor)};
        dispatcher.run();

        std::fprintf(stderr, "[EmuHem] M4 event dispatcher exited\n");
    });
}

void m4_init_prepared(const uint32_t, const bool) {
    std::fprintf(stderr, "[EmuHem] m4_init_prepared() stub called\n");
    shared_memory.set_baseband_ready();
}

void m4_request_shutdown() {
    shutdown_m4_thread();
}

void m0_halt() {
    std::fprintf(stderr, "[EmuHem] m0_halt() called\n");
}
