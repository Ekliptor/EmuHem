// EmuHem -- PortaPack Mayhem Firmware Emulator
// Entry point: runs firmware EventDispatcher in a thread, SDL render loop on main thread.

#include <SDL3/SDL.h>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <execinfo.h>
#include <unistd.h>

extern "C" void emuhem_preload_iq_source();
extern "C" void emuhem_inject_touch(int32_t x, int32_t y, uint8_t type);

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
extern EventDispatcher* event_dispatcher_ptr;


// Input control functions (from irq_controls_emu.cpp)
extern void emu_set_switches(uint8_t mask);
extern void emu_adjust_encoder(int delta);

static constexpr int LCD_WIDTH = 240;
static constexpr int LCD_HEIGHT = 320;
static constexpr int SCALE = 2;

// Bezel frame around the LCD in virtual (pre-scale) pixels.
// PortaPack Mayhem has the display at the top and physical buttons below.
// Set EMUHEM_BEZEL=0 to disable and get the bare LCD window (pre-6c behavior).
static constexpr int BEZEL_LEFT   = 20;
static constexpr int BEZEL_RIGHT  = 20;
static constexpr int BEZEL_TOP    = 20;
static constexpr int BEZEL_BOTTOM = 80;  // space for button-cluster labels

static int g_bezel_left   = BEZEL_LEFT;
static int g_bezel_right  = BEZEL_RIGHT;
static int g_bezel_top    = BEZEL_TOP;
static int g_bezel_bottom = BEZEL_BOTTOM;

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
    event_dispatcher_ptr = &event_dispatcher;
    portapack::setEventDispatcherToUSBSerial(&event_dispatcher);

    g_firmware_running.store(true);

    std::fprintf(stdout, "[EmuHem] Entering firmware event loop\n");

    // --app=<name>: jump directly to a firmware app, bypassing menu navigation.
    // Uses the firmware's own id-based lookup (ui_navigation.cpp::appList). The
    // same mechanism usb_serial_shell uses, so every id listed in appList with
    // a non-null name is launchable (e.g. "audio", "adsbrx", "pocsag",
    // "spectrum", "aprsrx", …).
    if (const char* app_name = std::getenv("EMUHEM_APP"); app_name && *app_name) {
        auto* nav = system_view.get_navigation_view();
        if (nav && nav->StartAppByName(app_name)) {
            std::fprintf(stdout, "[EmuHem] --app: launched '%s'\n", app_name);
        } else {
            std::fprintf(stderr,
                         "[EmuHem] --app: unknown app id '%s' (check the "
                         "appList in ui_navigation.cpp for valid ids)\n",
                         app_name);
        }
    }

    // Run the firmware's main event loop
    // This blocks until EventDispatcher::request_stop() is called
    event_dispatcher.run();

    // TX-shutdown-latency mitigation: the baseband M4 thread keeps pumping
    // samples until a ShutdownMessage arrives. Normally that happens when
    // the current view's destructor runs (e.g. ~APRSTXView calls
    // baseband::shutdown()), but those destructors only run at static
    // teardown after main() returns. Under sequential CTest load, the idle
    // TX pump can delay process exit past the test's `timeout 60` wrapper.
    //
    // Send the ShutdownMessage here via the firmware's own baseband::shutdown
    // API, which is guarded by `baseband_image_running` and is a no-op if no
    // image is running (e.g. utility apps like notepad). The M4 dispatcher
    // consumes the message, BasebandThread exits its loop, and the process
    // shuts down promptly. No join: the M4 std::thread is owned by a heap
    // pointer in core_control_emu.cpp and continues cleanup while main joins
    // the firmware thread.
    extern void emuhem_shutdown_baseband();
    emuhem_shutdown_baseband();

    g_firmware_running.store(false);
    event_dispatcher_ptr = nullptr;
    std::fprintf(stdout, "[EmuHem] Firmware thread exiting\n");
}

