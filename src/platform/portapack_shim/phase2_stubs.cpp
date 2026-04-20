// EmuHem Phase 2 catch-all stubs
// Provides stub implementations for all symbols needed to link the UI.
// These will be replaced with real implementations in later phases.

#include "ch.h"
#include "portapack.hpp"
#include "portapack_persistent_memory.hpp"
#include "portapack_shared_memory.hpp"
#include "message.hpp"
#include "message_queue.hpp"
#include "file.hpp"
#include "sd_card.hpp"
#include "battery.hpp"
#include "audio.hpp"
#include "radio.hpp"
#include "clock_manager.hpp"
#include "temperature_logger.hpp"
#include "receiver_model.hpp"
#include "transmitter_model.hpp"
#include "png_writer.hpp"
#include "app_settings.hpp"
#include "ui_navigation.hpp"
#include "ui_external_items_menu_loader.hpp"
#include "i2cdevmanager.hpp"
#include "string_format.hpp"
#include "i2c_pp.hpp"
#include "cpld_update.hpp"
#include "tone_key.hpp"
#include "config_mode.hpp"
#include "irq_lcd_frame.hpp"
#include "irq_rtc.hpp"
#include "ui_record_view.hpp"
#include "capture_thread.hpp"
#include "event_m0.hpp"
#include "spectrum_analysis_app.hpp"
// Headers for excluded app classes
#include "apps/ui_debug.hpp"
#include "apps/ui_settings.hpp"

#include <cstdio>

// ============================================================================
// Platform globals
// ============================================================================

bool hackrf_r9 = false;
ui::SystemView* system_view_ptr = nullptr;
EventDispatcher* event_dispatcher_ptr = nullptr;

// SDL thread pushes touch events here; main_emu.cpp's injector thread drains
// them and calls event_dispatcher_ptr->emulateTouch() one at a time (that
// call blocks until the firmware's main loop consumes the injected event).
extern "C" void emuhem_inject_touch(int32_t x, int32_t y, uint8_t type) {
    if (!event_dispatcher_ptr) return;
    ui::TouchEvent ev{
        {x, y},
        static_cast<ui::TouchEvent::Type>(type),
    };
    event_dispatcher_ptr->emulateTouch(ev);
}

// SharedMemory instance
static SharedMemory shared_memory_instance{};
SharedMemory& shared_memory = shared_memory_instance;

// MessageQueue signal -- wake M0 event loop when M4 pushes to application_queue
void MessageQueue::signal() {
    EventDispatcher::check_fifo_isr();
}

// BufferExchange -- provided by firmware's buffer_exchange.cpp

// ============================================================================
// SD card
// ============================================================================

namespace sd_card {
Status status() { return Status::Mounted; }
void poll_inserted() {}
Signal<Status> status_signal{};
FATFS fs{};
} // namespace sd_card

// ============================================================================
// Audio stubs
// ============================================================================

namespace audio {
namespace output {
void start() {}
void stop() {}
void mute() {}
void unmute() {}
void update_audio_mute() {}
void speaker_disable() {}
void speaker_enable() {}
void speaker_mute() {}
void speaker_unmute() {}
} // namespace output

namespace input {
void start(int8_t, bool) {}
void stop() {}
void loopback_mic_to_hp_enable() {}
void loopback_mic_to_hp_disable() {}
} // namespace input

namespace headphone {
volume_range_t volume_range() {
    return { volume_t::centibel(-990), volume_t::centibel(0) };
}
bool set_volume(const volume_t) { return true; }
} // namespace headphone

namespace speaker {
volume_range_t volume_range() {
    return { volume_t::centibel(-990), volume_t::centibel(0) };
}
void set_volume(const volume_t) {}
} // namespace speaker

namespace debug {
size_t reg_count() { return 0; }
uint32_t reg_read(const size_t) { return 0; }
void reg_write(const size_t, uint32_t) {}
std::string codec_name() { return "stub"; }
size_t reg_bits() { return 0; }
} // namespace debug

bool speaker_disable_supported() { return false; }
void init(audio::Codec* const) {}
void shutdown() {}
void set_rate(const Rate) {}
} // namespace audio

