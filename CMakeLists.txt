cmake_minimum_required(VERSION 3.25)
project(EmuHem LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Emulator define
add_definitions(-DEMUHEM=1)

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
# Include directories
# Order matters: our shims MUST come first to override ChibiOS headers.
# ---------------------------------------------------------------------------
set(EMU_INCLUDE_DIRS
    # Our shims (highest priority)
    ${EMU_PLATFORM_DIR}/chibios_shim
    ${EMU_PLATFORM_DIR}/lpc43xx

    # Firmware common and application headers
    ${FW_DIR}/common
    ${FW_DIR}/application
    ${FW_DIR}/application/hw
    ${FW_DIR}/application/apps
    ${FW_DIR}/application/ui
    ${FW_DIR}/baseband

    # HackRF headers (for platform_detect.h, hackrf_core.h, etc.)
    ${HACKRF_DIR}
)

# ---------------------------------------------------------------------------
# Phase 1: Emulator skeleton sources only
# (No firmware sources yet -- just the shims and main)
# ---------------------------------------------------------------------------
set(EMU_SOURCES
    # Entry point
    ${EMU_SRC_DIR}/main_emu.cpp

    # ChibiOS shim
    ${EMU_PLATFORM_DIR}/chibios_shim/chibios_shim.cpp
    ${EMU_PLATFORM_DIR}/chibios_shim/hal_instances.cpp

    # Platform stubs
    ${EMU_PLATFORM_DIR}/lpc43xx/platform_detect_emu.c
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(emuhem ${EMU_SOURCES})

target_include_directories(emuhem PRIVATE ${EMU_INCLUDE_DIRS})

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(emuhem PRIVATE SDL3::SDL3)

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
    )
endif()