// ===== Touch injection ==========================================================
//
// SDL captures mouse button/motion events on the main thread, enqueues them
// here, and a dedicated injector thread blocks on `emulateTouch` so the SDL
// render loop stays responsive.
namespace {

struct TouchEvent {
    int32_t x, y;
    uint8_t type;  // 0=Start, 1=Move, 2=End (matches ui::TouchEvent::Type)
};

std::mutex g_touch_mutex;
std::condition_variable g_touch_cv;
std::deque<TouchEvent> g_touch_queue;
bool g_mouse_down = false;
int32_t g_last_touch_x = -1, g_last_touch_y = -1;

void enqueue_touch(uint8_t type, int32_t x, int32_t y) {
    if (x < 0) x = 0;
    if (x >= LCD_WIDTH) x = LCD_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= LCD_HEIGHT) y = LCD_HEIGHT - 1;
    // Deduplicate Move events that land on the same pixel; the firmware
    // re-processes every Move, so flooding it wastes cycles.
    if (type == 1 && x == g_last_touch_x && y == g_last_touch_y) return;
    g_last_touch_x = x;
    g_last_touch_y = y;

    std::lock_guard<std::mutex> lk(g_touch_mutex);
    g_touch_queue.push_back({x, y, type});
    g_touch_cv.notify_one();
}

void touch_injector_thread_fn() {
    while (!g_quit_requested.load()) {
        std::unique_lock<std::mutex> lk(g_touch_mutex);
        g_touch_cv.wait_for(lk, std::chrono::milliseconds(100), [] {
            return !g_touch_queue.empty() || g_quit_requested.load();
        });
        if (g_touch_queue.empty()) continue;
        auto ev = g_touch_queue.front();
        g_touch_queue.pop_front();
        lk.unlock();
        if (g_firmware_running.load()) {
            emuhem_inject_touch(ev.x, ev.y, ev.type);
        }
    }
}

}  // namespace

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