// ============================================================================
// Radio stubs
// ============================================================================

// Forward-declared in the global namespace so radio::set_baseband_rate below
// resolves to ::baseband::dma::set_sample_rate (defined in baseband_dma_emu.cpp)
// and NOT to ::radio::baseband::dma::set_sample_rate (which would be a new,
// undefined symbol if the forward-decl were written inside `namespace radio`).
namespace baseband { namespace dma { void set_sample_rate(uint32_t); } }

namespace radio {
// Forward tuning state to the active I/Q source (rtl_tcp client forwards as
// upstream commands; file/noise sources ignore). Bridges live in
// baseband_dma_emu.cpp to keep iq_source.hpp out of the firmware include set.
extern "C" void emuhem_iq_set_center_frequency(uint64_t hz);
extern "C" void emuhem_iq_set_tuner_gain_tenths_db(int32_t tenths_db);

void set_antenna_bias(const bool) {}
void init() {}
void set_direction(const rf::Direction) {}
bool set_tuning_frequency(const rf::Frequency f) {
    emuhem_iq_set_center_frequency(static_cast<uint64_t>(f));
    return true;
}
void set_rf_amp(const bool) {}
void set_lna_gain(const int_fast8_t db) {
    // HackRF LNA: 0..40 dB in 8 dB steps. rtl_tcp expects tenths of dB.
    emuhem_iq_set_tuner_gain_tenths_db(static_cast<int32_t>(db) * 10);
}
void set_vga_gain(const int_fast8_t) {}
void set_tx_gain(const int_fast8_t) {}
void set_baseband_filter_bandwidth_rx(const uint32_t) {}
void set_baseband_filter_bandwidth_tx(const uint32_t) {}
void set_baseband_rate(const uint32_t rate) { ::baseband::dma::set_sample_rate(rate); }
void set_tx_max283x_iq_phase_calibration(const size_t) {}
void set_rx_max283x_iq_phase_calibration(const size_t) {}
void disable() {}

namespace debug {

namespace first_if {
uint32_t register_read(const size_t) { return 0; }
void register_write(const size_t, uint32_t) {}
} // namespace first_if

namespace second_if {
uint32_t register_read(const size_t) { return 0; }
void register_write(const size_t, uint32_t) {}
int8_t temp_sense() { return 0; }
} // namespace second_if

namespace rf_path_info {
rf::path::Band get_current_band() { return rf::path::Band::Mid; }
} // namespace rf_path_info

namespace sgpio {
uint32_t register_read(const size_t) { return 0; }
} // namespace sgpio

} // namespace debug

} // namespace radio

// ============================================================================
// Battery
// ============================================================================

namespace battery {
bool BatteryManagement::isDetected() { return false; }
void BatteryManagement::set_calc_override(bool) {}
uint8_t BatteryManagement::calc_percent_voltage(uint16_t) { return 0; }
bool BatteryManagement::calcOverride = false;
void BatteryManagement::getBatteryInfo(uint8_t& valid_mask, uint8_t& percent, uint16_t& voltage, int32_t& current) {
    valid_mask = 0;
    percent = 0;
    voltage = 0;
    current = 0;
}
uint16_t BatteryManagement::get_cycles() { return 0; }
float BatteryManagement::get_tte() { return 0.0f; }
float BatteryManagement::get_ttf() { return 0.0f; }
} // namespace battery

// ============================================================================
// I2C device manager
// ============================================================================

