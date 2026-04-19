// EmuHem persistent_memory: file-backed state.
//
// All PortaPack `persistent_memory::*` getters read from a single process-
// local `EmuPmemState` struct; setters mutate it and mark dirty. Loaded from
// disk on first access; flushed to disk when `cache::persist()` is called
// (firmware invokes this once per RTC tick). Path: `$EMUHEM_SDCARD_ROOT/..`
// normally `~/.emuhem/pmem_settings.bin`, overridable via
// `EMUHEM_PMEM_FILE=/path/to/file`.
//
// We don't replicate the firmware's `backup_ram_t` bit-packed layout. We own
// the serialization format (magic + version + padded struct), since EmuHem
// is the only thing that reads it.

#include "portapack_persistent_memory.hpp"
#include "touch.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <type_traits>
#include <unistd.h>

using namespace ui;

namespace portapack {
namespace persistent_memory {

namespace {

constexpr uint32_t kPmemMagic = 0x4D455053;  // 'SPEM' (little-endian 'SPEM')
constexpr uint32_t kPmemVersion = 1;

struct EmuPmemState {
    uint32_t magic;
    uint32_t version;

    // LCD / brightness
    bool config_lcd_normally_black;
    bool apply_fake_brightness;
    uint8_t fake_brightness_level;
    bool config_show_fake_brightness;

    // Frequency
    int64_t target_frequency;
    int32_t correction_ppb;

    // Touch
    touch::Calibration touch_cal;  // 7 x int32_t = 28 bytes
    uint16_t touchscreen_threshold;
    bool disable_touchscreen;

    // Backlight
    backlight_config_t backlight_timer;

    // UI booleans
    bool config_splash;
    bool hide_clock;
    bool clock_with_date;
    bool config_login;
    bool config_audio_mute;
    bool config_speaker_disable;
    bool stealth_mode;
    bool config_hide_fps;
    bool beep_on_packets;
    bool config_tx_disable;
    bool config_disable_external_tcxo;
    bool config_sdcard_high_speed_io;

    // Theme / menu
    uint8_t ui_theme_id;
    uint16_t menu_color;  // ui::Color RGB565

    // CPLD
    uint8_t config_cpld;

    // Modem / POCSAG
    int32_t modem_baudrate;
    uint32_t modem_repeat;
    uint32_t pocsag_last_address;
    uint32_t pocsag_ignore_address;

    // Converter
    bool config_converter;
    int64_t config_converter_freq;

    // Freq step / encoder
    uint32_t config_freq_step;
    uint32_t config_encoder_dial_sensitivity;

    // Antenna
    uint8_t config_antenna_bias;

    // Battery
    uint16_t config_battery_type;

    // Autostart
    int32_t config_autostart_app;

    // Playing dead
    uint32_t playing_dead;

    // DST
    dst_config_t dst;

    // UI hide flags
    bool ui_hide_clock;
    bool ui_hide_speaker;
    bool ui_hide_converter;
    bool ui_hide_stealth;
    bool ui_hide_camera;
    bool ui_hide_sleep;
    bool ui_hide_bias_tee;
    bool ui_hide_sd_card;
    bool ui_hide_mute;
};

static_assert(std::is_trivially_copyable_v<EmuPmemState>,
              "EmuPmemState must be memcpy-serializable");

std::mutex g_mutex;
EmuPmemState g_state{};
std::atomic<bool> g_dirty{false};
bool g_loaded = false;

void apply_defaults_locked() {
    g_state = EmuPmemState{};
    g_state.magic = kPmemMagic;
    g_state.version = kPmemVersion;
    g_state.target_frequency = 433000000;
    g_state.modem_baudrate = 1200;
    g_state.modem_repeat = 5;
    g_state.config_freq_step = 25000;
    g_state.config_splash = true;
    g_state.touch_cal = touch::Calibration{};
    g_state.backlight_timer = backlight_config_t{};
    g_state.menu_color = ui::Color::white().v;
}

std::string pmem_path() {
    if (const char* p = std::getenv("EMUHEM_PMEM_FILE"); p && *p) return p;
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = "/tmp";
    return std::string{home} + "/.emuhem/pmem_settings.bin";
}

void load_locked() {
    apply_defaults_locked();

    const std::string path = pmem_path();
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "[EmuHem] pmem: no existing state at %s (using defaults)\n", path.c_str());
        return;
    }