// ===== CLI parsing + crash handler + scripted input ==========================
namespace {

struct CliOptions {
    bool help = false;
    bool headless = false;
    int duration_sec = 0;         // 0 = run until quit
    bool no_bezel = false;        // --bezel=0 overrides default bezel
    std::string keys;             // scripted keystrokes
    int key_step_ms = 180;        // delay between scripted key press/release
    std::string fb_dump;          // --fb-dump=PATH: write final framebuffer as raw RGB565
};

void print_help(const char* argv0) {
    std::fprintf(stdout,
        "EmuHem -- PortaPack Mayhem emulator\n"
        "Usage: %s [options]\n"
        "\n"
        "General:\n"
        "  --help, -h             Show this help and exit.\n"
        "  --headless             Run without opening an SDL window (firmware still runs).\n"
        "  --duration=SEC         Quit automatically after SEC seconds (>=0; 0 = no limit).\n"
        "  --bezel=0|1            Override the device bezel frame (default 1).\n"
        "\n"
        "I/Q source (also settable via EMUHEM_* envs):\n"
        "  --iq-file=PATH         Play this .c8/.cu8/.cs16/.cf32/.wav capture.\n"
        "  --iq-loop=0|1          Loop the file (default 1).\n"
        "  --iq-tcp=HOST:PORT     Connect to a remote rtl_tcp server.\n"
        "  --soapy=ARGS           Use a USB SDR dongle via SoapySDR (e.g. 'driver=hackrf').\n"
        "  --soapy-rate=HZ        Initial sample rate for --soapy (default 2_400_000).\n"
        "  --soapy-freq=HZ        Initial center freq for --soapy (default 100_000_000).\n"
        "  --soapy-gain=TENTHS_DB Initial gain for --soapy in 0.1 dB units (default 200).\n"
        "  --iq-center=HZ         Declared recording center (activates NCO shift).\n"
        "\n"
        "I/Q TX sink (where TX processors emit I/Q):\n"
        "  --iq-tx-file=PATH      Write TX I/Q to a CS8 file (.c8).\n"
        "  --iq-tx-soapy=ARGS     Transmit via a SoapySDR device (e.g. 'driver=hackrf').\n"
        "  --iq-tx-soapy-rate=HZ  Initial TX sample rate (default 2_400_000).\n"
        "  --iq-tx-soapy-freq=HZ  Initial TX center freq (default 100_000_000).\n"
        "  --iq-tx-soapy-gain=TENTHS_DB  Initial TX gain in 0.1 dB (default 200).\n"
        "\n"
        "Server:\n"
        "  --rtl-tcp-server=HOST:PORT   Serve baseband I/Q as cu8 to external SDR clients.\n"
        "\n"
        "Filesystem:\n"
        "  --sdcard-root=PATH     Override ~/.emuhem/sdcard.\n"
        "  --pmem-file=PATH       Override ~/.emuhem/pmem_settings.bin.\n"
        "\n"
        "App launch:\n"
        "  --app=ID               Launch a firmware app directly, skipping menu navigation.\n"
        "                         Example ids: audio, adsbrx, ais, pocsag, aprsrx, weather,\n"
        "                         search, lookingglass, recon, capture, replay, aprstx,\n"
        "                         bletx, ooktx, rdstx, microphone, filemanager, freqman,\n"
        "                         iqtrim, notepad. Full list in the firmware's\n"
        "                         ui_navigation.cpp `appList`.\n"
        "\n"
        "Test harness:\n"
        "  --fb-dump=PATH         Write the final LCD framebuffer as raw RGB565\n"
        "                         (240*320*2 = 153600 bytes) to PATH on shutdown.\n"
        "                         Used by integration tests to assert rendering happened.\n"
        "\n"
        "Scripted input:\n"
        "  --keys=STRING          Play a keystroke sequence once the UI is ready. Chars:\n"
        "                           U/D/L/R = d-pad   S = Select   B = Back\n"
        "                           F = Dfu           + = encoder+1   - = encoder-1\n"
        "                           . = 200ms pause\n"
        "  --key-step=MS          Press/release delay for scripted keys (default 180).\n"
        "\n"
        "Examples:\n"
        "  %s --headless --duration=5 --keys='DDS' --iq-file=capture.cu8\n"
        "  %s --bezel=0 --rtl-tcp-server=0.0.0.0:1234\n"
        "  %s --soapy='driver=hackrf' --soapy-rate=8000000 --soapy-freq=915000000\n"
        "  %s --app=audio --iq-file=capture.cu8\n",
        argv0, argv0, argv0, argv0, argv0);
}

bool parse_int(std::string_view s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(std::string{s}.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

// If `arg` starts with `--<name>=`, return the value; otherwise nullopt.
// Also handles the bare `--<name>` form (returns empty string for use as flag).
bool match_flag(std::string_view arg, std::string_view name, std::string& value_out,
                bool& was_value_given) {
    if (arg.size() < name.size() + 2) return false;
    if (arg.substr(0, 2) != "--") return false;
    if (arg.substr(2, name.size()) != name) return false;
    const auto rest = arg.substr(2 + name.size());
    if (rest.empty()) {
        value_out.clear();
        was_value_given = false;
        return true;
    }
    if (rest.front() != '=') return false;
    value_out.assign(rest.substr(1));
    was_value_given = true;
    return true;
}

// Parse argv into options and into setenv() side-effects for env-equivalent flags.
// Returns false on error (message already printed).
bool parse_cli(int argc, char** argv, CliOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--help" || a == "-h") { opts.help = true; return true; }

        std::string v;
        bool has_v = false;
        auto takes = [&](std::string_view name) {
            return match_flag(a, name, v, has_v) && has_v;
        };
        auto flag = [&](std::string_view name) {
            return match_flag(a, name, v, has_v);
        };

        if (takes("iq-file"))            { ::setenv("EMUHEM_IQ_FILE", v.c_str(), 1); continue; }
        if (takes("iq-loop"))            { ::setenv("EMUHEM_IQ_LOOP", v.c_str(), 1); continue; }
        if (takes("iq-tcp"))             { ::setenv("EMUHEM_IQ_TCP",  v.c_str(), 1); continue; }
        if (takes("iq-center"))          { ::setenv("EMUHEM_IQ_CENTER", v.c_str(), 1); continue; }
        if (takes("soapy"))              { ::setenv("EMUHEM_IQ_SOAPY", v.c_str(), 1); continue; }
        if (takes("soapy-rate"))         { ::setenv("EMUHEM_IQ_SOAPY_RATE", v.c_str(), 1); continue; }
        if (takes("soapy-freq"))         { ::setenv("EMUHEM_IQ_SOAPY_FREQ", v.c_str(), 1); continue; }
        if (takes("soapy-gain"))         { ::setenv("EMUHEM_IQ_SOAPY_GAIN", v.c_str(), 1); continue; }
        if (takes("iq-tx-file"))         { ::setenv("EMUHEM_IQ_TX_FILE", v.c_str(), 1); continue; }
        if (takes("iq-tx-soapy"))        { ::setenv("EMUHEM_IQ_TX_SOAPY", v.c_str(), 1); continue; }
        if (takes("iq-tx-soapy-rate"))   { ::setenv("EMUHEM_IQ_TX_SOAPY_RATE", v.c_str(), 1); continue; }
        if (takes("iq-tx-soapy-freq"))   { ::setenv("EMUHEM_IQ_TX_SOAPY_FREQ", v.c_str(), 1); continue; }
        if (takes("iq-tx-soapy-gain"))   { ::setenv("EMUHEM_IQ_TX_SOAPY_GAIN", v.c_str(), 1); continue; }
        if (takes("rtl-tcp-server"))     { ::setenv("EMUHEM_RTL_TCP_SERVER", v.c_str(), 1); continue; }
        if (takes("sdcard-root"))        { ::setenv("EMUHEM_SDCARD_ROOT", v.c_str(), 1); continue; }
        if (takes("pmem-file"))          { ::setenv("EMUHEM_PMEM_FILE", v.c_str(), 1); continue; }
        if (takes("app"))                { ::setenv("EMUHEM_APP", v.c_str(), 1); continue; }
        if (takes("fb-dump"))            { opts.fb_dump = v; continue; }
        if (takes("keys"))               { opts.keys = v; continue; }
        if (takes("key-step")) {
            if (!parse_int(v, opts.key_step_ms) || opts.key_step_ms < 1) {
                std::fprintf(stderr, "[EmuHem] invalid --key-step=%s\n", v.c_str());
                return false;
            }
            continue;
        }
        if (takes("duration")) {
            if (!parse_int(v, opts.duration_sec) || opts.duration_sec < 0) {
                std::fprintf(stderr, "[EmuHem] invalid --duration=%s\n", v.c_str());
                return false;
            }
            continue;
        }
        if (flag("headless") && !has_v) { opts.headless = true; continue; }
        if (takes("bezel")) {
            if (v == "0") {
                opts.no_bezel = true;
                ::setenv("EMUHEM_BEZEL", "0", 1);
            } else if (v != "1") {
                std::fprintf(stderr, "[EmuHem] --bezel must be 0 or 1 (got '%s')\n", v.c_str());
                return false;
            }
            continue;
        }

        std::fprintf(stderr, "[EmuHem] unknown argument: %s (try --help)\n", argv[i]);
        return false;
    }
    return true;
}

void crash_handler(int sig) {
    // Signal-safe: use only async-signal-safe calls (write, backtrace_symbols_fd).
    const char* msg = "\n[EmuHem] CRASH — caught signal, backtrace follows:\n";
    (void)!::write(STDERR_FILENO, msg, std::strlen(msg));
    void* frames[64];
    const int n = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, n, STDERR_FILENO);
    // Restore default handler and re-raise so the OS still produces a core
    // dump / crash report with accurate state.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void install_crash_handler() {
    struct sigaction sa{};
    sa.sa_handler = crash_handler;
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
        ::sigaction(sig, &sa, nullptr);
    }
}

