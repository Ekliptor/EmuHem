// EmuHem core_control -- M4 processor lifecycle management
// Implements processor registry and m4_init() for baseband integration.

#include "core_control.hpp"
#include "baseband_processor.hpp"
#include "event_m4.hpp"
#include "proc_wideband_spectrum.hpp"
#include "proc_am_audio.hpp"
#include "proc_nfm_audio.hpp"
#include "proc_wfm_audio.hpp"
#include "proc_pocsag2.hpp"
#include "proc_tpms.hpp"
#include "proc_ert.hpp"
#include "proc_ais.hpp"
#include "proc_tones.hpp"
#include "proc_audio_beep.hpp"
#include "proc_siggen.hpp"
#include "proc_afskrx.hpp"
#include "proc_aprsrx.hpp"
#include "proc_btlerx.hpp"
#include "proc_nrfrx.hpp"
#include "proc_fsk_rx.hpp"
#include "proc_weather.hpp"
#include "proc_subghzd.hpp"
#include "proc_protoview.hpp"
#include "proc_afsk.hpp"
#include "proc_fsk.hpp"
#include "proc_ook.hpp"
#include "proc_ble_tx.hpp"
#include "proc_adsbtx.hpp"
#include "proc_rds.hpp"
#include "proc_acars.hpp"
#include "proc_adsbrx.hpp"
#include "proc_flex.hpp"
#include "proc_sonde.hpp"
#include "proc_epirb.hpp"
#include "proc_tonedetect.hpp"
#include "proc_subcar.hpp"
#include "proc_rtty_rx.hpp"
#include "proc_morse.hpp"
#include "proc_morsetx.hpp"
#include "proc_rtty_tx.hpp"
#include "proc_jammer.hpp"
#include "proc_p25_tx.hpp"
#include "proc_gps_sim.hpp"
#include "proc_spectrum_painter.hpp"
#include "proc_audiotx.hpp"
#include "proc_time_sink.hpp"
#include "proc_epirb_tx.hpp"
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

        // Phase 9: RX digital decoders
        r.push_back({image_tag_pocsag2,
            []() { return std::make_unique<POCSAGProcessor>(); }});
        r.push_back({image_tag_tpms,
            []() { return std::make_unique<TPMSProcessor>(); }});
        r.push_back({image_tag_ert,
            []() { return std::make_unique<ERTProcessor>(); }});
        r.push_back({image_tag_ais,
            []() { return std::make_unique<AISProcessor>(); }});

        // Phase 9: TX processors (run but emit no I/Q -- no IQSink yet)
        r.push_back({image_tag_tones,
            []() { return std::make_unique<TonesProcessor>(); }});
        r.push_back({image_tag_audio_beep,
            []() { return std::make_unique<AudioBeepProcessor>(); }});
        r.push_back({image_tag_siggen,
            []() { return std::make_unique<SigGenProcessor>(); }});

        // Phase 10: more RX digital decoders
        r.push_back({image_tag_afsk_rx,
            []() { return std::make_unique<AFSKRxProcessor>(); }});
        r.push_back({image_tag_aprs_rx,
            []() { return std::make_unique<APRSRxProcessor>(); }});
        r.push_back({image_tag_btle_rx,
            []() { return std::make_unique<BTLERxProcessor>(); }});
        r.push_back({image_tag_nrf_rx,
            []() { return std::make_unique<NRFRxProcessor>(); }});
        r.push_back({image_tag_fskrx,
            []() { return std::make_unique<FSKRxProcessor>(); }});
        r.push_back({image_tag_weather,
            []() { return std::make_unique<WeatherProcessor>(); }});
        r.push_back({image_tag_subghzd,
            []() { return std::make_unique<SubGhzDProcessor>(); }});
        r.push_back({image_tag_protoview,
            []() { return std::make_unique<ProtoViewProcessor>(); }});

        // Phase 10: TX modulators (silent until IQSink lands)
        r.push_back({image_tag_afsk,
            []() { return std::make_unique<AFSKProcessor>(); }});
        r.push_back({image_tag_fsktx,
            []() { return std::make_unique<FSKProcessor>(); }});
        r.push_back({image_tag_ook,
            []() { return std::make_unique<OOKProcessor>(); }});
        r.push_back({image_tag_btle_tx,
            []() { return std::make_unique<BTLETxProcessor>(); }});
        r.push_back({image_tag_adsb_tx,
            []() { return std::make_unique<ADSBTXProcessor>(); }});
        r.push_back({image_tag_rds,
            []() { return std::make_unique<RDSProcessor>(); }});

        // Phase 11: more RX digital decoders
        r.push_back({image_tag_acars,
            []() { return std::make_unique<ACARSProcessor>(); }});
        r.push_back({image_tag_adsb_rx,
            []() { return std::make_unique<ADSBRXProcessor>(); }});
        r.push_back({image_tag_flex,
            []() { return std::make_unique<FlexProcessor>(); }});
        r.push_back({image_tag_sonde,
            []() { return std::make_unique<SondeProcessor>(); }});
        r.push_back({image_tag_epirb_rx,
            []() { return std::make_unique<EPIRBProcessor>(); }});
        r.push_back({image_tag_tonedetect,
            []() { return std::make_unique<ToneDetectProcessor>(); }});
        r.push_back({image_tag_subcar,
            []() { return std::make_unique<SubCarProcessor>(); }});
        r.push_back({image_tag_rttyrx,
            []() { return std::make_unique<RTTYRxProcessor>(); }});
        r.push_back({image_tag_morse,
            []() { return std::make_unique<MorseProcessor>(); }});

        // Phase 11: TX modulators (silent until IQSink lands)
        r.push_back({image_tag_morsetx,
            []() { return std::make_unique<MorseTXProcessor>(); }});
        r.push_back({image_tag_rttytx,
            []() { return std::make_unique<RTTYTXProcessor>(); }});
        r.push_back({image_tag_jammer,
            []() { return std::make_unique<JammerProcessor>(); }});
        r.push_back({image_tag_p25_tx,
            []() { return std::make_unique<P25TxProcessor>(); }});
        r.push_back({image_tag_gps,
            []() { return std::make_unique<GPSReplayProcessor>(); }});
        r.push_back({image_tag_spectrum_painter,
            []() { return std::make_unique<SpectrumPainterProcessor>(); }});
        r.push_back({image_tag_audio_tx,
            []() { return std::make_unique<AudioTXProcessor>(); }});
        r.push_back({image_tag_time_sink,
            []() { return std::make_unique<TimeSinkProcessor>(); }});
        r.push_back({image_tag_epirb_tx,
            []() { return std::make_unique<EPIRBTXProcessor>(); }});

        // ToDo: remaining -- SSTV RX/TX, WEFAX RX, NOAA APT, capture/replay,
        //       AM TV, mictx, bint_stream_tx, test, sigfrx, flash_utility,
        //       sd_over_usb.
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