namespace i2cdev {
void I2CDevManager::set_autoscan_interval(uint16_t) {}
uint16_t I2CDevManager::get_autoscan_interval() { return 0; }
void I2CDevManager::init() {}
void I2CDevManager::manual_scan() {}
I2cDev* I2CDevManager::get_dev_by_addr(uint8_t) { return nullptr; }
I2cDev* I2CDevManager::get_dev_by_model(I2C_DEVMDL) { return nullptr; }
std::vector<I2C_DEVMDL> I2CDevManager::get_dev_list_by_model() { return {}; }
std::vector<uint8_t> I2CDevManager::get_gev_list_by_addr() { return {}; }

uint16_t I2CDevManager::scan_interval = 0;
bool I2CDevManager::force_scan = false;
std::vector<I2DevListElement> I2CDevManager::devlist{};
Thread* I2CDevManager::thread = nullptr;
Mutex I2CDevManager::mutex_list;
EventDispatcher* I2CDevManager::_eventDispatcher = nullptr;
bool I2CDevManager::found(uint8_t) { return false; }
bool I2CDevManager::scan() { return false; }
void I2CDevManager::create_thread() {}
msg_t I2CDevManager::timer_fn(void*) { return 0; }

// I2cDev base class methods
void I2cDev::set_update_interval(uint8_t interval) { query_interval = interval; }
uint8_t I2cDev::get_update_interval() { return query_interval; }
bool I2cDev::i2c_read(uint8_t*, uint8_t, uint8_t*, uint8_t) { return false; }
bool I2cDev::i2c_write(uint8_t*, uint8_t, uint8_t*, uint8_t) { return false; }
uint8_t I2cDev::read8_1(uint8_t) { return 0; }
bool I2cDev::write8_1(uint8_t, uint8_t) { return false; }
uint16_t I2cDev::read16_1(uint8_t) { return 0; }
int16_t I2cDev::readS16_1(uint8_t) { return 0; }
uint16_t I2cDev::read16_LE_1(uint8_t) { return 0; }
int16_t I2cDev::readS16_LE_1(uint8_t) { return 0; }
uint32_t I2cDev::read24_1(uint8_t) { return 0; }
bool I2cDev::lockDevice() { return true; }
void I2cDev::unlockDevice() {}
void I2cDev::got_error() { errcnt++; }
void I2cDev::got_success() { errcnt = 0; }
} // namespace i2cdev

// ============================================================================
// I2C hardware stubs
// ============================================================================

void I2C::start(const I2CConfig&) {}
void I2C::stop() {}
bool I2C::probe(i2caddr_t, systime_t) { return false; }
bool I2C::receive(const address_t, uint8_t* const, const size_t, const systime_t) { return false; }
bool I2C::transmit(const address_t, const uint8_t* const, const size_t, const systime_t) { return false; }
bool I2C::transfer(const address_t, const uint8_t* const, const size_t,
                   uint8_t* const, const size_t, const systime_t) { return false; }

// ============================================================================
// Clock manager stubs
// ============================================================================

void ClockManager::init_clock_generator() {}
void ClockManager::shutdown() {}
void ClockManager::start_audio_pll() {}
void ClockManager::stop_audio_pll() {}
void ClockManager::set_base_audio_clock_divider(const size_t) {}
void ClockManager::enable_codec_clocks() {}
void ClockManager::disable_codec_clocks() {}
void ClockManager::enable_if_clocks() {}
void ClockManager::disable_if_clocks() {}
void ClockManager::set_sampling_frequency(const uint32_t) {}
void ClockManager::set_reference_ppb(const int32_t) {}
uint32_t ClockManager::get_frequency_monitor_measurement_in_hertz() { return 0; }
ClockManager::Reference ClockManager::get_reference() const { return reference; }
std::string ClockManager::get_source() { return "Emulator"; }
std::string ClockManager::get_freq() { return "N/A"; }
void ClockManager::enable_clock_output(bool) {}