// Termination signals: SIGTERM (polite kill), SIGINT (Ctrl-C), SIGHUP (parent
// shell disconnect). Flip the quit flag so the main loop drops out of its
// SDL/headless sleep and runs the normal shutdown path (joins threads, flushes
// TX sinks, destroys SDL). Second signal of the same kind forces an exit so a
// stuck shutdown path can always be killed with two Ctrl-C's.
void termination_handler(int sig) {
    static std::atomic<int> s_count{0};
    const int n = s_count.fetch_add(1) + 1;
    // Async-signal-safe stderr write.
    const char* msg = (sig == SIGINT)
                          ? (n == 1 ? "\n[EmuHem] SIGINT — requesting shutdown (Ctrl-C again to force)\n"
                                    : "\n[EmuHem] SIGINT again — forcing exit\n")
                          : (n == 1 ? "\n[EmuHem] termination signal — requesting shutdown\n"
                                    : "\n[EmuHem] termination signal again — forcing exit\n");
    (void)!::write(STDERR_FILENO, msg, std::strlen(msg));
    g_quit_requested.store(true);
    if (n >= 2) {
        // Restore default handler and re-raise so the OS can terminate us
        // even if main-thread shutdown is wedged.
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
}

void install_termination_handler() {
    struct sigaction sa{};
    sa.sa_handler = termination_handler;
    // No SA_RESETHAND — we want the handler to stay installed so a second
    // signal can escalate to force-exit via the n>=2 branch.
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
        ::sigaction(sig, &sa, nullptr);
    }
}

