# EmuHem unit tests (Phase 13).
#
# Ports the upstream firmware test suite at ${FW_DIR}/test/ to run natively
# against the firmware TUs EmuHem compiles. Uses doctest (single-header,
# already shipped at ${FW_DIR}/test/include/doctest.h). Each test binary is
# registered with CTest so `ctest --test-dir build --output-on-failure`
# drives the whole suite.
#
# Test source files are NOT copied into this tree — they are referenced in
# place from the firmware checkout (same pattern EmuHem uses for the rest of
# the firmware source). Changes upstream are picked up on the next build.

set(FW_TEST_DIR ${FW_DIR}/test)
set(FW_TEST_INCLUDE ${FW_TEST_DIR}/include)

# ----------------------------------------------------------------------------
# Patch tone_key.cpp — fixes `std::std::abs` typo in upstream firmware that
# the main EmuHem build sidesteps by excluding tone_key.cpp + providing stubs
# in phase2_stubs.cpp. For the test target we want real tone_key behaviour so
# freqman_db.cpp's parse/serialize roundtrips exercise meaningful code.
# ----------------------------------------------------------------------------
set(PATCHED_TONE_KEY ${CMAKE_CURRENT_BINARY_DIR}/patched_tone_key.cpp)
file(READ ${FW_DIR}/application/tone_key.cpp _TK_SRC)
string(REPLACE "std::std::abs" "std::abs" _TK_SRC "${_TK_SRC}")
file(WRITE ${PATCHED_TONE_KEY} "${_TK_SRC}")

# ----------------------------------------------------------------------------
# Application test suite
#
# Covers utility classes and helpers that live in firmware/application/ and
# firmware/common/: circular_buffer, convert, optional, string_format,
# utility, file_reader, file_wrapper, freqman_db, mock_file, basics sanity.
# ----------------------------------------------------------------------------

add_executable(emuhem_tests_application
    ${FW_TEST_DIR}/application/main.cpp
    ${FW_TEST_DIR}/application/test_basics.cpp
    ${FW_TEST_DIR}/application/test_circular_buffer.cpp
    ${FW_TEST_DIR}/application/test_convert.cpp
    ${FW_TEST_DIR}/application/test_file_reader.cpp
    ${FW_TEST_DIR}/application/test_file_wrapper.cpp
    ${FW_TEST_DIR}/application/test_freqman_db.cpp
    ${FW_TEST_DIR}/application/test_mock_file.cpp
    ${FW_TEST_DIR}/application/test_optional.cpp
    ${FW_TEST_DIR}/application/test_string_format.cpp
    ${FW_TEST_DIR}/application/test_utility.cpp

    # Firmware TUs under test (and their dependencies).
    ${FW_DIR}/application/file.cpp
    ${FW_DIR}/application/file_reader.cpp
    ${FW_DIR}/application/freqman_db.cpp
    ${FW_DIR}/application/string_format.cpp
    ${PATCHED_TONE_KEY}
    ${FW_DIR}/common/utility.cpp

    # FatFS symbol satisfaction (real SD card not needed for unit tests).
    ${CMAKE_CURRENT_SOURCE_DIR}/linker_stubs.cpp
)

target_include_directories(emuhem_tests_application PRIVATE
    ${FW_TEST_INCLUDE}                             # doctest.h
    ${FW_TEST_DIR}/application                     # mock_file.hpp
    ${FW_DIR}/application
    ${FW_DIR}/common
    ${FW_DIR}/baseband
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/fatfs_shim       # ff.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/chibios_shim     # ch.h (pulled via ffconf.h)
)

target_compile_definitions(emuhem_tests_application PRIVATE
    EMUHEM=1
    LPC43XX_M0=1
)

# ----------------------------------------------------------------------------
# Baseband test suite
#
# Covers DSP primitives that live in firmware/common/. dsp_fft.cpp is not in
# EmuHem's main build (filtered out by the baseband-only regex) — we add it
# here explicitly so the FFT can be unit-tested without spinning up the full
# baseband pipeline.
# ----------------------------------------------------------------------------

add_executable(emuhem_tests_baseband
    ${FW_TEST_DIR}/baseband/main.cpp
    ${FW_TEST_DIR}/baseband/dsp_fft_test.cpp

    ${FW_DIR}/common/dsp_fft.cpp
)

target_include_directories(emuhem_tests_baseband PRIVATE
    ${FW_TEST_INCLUDE}
    ${FW_DIR}/common
    ${FW_DIR}/baseband
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/chibios_shim     # hal.h (pulled via lpc43xx_cpp.hpp)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/lpc43xx          # ARM intrinsic stubs (__RBIT, __CLZ, ...)
)

target_compile_definitions(emuhem_tests_baseband PRIVATE
    EMUHEM=1
    LPC43XX_M4=1
)

# The top-level CMakeLists adds -DLPC43XX_M0=1 globally (EmuHem's main build
# is M0-flavored). The baseband tests target M4 code paths, so we need to
# undo that global define for this target only. Also force-include
# lpc43xx_cpp.hpp so the ARM intrinsic stubs (__RBIT, __CLZ, ...) are
# available in every TU — the firmware's buffer.hpp only pulls them in for
# M0 builds, but dsp_fft.hpp needs __RBIT on every build.
target_compile_options(emuhem_tests_baseband PRIVATE
    -ULPC43XX_M0
    -include lpc43xx_cpp.hpp
)

# ----------------------------------------------------------------------------
# CTest wiring
# ----------------------------------------------------------------------------

# Upstream firmware drift: "It can parse frequency step" hardcodes the enum
# ordinal position of "0.1kHz" (expected 0) and "50kHz" (expected 11). Newer
# firmware added 2 smaller step entries ahead of 0.1kHz, shifting everything
# by +2. This is an upstream test staleness issue, not an EmuHem porting
# issue — excluding until upstream updates the expected values. Tracked in
# docs/tests_status.md.
add_test(NAME emuhem_tests_application
    COMMAND emuhem_tests_application
        --test-case-exclude=*parse\ frequency\ step*
)
add_test(NAME emuhem_tests_baseband    COMMAND emuhem_tests_baseband)

# ----------------------------------------------------------------------------
# Phase 16: integration tests
# ----------------------------------------------------------------------------
add_subdirectory(integration)