// private methods (needed if referenced from inlined code)
void ClockManager::set_gp_clkin_to_clkin_direct() {}
void ClockManager::start_frequency_monitor_measurement(const cgu::CLK_SEL) {}
void ClockManager::wait_For_frequency_monitor_measurement_done() {}
void ClockManager::set_m4_clock_to_irc() {}
void ClockManager::set_m4_clock_to_pll1() {}
uint32_t ClockManager::measure_gp_clkin_frequency() { return 0; }
ClockManager::ReferenceSource ClockManager::detect_reference_source() { return ReferenceSource::Xtal; }
ClockManager::Reference ClockManager::choose_reference() { return reference; }
bool ClockManager::loss_of_signal() { return false; }

// ============================================================================
// Temperature logger
// ============================================================================

void TemperatureLogger::second_tick() {}

// ============================================================================
// Receiver model / Transmitter model
// NOTE: receiver_model.cpp and transmitter_model.cpp are compiled from
// firmware sources via glob. No stubs needed here -- they provide all symbols.
// ============================================================================

// PNG writer -- provided by firmware's png_writer.cpp

// Settings store -- provided by firmware's app_settings.cpp

// ============================================================================
// USBSerial stubs
// ============================================================================

void portapack::USBSerial::initialize() {}
void portapack::USBSerial::dispatch() {}
void portapack::USBSerial::dispatch_transfer() {}
void portapack::USBSerial::on_channel_opened() {}
void portapack::USBSerial::on_channel_closed() {}

// Backlight methods are provided by firmware/common/backlight.cpp (compiled)

// ============================================================================
// Missing persistent_memory functions
// (those not already in persistent_memory_emu.cpp)
// ============================================================================