    EmuPmemState disk{};
    const size_t got = std::fread(&disk, 1, sizeof(disk), fp);
    std::fclose(fp);

    if (got != sizeof(disk) || disk.magic != kPmemMagic || disk.version != kPmemVersion) {
        std::fprintf(stderr,
                     "[EmuHem] pmem: %s is stale or wrong size (got=%zu magic=0x%08x v=%u); using defaults\n",
                     path.c_str(), got, disk.magic, disk.version);
        return;
    }
    g_state = disk;
    std::fprintf(stderr, "[EmuHem] pmem: loaded state from %s\n", path.c_str());
}

void ensure_loaded_locked() {
    if (!g_loaded) {
        load_locked();
        g_loaded = true;
    }
}

// Public helpers for writes (called by every setter under the mutex).
template <typename T>
void write_locked(T& slot, const T& v) {
    if (std::memcmp(&slot, &v, sizeof(T)) != 0) {
        slot = v;
        g_dirty.store(true, std::memory_order_relaxed);
    }
}

void save_if_dirty() {
    if (!g_dirty.exchange(false)) return;

    EmuPmemState snap;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        ensure_loaded_locked();
        snap = g_state;
    }

    const std::string path = pmem_path();
    // Create parent directory tree (POSIX mkdir -p); avoid <filesystem>
    // because firmware's file.hpp collides with libc++ std::filesystem.
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        std::string dir = path.substr(0, slash);
        for (size_t i = 1; i <= dir.size(); ++i) {
            if (i == dir.size() || dir[i] == '/') {
                std::string sub = dir.substr(0, i);
                ::mkdir(sub.c_str(), 0755);  // errors (incl. EEXIST) ignored
            }
        }
    }

    const std::string tmp = path + ".tmp";
    std::FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (!fp) {
        std::fprintf(stderr, "[EmuHem] pmem: fopen(%s) failed: %s\n", tmp.c_str(), std::strerror(errno));
        g_dirty.store(true);  // retry later
        return;
    }
    const size_t put = std::fwrite(&snap, 1, sizeof(snap), fp);
    std::fflush(fp);
    ::fsync(::fileno(fp));
    std::fclose(fp);
    if (put != sizeof(snap)) {
        std::fprintf(stderr, "[EmuHem] pmem: short write to %s\n", tmp.c_str());
        g_dirty.store(true);
        return;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::fprintf(stderr, "[EmuHem] pmem: rename(%s -> %s) failed: %s\n",
                     tmp.c_str(), path.c_str(), std::strerror(errno));
        g_dirty.store(true);
        return;
    }
}

// Macros to cut boilerplate for the ~50 accessors.
#define PMEM_GET(T, name, field)                    \
    T name() {                                      \
        std::lock_guard<std::mutex> lk(g_mutex);    \
        ensure_loaded_locked();                     \
        return g_state.field;                       \
    }
#define PMEM_SET(T, fname, field)                   \
    void fname(T v) {                               \
        std::lock_guard<std::mutex> lk(g_mutex);    \
        ensure_loaded_locked();                     \
        write_locked(g_state.field, v);             \
    }
#define PMEM_SET_CONST_REF(T, fname, field)         \
    void fname(const T& v) {                        \
        std::lock_guard<std::mutex> lk(g_mutex);    \
        ensure_loaded_locked();                     \
        write_locked(g_state.field, v);             \
    }

}  // namespace

// ============================================================================
// Cache lifecycle
// ============================================================================

namespace cache {

void init() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        load_locked();
        g_loaded = true;
    }
    // Dev hook: EMUHEM_PMEM_TEST=<hz> overwrites target_frequency at startup
    // so the persistence round-trip can be smoke-tested without UI driving.
    // Next run's `startup target_frequency` log line should match.
    if (const char* t = std::getenv("EMUHEM_PMEM_TEST"); t && *t) {
        const int64_t freq = std::strtoll(t, nullptr, 10);
        set_target_frequency(freq);
        std::fprintf(stderr, "[EmuHem] pmem: TEST set_target_frequency(%lld)\n",
                     static_cast<long long>(freq));
    }
    std::fprintf(stderr, "[EmuHem] pmem: startup target_frequency=%lld\n",
                 static_cast<long long>(target_frequency()));
}

void persist() { save_if_dirty(); }

void defaults() {
    std::lock_guard<std::mutex> lk(g_mutex);
    apply_defaults_locked();
    g_dirty.store(true);
}

}  // namespace cache