// ===== Scripted keys ====================================================
//
// Converts a --keys string into a sequence of emu_set_switches / encoder
// pulses. Runs on its own thread so it can sleep between presses without
// blocking the render loop.
//
// Char mapping (case-insensitive):
//   U/D/L/R = d-pad up/down/left/right   S = Select (Enter)   B = Back (Esc)
//   F = Dfu (Backspace)                   + / - = encoder tick   . = 200ms pause
constexpr uint8_t kBitRight  = 1 << 0;
constexpr uint8_t kBitLeft   = 1 << 1;
constexpr uint8_t kBitDown   = 1 << 2;
constexpr uint8_t kBitUp     = 1 << 3;
constexpr uint8_t kBitSel    = 1 << 4;
constexpr uint8_t kBitDfu    = 1 << 5;

void scripted_keys_thread_fn(std::string keys, int step_ms) {
    // Wait for firmware to reach its event loop.
    while (!g_quit_requested.load() && !g_firmware_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Extra settle so the splash screen can get out of the way.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    for (char c : keys) {
        if (g_quit_requested.load()) break;
        switch (std::toupper(static_cast<unsigned char>(c))) {
            case 'U': case 'D': case 'L': case 'R': case 'S': case 'B': case 'F': {
                uint8_t mask = 0;
                switch (std::toupper(static_cast<unsigned char>(c))) {
                    case 'U': mask = kBitUp; break;
                    case 'D': mask = kBitDown; break;
                    case 'L': mask = kBitLeft; break;
                    case 'R': mask = kBitRight; break;
                    case 'S': mask = kBitSel; break;
                    case 'B': mask = kBitLeft | kBitUp; break;  // Back = Left+Up
                    case 'F': mask = kBitDfu; break;
                }
                emu_set_switches(mask);
                EventDispatcher::events_flag(EVT_MASK_SWITCHES);
                std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
                emu_set_switches(0);
                EventDispatcher::events_flag(EVT_MASK_SWITCHES);
                std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
                break;
            }
            case '+':
                emu_adjust_encoder(1);
                EventDispatcher::events_flag(EVT_MASK_ENCODER);
                std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
                break;
            case '-':
                emu_adjust_encoder(-1);
                EventDispatcher::events_flag(EVT_MASK_ENCODER);
                std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
                break;
            case '.':
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            default:
                std::fprintf(stderr, "[EmuHem] keys: ignoring unknown char '%c'\n", c);
                break;
        }
    }
    std::fprintf(stdout, "[EmuHem] keys: script complete\n");
}

}  // namespace