namespace portapack {
namespace persistent_memory {

// Functions already in persistent_memory_emu.cpp are NOT repeated here.
// Only add functions that are declared in the header but missing from emu.

volume_t headphone_volume() { return volume_t::centibel(-600); }
void set_headphone_volume(volume_t) {}

serial_format_t serial_format() { return {}; }
void set_serial_format(const serial_format_t) {}

int32_t tone_mix() { return 0; }
void set_tone_mix(const int32_t) {}

int32_t afsk_mark_freq() { return 1200; }
void set_afsk_mark(const int32_t) {}

int32_t afsk_space_freq() { return 2200; }
void set_afsk_space(const int32_t) {}

uint32_t get_modem_def_index() { return 0; }

int32_t modem_bw() { return 0; }

bool config_disable_config_mode() { return false; }
void set_config_disable_config_mode(bool) {}

// Note: config_tx_disable (without 'd') is in persistent_memory_emu.cpp.
// The header declares config_tx_disabled (with 'd') -- a different symbol.
bool config_tx_disabled() { return false; }
void set_config_tx_disabled(bool) {}

bool config_tx_amp_disabled() { return false; }
void set_config_tx_amp_disabled(bool) {}

uint8_t config_tx_gain_max_db() { return 47; }
void set_config_tx_gain_max_db(uint8_t) {}

bool config_updown_converter() { return false; }
void set_config_updown_converter(bool) {}

bool show_gui_return_icon() { return false; }
void set_gui_return_icon(bool) {}

bool load_app_settings() { return true; }
void set_load_app_settings(bool) {}

bool save_app_settings() { return true; }
void set_save_app_settings(bool) {}

bool clkout_enabled() { return false; }
void set_clkout_enabled(bool) {}

uint16_t clkout_freq() { return 10000; }
void set_clkout_freq(uint16_t) {}

bool dst_enabled() { return false; }
void set_dst_enabled(bool) {}

void set_lcd_normally_black(bool) {}
void set_clock_hidden(bool) {}

uint8_t encoder_dial_sensitivity() { return 0; }
void set_encoder_dial_sensitivity(uint8_t) {}

uint8_t encoder_rate_multiplier() { return 1; }
void set_encoder_rate_multiplier(uint8_t) {}

bool encoder_dial_direction() { return false; }
void set_encoder_dial_direction(bool) {}

uint32_t config_mode_storage_direct() { return 0; }
void set_config_mode_storage_direct(uint32_t) {}
bool config_disable_config_mode_direct() { return true; }

bool config_freq_tx_correction_updown() { return false; }
void set_freq_tx_correction_updown(bool) {}
bool config_freq_rx_correction_updown() { return false; }
void set_freq_rx_correction_updown(bool) {}
uint32_t config_freq_tx_correction() { return 0; }
uint32_t config_freq_rx_correction() { return 0; }
void set_config_freq_tx_correction(uint32_t) {}
void set_config_freq_rx_correction(uint32_t) {}

bool ui_hide_fake_brightness() { return false; }
void set_ui_hide_fake_brightness(bool) {}
bool ui_hide_numeric_battery() { return false; }
void set_ui_hide_numeric_battery(bool) {}
bool ui_hide_battery_icon() { return false; }
void set_ui_hide_battery_icon(bool) {}
bool ui_override_batt_calc() { return false; }
void set_ui_override_batt_calc(bool) {}
bool ui_button_repeat_delay() { return false; }
void set_ui_button_repeat_delay(bool) {}
bool ui_button_repeat_speed() { return false; }
void set_ui_button_repeat_speed(bool) {}
bool ui_button_long_press_delay() { return false; }
void set_ui_button_long_press_delay(bool) {}
bool ui_battery_charge_hint() { return false; }
void set_ui_battery_charge_hint(bool) {}

// Recon app settings
uint64_t get_recon_config() { return 0; }
bool recon_autosave_freqs() { return false; }
bool recon_autostart_recon() { return true; }
bool recon_continuous() { return false; }
bool recon_clear_output() { return false; }
bool recon_load_freqs() { return true; }
bool recon_load_repeaters() { return false; }
bool recon_load_ranges() { return true; }
bool recon_update_ranges_when_recon() { return false; }
bool recon_auto_record_locked() { return false; }
bool recon_repeat_recorded() { return false; }
bool recon_repeat_recorded_file_mode() { return false; }
int8_t recon_repeat_nb() { return 0; }
int8_t recon_repeat_gain() { return 0; }
bool recon_repeat_amp() { return false; }
bool recon_load_hamradios() { return false; }
bool recon_match_mode() { return false; }
uint8_t recon_repeat_delay() { return 0; }
void set_recon_autosave_freqs(const bool) {}
void set_recon_autostart_recon(const bool) {}
void set_recon_continuous(const bool) {}
void set_recon_clear_output(const bool) {}
void set_recon_load_freqs(const bool) {}
void set_recon_load_ranges(const bool) {}
void set_recon_update_ranges_when_recon(const bool) {}
void set_recon_auto_record_locked(const bool) {}
void set_recon_repeat_recorded(const bool) {}
void set_recon_repeat_recorded_file_mode(const bool) {}
void set_recon_repeat_nb(const int8_t) {}
void set_recon_repeat_gain(const int8_t) {}
void set_recon_repeat_amp(const bool) {}
void set_recon_load_hamradios(const bool) {}
void set_recon_load_repeaters(const bool) {}
void set_recon_match_mode(const bool) {}
void set_recon_repeat_delay(const uint8_t) {}

uint32_t get_data_structure_version() { return 0; }
uint32_t pmem_data_word(uint32_t) { return 0; }
uint32_t pmem_stored_checksum() { return 0; }
uint32_t pmem_calculated_checksum() { return 0; }

void set_battery_cap_mah(uint16_t) {}
uint32_t battery_cap_mah() { return BATTERY_DESIGN_CAP; }
bool battery_cap_valid() { return false; }

size_t data_size() { return PMEM_SIZE_BYTES; }

} // namespace persistent_memory
} // namespace portapack

// External app loader -- provided by firmware's ui_external_items_menu_loader.cpp
// NotificationView -- provided by firmware's ui_notifications.cpp
// SDCardStatusView -- provided by firmware's ui_sd_card_status_view.cpp
// load_blacklist -- provided by firmware's ui_btngrid.cpp