// ============================================================================
// Accessors
// ============================================================================

PMEM_GET(bool, config_lcd_normally_black, config_lcd_normally_black)
PMEM_GET(bool, apply_fake_brightness, apply_fake_brightness)
PMEM_SET(const bool, set_apply_fake_brightness, apply_fake_brightness)
PMEM_GET(uint8_t, fake_brightness_level, fake_brightness_level)
PMEM_SET(uint8_t, set_fake_brightness_level, fake_brightness_level)

void toggle_fake_brightness_level() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    const uint8_t next = static_cast<uint8_t>((g_state.fake_brightness_level + 1) % 4);
    write_locked(g_state.fake_brightness_level, next);
}

// Frequency
rf::Frequency target_frequency() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return g_state.target_frequency;
}
void set_target_frequency(const rf::Frequency v) {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    int64_t vv = v;
    write_locked(g_state.target_frequency, vv);
}

ppb_t correction_ppb() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return g_state.correction_ppb;
}
void set_correction_ppb(const ppb_t v) {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    int32_t vv = v;
    write_locked(g_state.correction_ppb, vv);
}

// Touch calibration: return a reference to the live state slot so firmware
// can re-read after a set. Safe because g_state lives for process lifetime.
const touch::Calibration& touch_calibration() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return g_state.touch_cal;
}
void set_touch_calibration(const touch::Calibration& v) {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    write_locked(g_state.touch_cal, v);
}

// Backlight
backlight_config_t config_backlight_timer() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return g_state.backlight_timer;
}
PMEM_SET_CONST_REF(backlight_config_t, set_config_backlight_timer, backlight_timer)

// UI booleans
PMEM_GET(bool, config_splash, config_splash)
PMEM_SET(bool, set_config_splash, config_splash)
PMEM_GET(bool, hide_clock, hide_clock)
PMEM_GET(bool, clock_with_date, clock_with_date)
PMEM_SET(bool, set_clock_with_date, clock_with_date)
PMEM_GET(bool, config_login, config_login)
PMEM_SET(bool, set_config_login, config_login)
PMEM_GET(bool, config_audio_mute, config_audio_mute)
PMEM_SET(bool, set_config_audio_mute, config_audio_mute)
PMEM_GET(bool, config_speaker_disable, config_speaker_disable)
PMEM_SET(bool, set_config_speaker_disable, config_speaker_disable)
PMEM_GET(bool, stealth_mode, stealth_mode)
PMEM_SET(bool, set_stealth_mode, stealth_mode)

// CPLD
PMEM_GET(uint8_t, config_cpld, config_cpld)
PMEM_SET(uint8_t, set_config_cpld, config_cpld)

// Theme
PMEM_GET(uint8_t, ui_theme_id, ui_theme_id)
PMEM_SET(uint8_t, set_ui_theme_id, ui_theme_id)

// Modem
PMEM_GET(int32_t, modem_baudrate, modem_baudrate)
PMEM_SET(int32_t, set_modem_baudrate, modem_baudrate)
uint8_t modem_repeat() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return static_cast<uint8_t>(g_state.modem_repeat);
}
PMEM_SET(uint32_t, set_modem_repeat, modem_repeat)

// POCSAG
PMEM_GET(uint32_t, pocsag_last_address, pocsag_last_address)
PMEM_SET(uint32_t, set_pocsag_last_address, pocsag_last_address)
PMEM_GET(uint32_t, pocsag_ignore_address, pocsag_ignore_address)
PMEM_SET(uint32_t, set_pocsag_ignore_address, pocsag_ignore_address)

// Hardware
PMEM_GET(bool, config_disable_external_tcxo, config_disable_external_tcxo)
PMEM_SET(bool, set_config_disable_external_tcxo, config_disable_external_tcxo)
bool config_sdcard_high_speed_io() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return g_state.config_sdcard_high_speed_io;
}
void set_config_sdcard_high_speed_io(bool v, bool) {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    write_locked(g_state.config_sdcard_high_speed_io, v);
}

// Converter
PMEM_GET(bool, config_converter, config_converter)
PMEM_SET(bool, set_config_converter, config_converter)
PMEM_GET(int64_t, config_converter_freq, config_converter_freq)
PMEM_SET(int64_t, set_config_converter_freq, config_converter_freq)

// Freq step
PMEM_GET(uint32_t, config_freq_step, config_freq_step)
PMEM_SET(uint32_t, set_config_freq_step, config_freq_step)