int main(int argc, char* argv[]) {
    // Line-buffer stdout even when redirected, so integration tests and
    // timed-shutdown diagnostics (--duration elapsed, Shutting down, Done)
    // appear in order with the unbuffered stderr emitted by worker threads.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    install_crash_handler();
    install_termination_handler();

    CliOptions opts;
    if (!parse_cli(argc, argv, opts)) return 2;
    if (opts.help) {
        print_help(argv[0]);
        return 0;
    }

    std::fprintf(stdout, "[EmuHem] Starting PortaPack Mayhem Emulator...\n");

    // Initialize SDL3 (must be on main thread for macOS). In headless mode we
    // still init the subsystems so audio/texture APIs used elsewhere don't
    // misbehave, but we skip window/renderer creation.
    const uint32_t sdl_flags = opts.headless ? SDL_INIT_AUDIO : (SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    if (!SDL_Init(sdl_flags)) {
        std::fprintf(stderr, "[EmuHem] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Eagerly resolve the I/Q source so env-var misconfiguration surfaces at
    // startup rather than on first app launch.
    emuhem_preload_iq_source();

    // EMUHEM_BEZEL=0 (from env or --bezel=0) disables the device frame.
    if (const char* bez = std::getenv("EMUHEM_BEZEL"); bez && std::string{bez} == "0") {
        g_bezel_left = g_bezel_right = g_bezel_top = g_bezel_bottom = 0;
    }

    const int window_w = (LCD_WIDTH + g_bezel_left + g_bezel_right) * SCALE;
    const int window_h = (LCD_HEIGHT + g_bezel_top + g_bezel_bottom) * SCALE;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    if (!opts.headless) {
        window = SDL_CreateWindow(
            "EmuHem - PortaPack Mayhem Emulator",
            window_w, window_h, 0);
        if (!window) {
            std::fprintf(stderr, "[EmuHem] SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            std::fprintf(stderr, "[EmuHem] SDL_CreateRenderer failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        texture = SDL_CreateTexture(
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

        std::fprintf(stdout,
                     "[EmuHem] SDL window created (%dx%d @ %dx, LCD %dx%d, bezel L%d R%d T%d B%d)\n",
                     window_w, window_h, SCALE, LCD_WIDTH, LCD_HEIGHT,
                     g_bezel_left, g_bezel_right, g_bezel_top, g_bezel_bottom);
    } else {
        std::fprintf(stdout, "[EmuHem] headless mode (no window)\n");
    }

    // Start timer threads
    std::thread frame_sync_thread(frame_sync_thread_fn);
    std::thread rtc_tick_thread(rtc_tick_thread_fn);
    std::thread touch_injector_thread(touch_injector_thread_fn);

    // Start firmware thread
    std::thread firmware_thread(firmware_thread_fn);

    // Give firmware time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread scripted_keys_thread;
    if (!opts.keys.empty()) {
        std::fprintf(stdout, "[EmuHem] keys: scheduling '%s' (step=%dms)\n",
                     opts.keys.c_str(), opts.key_step_ms);
        scripted_keys_thread = std::thread(scripted_keys_thread_fn, opts.keys, opts.key_step_ms);
    }

    // Auto-quit timer when --duration=N is set.
    std::thread duration_thread;
    if (opts.duration_sec > 0) {
        duration_thread = std::thread([d = opts.duration_sec] {
            for (int i = 0; i < d * 10; ++i) {
                if (g_quit_requested.load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::fprintf(stdout, "[EmuHem] --duration elapsed, quitting\n");
            g_quit_requested.store(true);
        });
    }

    std::fprintf(stdout, "[EmuHem] Entering %s loop\n", opts.headless ? "headless" : "SDL render");

    // Headless: no event loop; just wait until a duration timer or Ctrl-C
    // flips g_quit_requested. Must stay on the main thread so macOS is happy.
    if (opts.headless) {
        while (!g_quit_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Main SDL event + render loop (must be on main thread for macOS)
    bool running = !opts.headless;
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

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        g_mouse_down = true;
                        const int32_t x = static_cast<int32_t>(event.button.x) / SCALE - g_bezel_left;
                        const int32_t y = static_cast<int32_t>(event.button.y) / SCALE - g_bezel_top;
                        enqueue_touch(0 /*Start*/, x, y);
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if (event.button.button == SDL_BUTTON_LEFT && g_mouse_down) {
                        g_mouse_down = false;
                        const int32_t x = static_cast<int32_t>(event.button.x) / SCALE - g_bezel_left;
                        const int32_t y = static_cast<int32_t>(event.button.y) / SCALE - g_bezel_top;
                        enqueue_touch(2 /*End*/, x, y);
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_MOTION: {
                    if (g_mouse_down) {
                        const int32_t x = static_cast<int32_t>(event.motion.x) / SCALE - g_bezel_left;
                        const int32_t y = static_cast<int32_t>(event.motion.y) / SCALE - g_bezel_top;
                        enqueue_touch(1 /*Move*/, x, y);
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

        // Bezel: dark gray body.
        SDL_SetRenderDrawColor(renderer, 38, 38, 44, 255);
        SDL_RenderClear(renderer);

        const float lcd_x = static_cast<float>(g_bezel_left * SCALE);
        const float lcd_y = static_cast<float>(g_bezel_top * SCALE);
        const float lcd_w = static_cast<float>(LCD_WIDTH * SCALE);
        const float lcd_h = static_cast<float>(LCD_HEIGHT * SCALE);

        if (g_bezel_left > 0 || g_bezel_top > 0) {
            // Thin inner frame around the LCD to make the screen edge pop.
            SDL_FRect inner_bg = {lcd_x - 3.0f, lcd_y - 3.0f, lcd_w + 6.0f, lcd_h + 6.0f};
            SDL_SetRenderDrawColor(renderer, 15, 15, 18, 255);
            SDL_RenderFillRect(renderer, &inner_bg);

            // Button-cluster placeholder: 5 dots centered in the bottom bezel
            // (D-pad Left/Up/Right/Down + center Select). Purely decorative —
            // real input still goes through keyboard/mouse.
            const float cy = (g_bezel_top + LCD_HEIGHT) * SCALE + (g_bezel_bottom * SCALE) * 0.5f;
            const float cx = g_bezel_left * SCALE + LCD_WIDTH * SCALE * 0.5f;
            const float r = 8.0f;
            const float spacing = 28.0f;
            const SDL_FRect dots[5] = {
                {cx - r, cy - r, 2 * r, 2 * r},                    // center / Select
                {cx - spacing - r, cy - r, 2 * r, 2 * r},          // Left
                {cx + spacing - r, cy - r, 2 * r, 2 * r},          // Right
                {cx - r, cy - spacing - r, 2 * r, 2 * r},          // Up
                {cx - r, cy + spacing - r, 2 * r, 2 * r},          // Down
            };
            SDL_SetRenderDrawColor(renderer, 80, 80, 88, 255);
            for (const auto& d : dots) SDL_RenderFillRect(renderer, &d);
        }

        SDL_FRect dest = {lcd_x, lcd_y, lcd_w, lcd_h};
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
    g_touch_cv.notify_all();
    touch_injector_thread.join();
    if (scripted_keys_thread.joinable()) scripted_keys_thread.join();
    if (duration_thread.joinable()) duration_thread.join();

    // Dump the final LCD framebuffer for integration tests. Written as raw
    // RGB565 little-endian (240 * 320 * 2 = 153600 bytes). Done after threads
    // are joined so the firmware can't race a final paint on top of us.
    if (!opts.fb_dump.empty()) {
        const uint16_t* fb = portapack::IO::get_framebuffer();
        if (FILE* f = std::fopen(opts.fb_dump.c_str(), "wb")) {
            const size_t n = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT;
            const size_t w = std::fwrite(fb, sizeof(uint16_t), n, f);
            std::fclose(f);
            std::fprintf(stdout, "[EmuHem] fb-dump: wrote %zu pixels (%zu bytes) to %s\n",
                         w, w * sizeof(uint16_t), opts.fb_dump.c_str());
        } else {
            std::fprintf(stderr, "[EmuHem] fb-dump: failed to open %s for writing\n",
                         opts.fb_dump.c_str());
        }
    }

    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();

    std::fprintf(stdout, "[EmuHem] Done.\n");
    return 0;
}