namespace ui {

// RecordView now compiled from firmware's ui_record_view.cpp

} // namespace ui

// SpectrumAnalysisModel stub (uses removed API, kept excluded)
SpectrumAnalysisModel::SpectrumAnalysisModel() {}

// ============================================================================
// EventDispatcher & MessageHandlerRegistration
// (event_m0.cpp IS compiled via glob, but we may still need these if it's
//  excluded or has unresolved deps -- they'll be weak/duplicate-safe here
//  only if event_m0.cpp is actually not linked)
// ============================================================================

// MessageHandlerRegistration is always needed (event_m0.cpp may be compiled
// but sometimes the linker still needs these from the stubs translation unit)

// ============================================================================
// CPLD stubs (cpld_update.cpp, cpld_max5.cpp, cpld_xilinx.cpp are excluded)
// ============================================================================

namespace portapack {
namespace cpld {
CpldUpdateStatus update_if_necessary(const Config) { return CpldUpdateStatus::Success; }
CpldUpdateStatus update_autodetect(const Config, const Config) { return CpldUpdateStatus::Success; }
} // namespace cpld
} // namespace portapack

namespace hackrf {
namespace cpld {
bool load_sram() { return true; }
void load_sram_no_verify() {}
bool verify_eeprom() { return true; }
void init_from_eeprom() {}
} // namespace cpld
} // namespace hackrf

// ============================================================================
// Tone key stubs (tone_key.cpp is excluded)
// ============================================================================

namespace tonekey {

const tone_key_t tone_keys = {
    {"None", 0},
};

float tone_key_frequency(tone_index) { return 0.0f; }
std::string fx100_string(uint32_t) { return "0.0"; }
std::string tone_key_string(tone_index) { return ""; }
std::string tone_key_value_string(tone_index) { return ""; }
std::string tone_key_string_by_value(uint32_t, size_t) { return ""; }
tone_index tone_key_index_by_value(uint32_t) { return -1; }

} // namespace tonekey

// ============================================================================
// config_mode stubs (config_mode.cpp is excluded)
// ============================================================================

void config_mode_set() {}
bool config_mode_should_enter() { return false; }
void config_mode_clear() {}
void config_mode_enable(bool) {}
bool config_mode_disabled() { return true; }
void config_mode_run() {}

// ============================================================================
// IRQ stubs (irq_lcd_frame.cpp and irq_rtc.cpp are excluded)
// ============================================================================

void lcd_frame_sync_configure() {}
void rtc_interrupt_enable() {}

// split_string -- provided by firmware's file_reader.cpp

// ============================================================================
// CPLD data arrays (portapack_cpld_data.cpp and hackrf_cpld_data.cpp are
// excluded -- provide empty arrays so the Config references link)
// ============================================================================

namespace portapack {
namespace cpld {
namespace rev_20150901 {
const std::array<uint16_t, 3328> block_0{};
const std::array<uint16_t, 512> block_1{};
} // namespace rev_20150901
namespace rev_20170522 {
const std::array<uint16_t, 3328> block_0{};
const std::array<uint16_t, 512> block_1{};
} // namespace rev_20170522
} // namespace cpld
} // namespace portapack

// hackrf::one::cpld::verify_blocks -- omitted, CPLD type not available in emulator

// ============================================================================
// Si5351 stubs (si5351.cpp is excluded -- only out-of-line methods)
// ============================================================================

#include "si5351.hpp"

namespace si5351 {
void Si5351::reset() {}
void Si5351::set_ms_frequency(const size_t, const uint32_t, const uint32_t, const size_t) {}
Si5351::regvalue_t Si5351::read_register(const uint8_t) { return 0; }
} // namespace si5351

// ============================================================================
// Additional stubs for remaining linker symbols (round 2)
// ============================================================================