// Encoder
PMEM_GET(uint32_t, config_encoder_dial_sensitivity, config_encoder_dial_sensitivity)
PMEM_SET(uint32_t, set_config_encoder_dial_sensitivity, config_encoder_dial_sensitivity)

// Antenna bias
PMEM_GET(uint8_t, config_antenna_bias, config_antenna_bias)
PMEM_SET(uint8_t, set_config_antenna_bias, config_antenna_bias)

// Touch threshold
PMEM_GET(uint16_t, touchscreen_threshold, touchscreen_threshold)
PMEM_SET(uint16_t, set_touchscreen_threshold, touchscreen_threshold)

// Menu color
Color menu_color() {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    return Color{g_state.menu_color};
}
void set_menu_color(Color v) {
    std::lock_guard<std::mutex> lk(g_mutex);
    ensure_loaded_locked();
    uint16_t vv = v.v;
    write_locked(g_state.menu_color, vv);
}

// DST
PMEM_GET(dst_config_t, config_dst, dst)
PMEM_SET(dst_config_t, set_config_dst, dst)

// FPS / brightness display
PMEM_GET(bool, config_hide_fps, config_hide_fps)
PMEM_SET(bool, set_config_hide_fps, config_hide_fps)
PMEM_GET(bool, config_show_fake_brightness, config_show_fake_brightness)
PMEM_SET(bool, set_config_show_fake_brightness, config_show_fake_brightness)

// Beep
PMEM_GET(bool, beep_on_packets, beep_on_packets)
PMEM_SET(bool, set_beep_on_packets, beep_on_packets)

// Playing dead
PMEM_GET(uint32_t, playing_dead, playing_dead)
PMEM_SET(uint32_t, set_playing_dead, playing_dead)

// SD card persistence
bool should_use_sdcard_for_pmem() { return false; }
int save_persistent_settings_to_file() { save_if_dirty(); return 0; }
int load_persistent_settings_from_file() {
    std::lock_guard<std::mutex> lk(g_mutex);
    load_locked();
    g_loaded = true;
    return 0;
}

// UI hide options
PMEM_GET(bool, ui_hide_clock, ui_hide_clock)
PMEM_SET(bool, set_ui_hide_clock, ui_hide_clock)
PMEM_GET(bool, ui_hide_speaker, ui_hide_speaker)
PMEM_SET(bool, set_ui_hide_speaker, ui_hide_speaker)
PMEM_GET(bool, ui_hide_converter, ui_hide_converter)
PMEM_SET(bool, set_ui_hide_converter, ui_hide_converter)
PMEM_GET(bool, ui_hide_stealth, ui_hide_stealth)
PMEM_SET(bool, set_ui_hide_stealth, ui_hide_stealth)
PMEM_GET(bool, ui_hide_camera, ui_hide_camera)
PMEM_SET(bool, set_ui_hide_camera, ui_hide_camera)
PMEM_GET(bool, ui_hide_sleep, ui_hide_sleep)
PMEM_SET(bool, set_ui_hide_sleep, ui_hide_sleep)
PMEM_GET(bool, ui_hide_bias_tee, ui_hide_bias_tee)
PMEM_SET(bool, set_ui_hide_bias_tee, ui_hide_bias_tee)
PMEM_GET(bool, ui_hide_sd_card, ui_hide_sd_card)
PMEM_SET(bool, set_ui_hide_sd_card, ui_hide_sd_card)
PMEM_GET(bool, ui_hide_mute, ui_hide_mute)
PMEM_SET(bool, set_ui_hide_mute, ui_hide_mute)

// Battery
PMEM_GET(uint16_t, config_battery_type, config_battery_type)
PMEM_SET(uint16_t, set_config_battery_type, config_battery_type)

// Autostart
PMEM_GET(int32_t, config_autostart_app, config_autostart_app)
PMEM_SET(int32_t, set_config_autostart_app, config_autostart_app)

// TX disable
PMEM_GET(bool, config_tx_disable, config_tx_disable)
PMEM_SET(bool, set_config_tx_disable, config_tx_disable)

PMEM_GET(bool, disable_touchscreen, disable_touchscreen)
PMEM_SET(bool, set_disable_touchscreen, disable_touchscreen)

#undef PMEM_GET
#undef PMEM_SET
#undef PMEM_SET_CONST_REF

}  // namespace persistent_memory
}  // namespace portapack
