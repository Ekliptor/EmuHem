cmake_minimum_required(VERSION 3.25)
project(EmuHem LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Emulator defines
add_definitions(-DEMUHEM=1)
add_definitions(-DLOCATE_IN_RAM=)
add_definitions(-DLPC43XX_M0=1)
add_definitions(-DLPC_BACKUP_REG_BASE=0x40041000)
add_definitions(-DLPC_SPIFI_DATA_BASE=0x14000000)
add_definitions(-DLPC_SPIFI_DATA_CACHED_BASE=0x80000000)
add_definitions(-DFLASH_SIZE_MB=1)
add_definitions(-DCORTEX_M4=0)
add_definitions(-DVERSION_STRING="EmuHem")
add_definitions(-DFLASH_SIZE_LIMIT_MB=1)
add_definitions(-DVERSION_MD5=0)
# Fix missing errno include in firmware's convert.hpp
add_definitions(-include errno.h)

# ---------------------------------------------------------------------------
# SDL3
# ---------------------------------------------------------------------------
find_package(SDL3 REQUIRED)

# ---------------------------------------------------------------------------
# Source paths
# ---------------------------------------------------------------------------
set(EMU_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(EMU_PLATFORM_DIR ${EMU_SRC_DIR}/platform)
set(FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mayhem-firmware/firmware)
set(HACKRF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hackrf/firmware/common)

# ---------------------------------------------------------------------------
# Patched firmware headers
# Some firmware headers contain GCC-only syntax that Clang rejects.
# We create patched copies at configure time in the build directory.
# ---------------------------------------------------------------------------
set(PATCHED_FW_DIR ${CMAKE_CURRENT_BINARY_DIR}/patched_fw)
file(MAKE_DIRECTORY ${PATCHED_FW_DIR})

# message.hpp: Fix GNU old-style designator on non-aggregate type
#   Message{id : ID::SSTVRXConfigure}  →  Message{ID::SSTVRXConfigure}
file(READ ${FW_DIR}/common/message.hpp _MSG_CONTENT)
string(REPLACE "Message{id : ID::SSTVRXConfigure}" "Message{ID::SSTVRXConfigure}" _MSG_CONTENT "${_MSG_CONTENT}")
string(REPLACE "std::max(1U, percent)" "std::max(size_t{1}, percent)" _MSG_CONTENT "${_MSG_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/message.hpp "${_MSG_CONTENT}")

# ui_widget.cpp: Fix narrowing conversion and std::max type mismatch on 64-bit
file(READ ${FW_DIR}/common/ui_widget.cpp _UIW_CONTENT)
string(REPLACE "std::max<uint32_t>(length, 1)" "std::max<size_t>(length, 1)" _UIW_CONTENT "${_UIW_CONTENT}")
string(REPLACE "std::max(1UL, parent_rect.size().height() / LEDs)" "std::max(1, (int)(parent_rect.size().height() / LEDs))" _UIW_CONTENT "${_UIW_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_widget.cpp "${_UIW_CONTENT}")

# ui_playlist.cpp: Remove <fstream> include that clashes with firmware's std::filesystem::path
file(READ ${FW_DIR}/application/apps/ui_playlist.cpp _PLAYLIST_CONTENT)
string(REPLACE "#include <fstream>" "// #include <fstream>  // removed for emulator (clashes with firmware's std::filesystem)" _PLAYLIST_CONTENT "${_PLAYLIST_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_playlist.cpp "${_PLAYLIST_CONTENT}")

# ui_debug.cpp: Fix std::max type mismatch on 64-bit
file(READ ${FW_DIR}/application/apps/ui_debug.cpp _DEBUG_CONTENT)
string(REPLACE "std::max(0ul," "std::max((size_t)0," _DEBUG_CONTENT "${_DEBUG_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_debug.cpp "${_DEBUG_CONTENT}")

# tone_key.cpp: excluded (abs ambiguity), stubs provided

# ui_freqman.cpp: Fix clip type mismatch
file(READ ${FW_DIR}/application/apps/ui_freqman.cpp _FREQMAN_CONTENT)
string(REPLACE "clip(current_category_index, 0u," "clip<size_t>(current_category_index, 0," _FREQMAN_CONTENT "${_FREQMAN_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_freqman.cpp "${_FREQMAN_CONTENT}")

# ui_settings.cpp: Fix TRUE identifier
file(READ ${FW_DIR}/application/apps/ui_settings.cpp _SETTINGS_CONTENT)
string(REPLACE " TRUE" " true" _SETTINGS_CONTENT "${_SETTINGS_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_settings.cpp "${_SETTINGS_CONTENT}")

# ui_view_wav.cpp: Fix std::min type mismatch
file(READ ${FW_DIR}/application/external/wav_view/ui_view_wav.cpp _WAVVIEW_CONTENT)
string(REPLACE "std::min(" "std::min<size_t>(" _WAVVIEW_CONTENT "${_WAVVIEW_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_view_wav.cpp "${_WAVVIEW_CONTENT}")

# Copy patched app files in-place so same-directory includes work
file(COPY_FILE ${PATCHED_FW_DIR}/ui_playlist.cpp ${FW_DIR}/application/apps/ui_playlist.cpp ONLY_IF_DIFFERENT)
file(COPY_FILE ${PATCHED_FW_DIR}/ui_debug.cpp ${FW_DIR}/application/apps/ui_debug.cpp ONLY_IF_DIFFERENT)
# tone_key.cpp excluded
file(COPY_FILE ${PATCHED_FW_DIR}/ui_freqman.cpp ${FW_DIR}/application/apps/ui_freqman.cpp ONLY_IF_DIFFERENT)
file(COPY_FILE ${PATCHED_FW_DIR}/ui_settings.cpp ${FW_DIR}/application/apps/ui_settings.cpp ONLY_IF_DIFFERENT)
file(COPY_FILE ${PATCHED_FW_DIR}/ui_view_wav.cpp ${FW_DIR}/application/external/wav_view/ui_view_wav.cpp ONLY_IF_DIFFERENT)

# ui_record_view.cpp: Fix std::min type mismatch
file(READ ${FW_DIR}/application/ui_record_view.cpp _RECVIEW_CONTENT)
string(REPLACE "std::min(" "std::min<size_t>(" _RECVIEW_CONTENT "${_RECVIEW_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_record_view.cpp "${_RECVIEW_CONTENT}")
file(COPY_FILE ${PATCHED_FW_DIR}/ui_record_view.cpp ${FW_DIR}/application/ui_record_view.cpp ONLY_IF_DIFFERENT)

# spi_image.hpp: Fix reinterpret_cast from pointer to uint32_t on 64-bit
file(READ ${FW_DIR}/common/spi_image.hpp _SPI_CONTENT)
string(REPLACE "reinterpret_cast<uint32_t>(&_textend)" "0U" _SPI_CONTENT "${_SPI_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/spi_image.hpp "${_SPI_CONTENT}")

# ui_receiver.hpp: Fix constexpr pow() collision with std::pow on desktop
file(READ ${FW_DIR}/application/ui/ui_receiver.hpp _UIRX_CONTENT)
string(REPLACE "pow(10, mhz_digits)" "10000" _UIRX_CONTENT "${_UIRX_CONTENT}")
string(REPLACE "pow(10, 6 - submhz_digits)" "100" _UIRX_CONTENT "${_UIRX_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_receiver.hpp "${_UIRX_CONTENT}")

# ui_adsb_rx.hpp: Fix ambiguous abs() call
file(READ ${FW_DIR}/application/apps/ui_adsb_rx.hpp _ADSB_CONTENT)
string(REPLACE "abs(frame_pos_even.get_rx_timestamp() - frame_pos_odd.get_rx_timestamp())" "std::abs(static_cast<int64_t>(frame_pos_even.get_rx_timestamp() - frame_pos_odd.get_rx_timestamp()))" _ADSB_CONTENT "${_ADSB_CONTENT}")
file(WRITE ${PATCHED_FW_DIR}/ui_adsb_rx.hpp "${_ADSB_CONTENT}")

# For headers that are included from same-directory source files, we must
# patch them in-place because #include "file.hpp" resolves to the same
# directory first, before any -I path.
# These patches are idempotent (safe to re-apply).
file(COPY_FILE ${PATCHED_FW_DIR}/ui_receiver.hpp ${FW_DIR}/application/ui/ui_receiver.hpp ONLY_IF_DIFFERENT)
file(COPY_FILE ${PATCHED_FW_DIR}/ui_adsb_rx.hpp ${FW_DIR}/application/apps/ui_adsb_rx.hpp ONLY_IF_DIFFERENT)

# ---------------------------------------------------------------------------
# Phase 3: Baseband processor patches
# Rename M4's EventDispatcher → BasebandEventDispatcher to avoid ODR
# collision with M0's EventDispatcher (both compile in same binary).
# ---------------------------------------------------------------------------

# event_m4.hpp: Rename EventDispatcher -> BasebandEventDispatcher
file(READ ${FW_DIR}/baseband/event_m4.hpp _EVTM4H)
string(REPLACE "EventDispatcher" "BasebandEventDispatcher" _EVTM4H "${_EVTM4H}")
file(WRITE ${PATCHED_FW_DIR}/event_m4.hpp "${_EVTM4H}")

# event_m4.cpp: Same rename + bridge for M0→M4 signaling
file(READ ${FW_DIR}/baseband/event_m4.cpp _EVTM4C)
# Use unique sentinel to avoid double-replacement: EventDispatcher → _BB_EVDISP_ → BasebandEventDispatcher
string(REPLACE "EventDispatcher" "_BB_EVDISP_" _EVTM4C "${_EVTM4C}")
string(REPLACE "_BB_EVDISP_" "BasebandEventDispatcher" _EVTM4C "${_EVTM4C}")
# Add g_m4_event_thread bridge after thread_event_loop assignment
string(REPLACE "thread_event_loop = chThdSelf();" "thread_event_loop = chThdSelf();\n    extern Thread* g_m4_event_thread;\n    g_m4_event_thread = thread_event_loop;" _EVTM4C "${_EVTM4C}")
# Fix ShutdownMessage not clearing baseband_message (firmware bug without PRALINE)
string(REPLACE "on_message_shutdown(*reinterpret_cast<const ShutdownMessage*>(message));" "on_message_shutdown(*reinterpret_cast<const ShutdownMessage*>(message));\n            shared_memory.baseband_message = nullptr;" _EVTM4C "${_EVTM4C}")
file(WRITE ${PATCHED_FW_DIR}/event_m4.cpp "${_EVTM4C}")

# spectrum_collector.cpp: Update EventDispatcher reference
file(READ ${FW_DIR}/baseband/spectrum_collector.cpp _SPCOL)
string(REPLACE "EventDispatcher" "BasebandEventDispatcher" _SPCOL "${_SPCOL}")
file(WRITE ${PATCHED_FW_DIR}/spectrum_collector.cpp "${_SPCOL}")

# proc_wideband_spectrum.cpp: Remove main() function (conflicts with main_emu.cpp)
file(READ ${FW_DIR}/baseband/proc_wideband_spectrum.cpp _PWBS)
string(REPLACE
    "int main() {\n    audio::dma::init_audio_out();  // for AudioRX app (enables audio output while this baseband image is running)\n    EventDispatcher event_dispatcher{std::make_unique<WidebandSpectrum>()};\n    event_dispatcher.run();\n    return 0;\n}"
    "// main() removed for EmuHem -- processor is instantiated via registry"
    _PWBS "${_PWBS}")
file(WRITE ${PATCHED_FW_DIR}/proc_wideband_spectrum.cpp "${_PWBS}")

# ---------------------------------------------------------------------------
# Phase 3.5: Audio processor patches
# Drop M4-only guard from simd.hpp/utility_m4.hpp so the DSP helpers compile
# in the unified binary. Strip main() from the three audio processor .cpp
# files (they conflict with main_emu.cpp).
# ---------------------------------------------------------------------------

# simd.hpp: drop LPC43XX_M4 guard (ARM intrinsics are stubbed in lpc43xx_cpp.hpp)
file(READ ${FW_DIR}/common/simd.hpp _SIMD_C)
string(REPLACE "#if defined(LPC43XX_M4)" "#if 1  // EmuHem: always compile" _SIMD_C "${_SIMD_C}")
file(WRITE ${PATCHED_FW_DIR}/simd.hpp "${_SIMD_C}")
file(COPY_FILE ${PATCHED_FW_DIR}/simd.hpp ${FW_DIR}/common/simd.hpp ONLY_IF_DIFFERENT)

# utility_m4.hpp: drop LPC43XX_M4 guard
file(READ ${FW_DIR}/common/utility_m4.hpp _UM4_C)
string(REPLACE "#if defined(LPC43XX_M4)" "#if 1  // EmuHem: always compile" _UM4_C "${_UM4_C}")
file(WRITE ${PATCHED_FW_DIR}/utility_m4.hpp "${_UM4_C}")
file(COPY_FILE ${PATCHED_FW_DIR}/utility_m4.hpp ${FW_DIR}/common/utility_m4.hpp ONLY_IF_DIFFERENT)

# proc_am_audio.cpp: Remove main() and demote constexpr locals that touch
# instance-member static constexpr values (Clang is stricter than GCC here).
file(READ ${FW_DIR}/baseband/proc_am_audio.cpp _PAM)
string(REPLACE
    "int main() {\n    audio::dma::init_audio_out();\n\n    EventDispatcher event_dispatcher{std::make_unique<NarrowbandAMAudio>()};\n    event_dispatcher.run();\n    return 0;\n}"
    "// main() removed for EmuHem -- processor is instantiated via registry"
    _PAM "${_PAM}")
string(REPLACE "constexpr size_t " "const size_t " _PAM "${_PAM}")
file(WRITE ${PATCHED_FW_DIR}/proc_am_audio.cpp "${_PAM}")

# proc_nfm_audio.cpp: Remove main() and demote constexpr locals
file(READ ${FW_DIR}/baseband/proc_nfm_audio.cpp _PNFM)
string(REPLACE
    "int main() {\n    audio::dma::init_audio_out();\n\n    EventDispatcher event_dispatcher{std::make_unique<NarrowbandFMAudio>()};\n    event_dispatcher.run();\n    return 0;\n}"
    "// main() removed for EmuHem -- processor is instantiated via registry"
    _PNFM "${_PNFM}")
string(REPLACE "constexpr size_t " "const size_t " _PNFM "${_PNFM}")
file(WRITE ${PATCHED_FW_DIR}/proc_nfm_audio.cpp "${_PNFM}")

# proc_wfm_audio.cpp: Remove main() and demote constexpr locals
file(READ ${FW_DIR}/baseband/proc_wfm_audio.cpp _PWFM)
string(REPLACE
    "int main() {\n    audio::dma::init_audio_out();\n\n    EventDispatcher event_dispatcher{std::make_unique<WidebandFMAudio>()};\n    event_dispatcher.run();\n    return 0;\n}"
    "// main() removed for EmuHem -- processor is instantiated via registry"
    _PWFM "${_PWFM}")
string(REPLACE "constexpr size_t " "const size_t " _PWFM "${_PWFM}")
file(WRITE ${PATCHED_FW_DIR}/proc_wfm_audio.cpp "${_PWFM}")

# audio_compressor.hpp: libc++ treats std::pow/log10/exp as non-constexpr.
# Swap those static constexpr floats to inline const, and make tau_alpha
# non-constexpr. The call sites only need runtime init.
file(READ ${FW_DIR}/baseband/audio_compressor.hpp _ACMP)
string(REPLACE
    "static constexpr float lin_floor = std::pow(10.0f, db_floor / 20.0f);"
    "static inline const float lin_floor = std::pow(10.0f, db_floor / 20.0f);"
    _ACMP "${_ACMP}")
string(REPLACE
    "static constexpr float log2_db_k = 20.0f * std::log10(2.0f);"
    "static inline const float log2_db_k = 20.0f * std::log10(2.0f);"
    _ACMP "${_ACMP}")
string(REPLACE
    "    static constexpr float tau_alpha(const float tau, const float fs) {"
    "    static float tau_alpha(const float tau, const float fs) {"
    _ACMP "${_ACMP}")
file(WRITE ${PATCHED_FW_DIR}/audio_compressor.hpp "${_ACMP}")
file(COPY_FILE ${PATCHED_FW_DIR}/audio_compressor.hpp ${FW_DIR}/baseband/audio_compressor.hpp ONLY_IF_DIFFERENT)

# audio_compressor.cpp: constexpr makeup_gain uses std::pow (not constexpr on libc++)
file(READ ${FW_DIR}/baseband/audio_compressor.cpp _ACMPC)
string(REPLACE
    "    constexpr float makeup_gain = std::pow(10.0f, (threshold - (threshold / ratio)) / -20.0f);"
    "    const float makeup_gain = std::pow(10.0f, (threshold - (threshold / ratio)) / -20.0f);"
    _ACMPC "${_ACMPC}")
file(WRITE ${PATCHED_FW_DIR}/audio_compressor.cpp "${_ACMPC}")
file(COPY_FILE ${PATCHED_FW_DIR}/audio_compressor.cpp ${FW_DIR}/baseband/audio_compressor.cpp ONLY_IF_DIFFERENT)

# event_m4.hpp: NOT copied in-place. Patched files in PATCHED_FW_DIR include it
# from their same directory. Other code uses PATCHED_FW_DIR via -I path.

# Replace lpc43xx_cpp.hpp in-place so same-directory includes from common/ find our version
file(COPY_FILE ${EMU_PLATFORM_DIR}/lpc43xx/lpc43xx_cpp.hpp ${FW_DIR}/common/lpc43xx_cpp.hpp ONLY_IF_DIFFERENT)
file(COPY_FILE ${PATCHED_FW_DIR}/message.hpp ${FW_DIR}/common/message.hpp ONLY_IF_DIFFERENT)
file(COPY_FILE ${EMU_PLATFORM_DIR}/portapack_shim/usb_serial_device_to_host.h ${FW_DIR}/application/usb_serial_device_to_host.h ONLY_IF_DIFFERENT)

# ---------------------------------------------------------------------------
# Include directories
# Order matters: our shims MUST come first to override ChibiOS headers.
# ---------------------------------------------------------------------------
set(EMU_INCLUDE_DIRS
    # Our shims (highest priority -- must shadow firmware headers)
    ${EMU_PLATFORM_DIR}/portapack_shim
    ${EMU_PLATFORM_DIR}/fatfs_shim
    ${EMU_PLATFORM_DIR}/chibios_shim
    ${EMU_PLATFORM_DIR}/lpc43xx

    # Patched firmware headers (shadow originals)
    ${PATCHED_FW_DIR}

    # Firmware common and application headers
    ${FW_DIR}/common
    ${FW_DIR}/application
    ${FW_DIR}/application/hw
    ${FW_DIR}/application/apps
    ${FW_DIR}/application/ui
    ${FW_DIR}/application/protocols
    ${FW_DIR}/application/external
    ${FW_DIR}/baseband

    # HackRF headers (for platform_detect.h, hackrf_core.h, etc.)
    ${HACKRF_DIR}
)

# ---------------------------------------------------------------------------
# Phase 2: Emulator sources
# ---------------------------------------------------------------------------
set(EMU_SOURCES
    # Entry point
    ${EMU_SRC_DIR}/main_emu.cpp

    # ChibiOS shim
    ${EMU_PLATFORM_DIR}/chibios_shim/chibios_shim.cpp
    ${EMU_PLATFORM_DIR}/chibios_shim/hal_instances.cpp

    # FatFS shim
    ${EMU_PLATFORM_DIR}/fatfs_shim/ff_emu.cpp

    # Portapack shims
    ${EMU_PLATFORM_DIR}/portapack_shim/portapack_io_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/portapack_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/persistent_memory_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/core_control_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/debug_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/irq_controls_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/rtc_time_emu.cpp

    # Phase 3: Baseband shims
    ${EMU_PLATFORM_DIR}/portapack_shim/baseband_dma_emu.cpp
    ${EMU_PLATFORM_DIR}/portapack_shim/baseband_hw_emu.cpp

    # Phase 5: I/Q source (file + noise)
    ${EMU_PLATFORM_DIR}/portapack_shim/iq_source.cpp

    # Phase 3: Baseband firmware sources (patched)
    ${FW_DIR}/baseband/baseband_processor.cpp
    ${FW_DIR}/baseband/baseband_thread.cpp
    ${FW_DIR}/baseband/rssi_thread.cpp
    ${PATCHED_FW_DIR}/event_m4.cpp
    ${PATCHED_FW_DIR}/spectrum_collector.cpp
    ${PATCHED_FW_DIR}/proc_wideband_spectrum.cpp

    # Phase 3.5: Audio baseband DSP + processors
    ${FW_DIR}/baseband/dsp_decimate.cpp
    ${FW_DIR}/baseband/dsp_demodulate.cpp
    ${FW_DIR}/baseband/dsp_hilbert.cpp
    ${FW_DIR}/baseband/dsp_modulate.cpp
    ${FW_DIR}/common/dsp_iir.cpp
    ${FW_DIR}/baseband/audio_output.cpp
    ${FW_DIR}/baseband/audio_compressor.cpp
    ${FW_DIR}/baseband/audio_stats_collector.cpp
    ${PATCHED_FW_DIR}/proc_am_audio.cpp
    ${PATCHED_FW_DIR}/proc_nfm_audio.cpp
    ${PATCHED_FW_DIR}/proc_wfm_audio.cpp

    # Firmware sources: common layer (compiled as-is)
    ${FW_DIR}/common/lcd_ili9341.cpp
    ${FW_DIR}/common/utility.cpp
    ${FW_DIR}/common/ui.cpp
    ${FW_DIR}/common/ui_text.cpp
    ${FW_DIR}/common/ui_painter.cpp
    ${PATCHED_FW_DIR}/ui_widget.cpp
    ${FW_DIR}/common/ui_focus.cpp
    ${FW_DIR}/common/backlight.cpp
    ${FW_DIR}/common/lfsr_random.cpp
    # portapack_io.cpp, hackrf_hal.cpp etc. excluded -- replaced by shims
)

# Firmware sources: application layer (glob all .cpp, exclude replaced files)
file(GLOB_RECURSE FW_APP_SOURCES
    ${FW_DIR}/application/*.cpp
)

# Remove firmware main() -- replaced by main_emu.cpp
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "/main\\.cpp$")
# Remove files we replace with emulator versions
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "portapack\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "irq_controls\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "irq_lcd_frame\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "irq_rtc\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "radio\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "core_control\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "sd_card\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "audio\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "rtc_time\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "portapack_persistent_memory\\.cpp$")
# Remove USB-related files that need the real ChibiOS USB stack
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "usb_serial\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "usb_serial_shell.*\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "usb_serial_cdc\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "usb_serial_asyncmsg\\.cpp$")
# spectrum_analysis_app.cpp: uses removed API set_baseband_configuration, keep excluded
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "spectrum_analysis_app\\.cpp$")
# ui_debug.cpp and ui_freqman.cpp now patched in-place and compiled
# Files that pull in <fstream> which clashes with firmware's std::filesystem::path
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "random_password")
# Files that access hardware directly (we have stubs)
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "clock_manager\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "config_mode\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "debug\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "temperature_logger\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "hackrf_hal\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "i2cdevmanager\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "battery\\.cpp$")
# Files that use RF chip drivers directly
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "si5351\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "rffc507x\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "max283x\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "max5864\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "wm8731\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ak4951\\.cpp$")
# Files with compatibility issues
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ui_sigfrx\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ui_test\\.cpp$")
# Hardware driver files (hw/ subdirectory, CPLD, JTAG)
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "/hw/")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "cpld_.*\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "jtag.*\\.cpp$")
# Misc
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "shell\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "tone_key\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "usb_serial_host_to_device\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "usb_serial_thread\\.cpp$")
# Remove standalone apps (different build target)
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "/standalone/")
# Remove external app main.cpp files (use section attributes incompatible with mach-o)
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "/external/.*/main\\.cpp$")
# Remove external apps with ChibiOS thread APIs not shimmed
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ui_morse\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ui_morse_radiotx\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ui_hopper\\.cpp$")
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "ui_sd_wipe\\.cpp$")
# Remove test files
list(FILTER FW_APP_SOURCES EXCLUDE REGEX "/test/")

# Firmware sources: common layer (glob remaining .cpp files we haven't listed)
file(GLOB FW_COMMON_SOURCES ${FW_DIR}/common/*.cpp)
# Remove files already in EMU_SOURCES or replaced by shims
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "lcd_ili9341\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "utility\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "ui\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "ui_text\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "ui_painter\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "ui_widget\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "ui_focus\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "backlight\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "lfsr_random\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "portapack_persistent_memory\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "portapack_shared_memory\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "portapack_io\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "hackrf_hal\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "message_queue\\.cpp$")
# Exclude hardware-specific files (stubs in phase2_stubs.cpp)
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "battery\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "i2cdevmanager\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "cpld_update\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "gpdma\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "cpld_max5\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "cpld_xilinx\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "i2c_pp\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "spi_pp\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "dsp_.*\\.cpp$")  # DSP is for baseband -- added explicitly in Phase 3.5
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "baseband_sgpio\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "baseband_dma\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "audio_dma\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "jammer\\.cpp$")
list(FILTER FW_COMMON_SOURCES EXCLUDE REGEX "msgpack\\.cpp$")

list(APPEND EMU_SOURCES ${FW_COMMON_SOURCES})
list(APPEND EMU_SOURCES ${FW_APP_SOURCES}

    # Catch-all stubs for remaining symbols
    ${EMU_PLATFORM_DIR}/portapack_shim/phase2_stubs.cpp
    # app_stubs.cpp removed -- real firmware sources now compiled via glob

    # Platform stubs
    ${EMU_PLATFORM_DIR}/lpc43xx/platform_detect_emu.c
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(emuhem ${EMU_SOURCES})

target_include_directories(emuhem PRIVATE ${EMU_INCLUDE_DIRS})

# Phase 3.5: Scope PRALINE define to just the AM/NFM audio processor TUs.
# Their headers use PRALINE to select the Phase-2 manual-thread-start pattern
# (auto_start=false + explicit .start() in the ctor body). WidebandFMAudio has
# no PRALINE guard and auto-starts its threads like WidebandSpectrum.
set_source_files_properties(
    ${PATCHED_FW_DIR}/proc_am_audio.cpp
    ${PATCHED_FW_DIR}/proc_nfm_audio.cpp
    PROPERTIES COMPILE_DEFINITIONS "PRALINE"
)

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(emuhem PRIVATE SDL3::SDL3)

# Allow remaining undefined symbols for excluded apps' vtables + USB serial
# (not called at runtime -- apps are stubs)
if(APPLE)
    target_link_options(emuhem PRIVATE -Wl,-undefined,dynamic_lookup)
endif()

if(APPLE)
    # macOS frameworks
    target_link_libraries(emuhem PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
elseif(WIN32)
    target_link_libraries(emuhem PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(emuhem PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
        -Wno-missing-field-initializers -Wno-sign-compare
        -Wno-macro-redefined -Wno-deprecated-literal-operator
        -Wno-gnu-designator -Wno-braced-scalar-init
        -Wno-deprecated-volatile -Wno-unused-private-field
        -Wno-c++11-narrowing
        -Wno-pointer-to-int-cast
        # Allow reinterpret_cast from pointer to smaller int (64-bit compat)
        -fms-extensions
        -Wno-invalid-constexpr -Wno-absolute-value
    )
endif()