// USB serial stubs
#include "usb_serial_device_to_host.h"
extern "C" {
SerialUSBDriver SUSBD1{};
void init_serial_usb_driver(SerialUSBDriver*) {}
size_t fillOBuffer(OutputQueue*, const uint8_t*, size_t) { return 0; }
void usb_serial_active_input_handler(void) {}
void complete_i2chost_to_device_transfer(void) {}
void create_shell_i2c(void) {}
uint32_t __heap_base__ = 0;
uint32_t __heap_end__ = 0;
}

// UsbSerialAsyncmsg -- template specialization doesn't work easily,
// provide a non-template version and hope the linker deduplicates
namespace UsbSerialAsyncmsg {
void asyncmsg(const std::string&) {}
}

// TemperatureLogger
size_t TemperatureLogger::capacity() const { return 0; }
std::vector<TemperatureLogger::sample_t> TemperatureLogger::history() const { return {}; }

// CPLD stubs
#include "cpld_max5.hpp"
#include "cpld_xilinx.hpp"

namespace cpld {
namespace max5 {
bool CPLD::idcode_ok() { return true; }
bool CPLD::silicon_id_ok() { return true; }
void CPLD::enable() {}
void CPLD::disable() {}
void CPLD::bypass() {}
void CPLD::sample() {}
void CPLD::sample(std::bitset<240>&) {}
void CPLD::extest(std::bitset<240>&) {}
void CPLD::clamp() {}
bool CPLD::program(const std::array<uint16_t, 3328>&, const std::array<uint16_t, 512>&) { return true; }
bool CPLD::verify(const std::array<uint16_t, 3328>&, const std::array<uint16_t, 512>&) { return true; }
void CPLD::prepare_read(uint16_t) {}
uint32_t CPLD::read() { return 0; }
bool CPLD::is_blank() { return true; }
uint32_t CPLD::crc() { return 0; }
uint32_t CPLD::get_idcode() { return 0; }
uint32_t CPLD::usercode() { return 0; }
void CPLD::bulk_erase() {}
std::pair<bool, uint8_t> CPLD::boundary_scan() { return {false, 0}; }
bool CPLD::AGM_enter_maintenance_mode() { return false; }
void CPLD::AGM_exit_maintenance_mode() {}
void CPLD::AGM_enter_read_mode() {}
uint32_t CPLD::AGM_encode_address(uint32_t, uint32_t) { return 0; }
uint32_t CPLD::AGM_read(uint32_t) { return 0; }
void CPLD::AGM_write(const std::array<uint32_t, 1801>&, uint32_t) {}
} // namespace max5

namespace xilinx {
void XC2C64A::write_sram(const verify_blocks_t&) {}
bool XC2C64A::verify_sram(const verify_blocks_t&) { return true; }
bool XC2C64A::verify_eeprom(const verify_blocks_t&) { return true; }
void XC2C64A::init_from_eeprom() {}
} // namespace xilinx
} // namespace cpld

// hackrf verify_blocks data
namespace hackrf {
namespace one {
namespace cpld {
const ::cpld::xilinx::XC2C64A::verify_blocks_t verify_blocks{};
} // namespace cpld
} // namespace one
} // namespace hackrf

// RFFC507x
#include "rffc507x.hpp"
void rffc507x::RFFC507x::init() {}

// freqman UI functions
// freqman functions now compiled from firmware's ui_freqman.cpp

// tonekey populate
namespace tonekey {
void tone_keys_populate(ui::OptionsField&) {}
} // namespace tonekey

// ui::Audio widget (vtable anchor + paint for vtable)
namespace ui {
void Audio::on_statistics_update(const AudioStatistics&) {}
void Audio::paint(Painter&) {}

} // namespace ui

// PlaylistView now compiled from firmware

// App views missing from app_stubs.cpp
#include "ui_freqman.hpp"
// FreqManBaseView, DebugMenuView etc. now compiled from firmware sources
// ui_settings.cpp, ui_playlist.cpp, ui_record_view.cpp now compiled from firmware sources

// SetBatteryView now compiled from firmware's ui_settings.cpp
