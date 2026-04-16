# EmuHem: PortaPack Mayhem Firmware Emulator for macOS and Windows

## Context

The Mayhem firmware runs on HackRF+PortaPack hardware (LPC43xx dual-core ARM Cortex-M4/M0 at 200MHz, ChibiOS RTOS). The goal is to run this firmware natively on macOS and Windows by replacing hardware-touching layers with desktop shims -- the same approach Android Studio uses for its device emulator. This gives developers a way to test firmware UI, apps, and baseband signal processing without physical hardware.

The firmware has ~1,100 source files with clean separation between application logic (M4/UI), baseband DSP (M0), and hardware (HAL). We exploit this separation: compile firmware C++ sources natively with Clang/C++23 (macOS) or MSVC/Clang (Windows), swapping out HAL implementations for desktop equivalents.

**Cross-platform strategy:** All platform-specific code is isolated behind abstractions. SDL3 handles display, input, and audio on both platforms. Filesystem paths use `std::filesystem`. Network sockets use POSIX on macOS and Winsock2 on Windows (abstracted behind a thin socket wrapper). CMake handles platform detection and conditional linking.

**Key firmware architecture:**
- M0 thread (application): `main()` -> `portapack::init()` -> `EventDispatcher::run()` -- UI, apps, SD card, settings
- M4 thread (baseband): `BasebandThread::run()` -> `dma::wait_for_buffer()` -> `processor->execute()` -- DSP
- Communication: `SharedMemory` with two `MessageQueue`s + volatile `baseband_message` pointer
- Display: ILI9341 LCD, 240x320 RGB565, driven via GPIO bit-banging through `portapack::IO`
- Input: buttons via `io_read()`, rotary encoder via GPIO, resistive touchscreen via ADC
- RF: RFFC507x -> MAX2837 -> MAX5864 -> SGPIO/DMA -> 8192-sample circular buffer (4x2048 chunks)

---

## Phase 1: Skeleton Build -- Black SDL Window

**Goal:** CMake project compiles and launches, showing a blank SDL3 window. Proves the build system works.

### 1.1 Project Structure

Create the following directory layout:

```
EmuHem/
  CMakeLists.txt                         # Top-level build
  src/
    main_emu.cpp                         # Emulator entry point
    emu_core.hpp / .cpp                  # SDL lifecycle, main loop coordination
    platform/
      chibios_shim/
        ch.h                             # ChibiOS kernel API shim
        hal.h                            # ChibiOS HAL shim
        hal_lld.h                        # Low-level HAL types
        chprintf.h                       # Printf wrapper
        chibios_shim.cpp                 # Implementation
      lpc43xx/
        lpc43xx_cpp.hpp                  # LPC register struct stubs
        memory_map_emu.hpp               # SharedMemory normal allocation
        gpio_emu.hpp                     # GPIO stub (LPC_GPIO struct)
        platform_detect_emu.c            # platform_detect.h stub
        gpdma_stub.hpp                   # GPDMA no-op
      display/
        portapack_io_emu.hpp             # IO class reimplementation
        portapack_io_emu.cpp
        lcd_emu.hpp / .cpp               # SDL3 window + framebuffer blit
      stubs/
        hw_stubs.cpp                     # Catch-all: CPLD, JTAG, backlight, battery, temp, USB serial
        i2c_emu.cpp                      # I2C bus stub
        spi_emu.cpp                      # SPI bus stub
        irq_stubs.cpp                    # IRQ timer stubs (frame sync, RTC tick)
```

### 1.2 CMake Build System

- `CMakeLists.txt`: C++23, find SDL3 via `find_package(SDL3)`, set include paths
- Include order matters: `src/platform/chibios_shim/` FIRST so `#include "ch.h"` and `#include "hal.h"` resolve to our shims
- Then: `mayhem-firmware/firmware/common/`, `mayhem-firmware/firmware/application/`, `mayhem-firmware/firmware/baseband/`
- Compile firmware `.cpp` files explicitly listed, substituting HAL files with our emulator versions
- Exclude all: ChibiOS kernel/HAL sources, ARM assembly (`.S`), hackrf C firmware files, CPLD/JTAG
- Link: SDL3 (cross-platform). On macOS also CoreAudio + AudioToolbox frameworks. On Windows, Winsock2 (`ws2_32.lib`).
- Define: `-DEMUHEM=1` for conditional compilation guards
- CMake platform detection: `if(APPLE)` / `if(WIN32)` for platform-specific linking and source selection
- Windows: support MSVC 17.x (VS 2022) and Clang-cl with C++23. Use vcpkg or FetchContent for SDL3.

### 1.3 ChibiOS Shim (`ch.h` / `hal.h` / `chibios_shim.cpp`)

Map ChibiOS primitives to C++23 standard library:

| ChibiOS | Emulator |
|---------|----------|
| `Thread*`, `chThdCreateStatic` | `std::jthread` wrapper |
| `chThdSleepMilliseconds/Microseconds` | `std::this_thread::sleep_for` |
| `chThdShouldTerminate` | `std::stop_token::stop_requested()` |
| `Mutex`, `chMtxLock/Unlock/TryLock` | `std::mutex` + `std::unique_lock` |
| `Semaphore`, `chSemWait/Signal` | `std::counting_semaphore` |
| `chEvtSignal/SignalI/WaitAny` | Per-thread `std::condition_variable` + `std::atomic<eventmask_t>` |
| `EVENT_MASK(n)` | `(1U << n)` |
| `systime_t`, `chTimeNow` | `std::chrono::steady_clock` |
| `chSysLock/Unlock`, `chSysLockFromIsr/UnlockFromIsr` | Global `std::recursive_mutex` |
| `halPolledDelay` | No-op (timing not critical in emulation) |
| `chDbgPanic` | `std::abort()` with error message to stderr |
| `WORKING_AREA` | `alignas(16) std::array<uint8_t, N>` (unused but satisfies compilation) |
| `CH_IRQ_HANDLER`, `CH_IRQ_PROLOGUE/EPILOGUE` | Empty macros |
| `port_wait_for_interrupt` | `std::this_thread::yield()` |
| `NORMALPRIO` | Integer constant |

Also stub: `LPC_GPIO`, `LPC_CGU`, `LPC_SGPIO`, `LPC_CREG`, `LPC_RGU`, `LPC_GPDMA` as global struct instances with all fields writable but ignored. `palSetPad/palClearPad/palReadPad` as no-ops.

SDC driver stubs: `SDCD1`, `sdcStart/sdcStop/sdcConnect/sdcDisconnect` as no-ops returning success. `I2CD0`, `SPID2` as empty driver structs.

### 1.4 LPC43xx Stubs

- `lpc43xx_cpp.hpp`: Stub `cgu`, `rgu`, `creg` namespaces with no-op functions. `creg::m4txevent::clear()` / `creg::m0apptxevent::assert_event()` -> no-ops (replaced with condition_variable signals).
- `memory_map_emu.hpp`: `SharedMemory` allocated as a normal global, not at a hardcoded address.
- `gpio_emu.hpp`: `GPIO` class with `set()`, `clear()`, `read()`, `write()`, `output()`, `input()` as no-ops.
- `platform_detect_emu.c`: Returns `hackrf_r9 = false`, `portapack_model = PORTAPACK`.
- ARM intrinsics: `#define __asm__(x)` as no-op. Provide stubs for `__SXTB16`, `__SMUAD`, `__SMLAD` etc. from `utility_m4.hpp` as plain C++ equivalents.

### 1.5 Display Stub (Phase 1 = blank window)

- `lcd_emu.hpp/.cpp`: Opens SDL3 window (240x320 scaled 2x = 480x640), creates texture, renders black. Runs SDL event loop on main thread.
- `portapack_io_emu.cpp`: Implements `portapack::IO` class. All LCD write methods write into a `uint16_t framebuffer[240*320]` array. All GPIO methods are no-ops. `io_read()` returns 0 (no switches pressed).

### 1.6 Entry Point (`main_emu.cpp`)

Minimal flow:
1. Initialize SDL3
2. Create display window
3. Call `portapack::init()` (stubbed to return SUCCESS)
4. Start event timers (frame sync at 60Hz, RTC at 1Hz)
5. Enter `event_loop()` (from firmware's main.cpp, but we may need a modified version)
6. SDL event polling integrated with ChibiOS event wait

**Critical files to modify/replace:**
- `mayhem-firmware/firmware/application/main.cpp` -> `src/main_emu.cpp`
- `mayhem-firmware/firmware/application/portapack.cpp` -> partially stubbed init
- `mayhem-firmware/firmware/common/portapack_io.hpp` -> `src/platform/display/portapack_io_emu.hpp`
- `mayhem-firmware/firmware/common/portapack_shared_memory.cpp` -> `src/platform/lpc43xx/memory_map_emu.hpp`

---

## Phase 2: UI Alive -- See the Mayhem Menu

**Goal:** Firmware UI renders correctly in the SDL window. Navigate menus with keyboard.

### 2.1 Complete LCD Emulation

The ILI9341 driver (`lcd_ili9341.cpp`) sends commands followed by data through `portapack::IO`. The emulated IO must track:
- Column/row address window (set by `caset`/`paset` commands = 0x2A/0x2B)
- Auto-increment cursor position on each pixel write (matching ILI9341 behavior)
- `ramwr` command (0x2C) starts a pixel write sequence
- `ramrd` command (0x2E) reads back from framebuffer

Implement command interception in `portapack_io_emu.cpp`:
- `lcd_command(cmd)`: Track current command, set state machine
- `lcd_write_data(value)`: If in RAMWR mode, write RGB565 pixel to framebuffer at current (col, row), auto-advance
- `lcd_read_data()`: If in RAMRD mode, read from framebuffer
- ILI9341 init commands (sleep/wake, color format, rotation): Parse and configure emulator state

### 2.2 SDL Framebuffer Rendering

- Timer or separate thread: 60fps, reads `uint16_t framebuffer[]`, converts RGB565->RGBA8888, uploads to SDL texture, renders
- The firmware's `EVT_MASK_LCD_FRAME_SYNC` event triggers UI repaints. A timer thread fires this event at ~60Hz via `chEvtSignal()` on the event dispatcher thread.

### 2.3 Input Emulation

Replace `irq_controls.cpp` with `irq_controls_emu.cpp`:

| Physical | Emulated |
|----------|----------|
| D-pad Up/Down/Left/Right | Arrow keys |
| Select button | Enter |
| Back (Left+Up) | Escape |
| DFU button | Backspace |
| Rotary encoder | Mouse scroll wheel |
| Touchscreen | Mouse click+drag on window |

SDL events are captured in the display thread and pushed into a thread-safe queue. The `controls_update()` function (called periodically by the IRQ shim timer) reads from this queue.

### 2.4 Filesystem Emulation

Replace FatFS (`ff.h`) with POSIX passthrough:
- `ff_emu.h`: Define `FIL`, `DIR`, `FILINFO`, `FATFS`, `FRESULT` types matching FatFS signatures
- `f_open` -> `fopen`, `f_read` -> `fread`, `f_write` -> `fwrite`, `f_lseek` -> `fseek`, etc.
- SD card root maps to `~/.emuhem/sdcard/` on the host
- `sd_card::status()` always returns `Mounted`

### 2.5 Persistent Memory

The firmware stores 256 bytes of settings in flash. In the emulator:
- Allocate `persistent_memory` as normal memory
- Load/save to `~/.emuhem/persistent_memory.bin` on startup/shutdown
- First run: initialize with defaults

### 2.6 IRQ Timer Threads

Two timer threads replace hardware interrupts:
- **Frame sync thread**: Fires `EVT_MASK_LCD_FRAME_SYNC` at ~60Hz
- **RTC tick thread**: Fires `EVT_MASK_RTC_TICK` at 1Hz, updates `rtc::RTC` time from host system clock

---

## Phase 3: Baseband Integration -- DSP Processors Run

**Goal:** Select an app, baseband processor starts. Spectrum analyzer shows noise floor.

### 3.1 Baseband Processor Registry

On real hardware, `m4_init()` decompresses an LZ4 image from SPI flash by tag and resets the M4 core. In the emulator, all `proc_*.cpp` processors are compiled into the binary. A registry maps image tags to factory functions:

```cpp
// src/platform/core/baseband_registry.hpp
using ProcessorFactory = std::unique_ptr<BasebandProcessor>(*)();
void register_processor(spi_flash::image_tag_t tag, ProcessorFactory factory);
ProcessorFactory find_processor(spi_flash::image_tag_t tag);
```

### 3.2 Core Control Replacement

`core_control_emu.cpp` replaces `core_control.cpp`:
- `m4_init(tag, ...)`: Look up tag in registry -> create processor -> stop old baseband thread -> start new one
- `m0_halt()`: Signal main loop to exit

### 3.3 Virtual SGPIO/DMA (`virtual_sgpio.hpp/.cpp`)

Replaces `baseband_dma.cpp` and `baseband_sgpio.cpp`:
- Maintains `std::array<complex8_t, 8192>` circular buffer, 4 chunks of 2048
- Producer thread fills chunks at configured sample rate (paced by `std::chrono::steady_clock`)
- `wait_for_buffer()` blocks on `std::condition_variable` until a chunk is ready
- Default source: Gaussian noise generator (realistic noise floor)
- Returns `baseband::buffer_t{pointer, 2048}` matching real DMA behavior

### 3.4 BasebandThread Integration

The firmware's `BasebandThread::run()` calls `baseband_sgpio.init()`, `dma::init()`, `dma::configure()`, `dma::enable()`, `sgpio.streaming_enable()` then loops on `dma::wait_for_buffer()`. Our shims make all init calls no-ops and route `wait_for_buffer()` to `VirtualSGPIO`.

### 3.5 BufferExchange

`BufferExchange` handles M4->M0 buffer passing for spectrum data and audio. The `M4Core_IRQHandler` ISR (in `event_m0.cpp`) calls `BufferExchange::handle_isr()` and `EventDispatcher::check_fifo_isr()`. In the emulator, the baseband thread directly calls these after writing to shared memory (replacing the hardware interrupt with a function call + event signal).

### 3.6 Radio State Stubs

`radio_emu.cpp` replaces `radio.cpp`:
- `set_tuning_frequency()`, `set_lna_gain()`, `set_vga_gain()`, etc.: Store state, log changes
- `set_baseband_rate()`: Updates VirtualSGPIO pacing rate
- `set_direction()`: Updates VirtualSGPIO direction (RX/TX)
- All RF chip drivers (rffc507x, max2837, max5864, si5351): Register read/write stubs that store values

---

## Phase 4: Audio Output

**Goal:** Hear decoded audio through speakers (macOS and Windows).

### 4.1 Audio DMA Replacement

Replace `audio_dma.cpp` with SDL3 audio output (cross-platform):
- SDL3 `SDL_OpenAudioDeviceStream()` with the firmware's audio sample rate (typically 48kHz)
- `AudioOutput::write()` pushes PCM samples into a lock-free ring buffer
- SDL audio callback pulls from the ring buffer
- SDL3 audio works identically on macOS (CoreAudio backend) and Windows (WASAPI backend)

### 4.2 Audio Codec Stubs

- `audio_codec_wm8731.detected()` -> returns `true`
- `audio_codec_ak4951.detected()` -> returns `false`
- Volume/gain controls: Store state (optionally apply to SDL volume)

---

## Phase 5: RF/Antenna Connectivity via Network

**Goal:** Connect external SDR software or other emulator instances to provide/consume I/Q data.

### 5.1 Architecture

```
External SDR Software (gqrx, GNU Radio, SDR#)
       |  rtl_tcp protocol (TCP port 1234)
       v
  AntennaPortManager
       |
  VirtualAntennaPort (frequency binding, format conversion)
       |
  VirtualSGPIO (circular buffer, pacing)
       |
  BasebandProcessor::execute(buffer_c8_t&)
```

### 5.2 I/Q Endpoint Interface

```cpp
class IQEndpoint {
public:
    virtual size_t read_samples(complex8_t* buf, size_t count) = 0;
    virtual size_t write_samples(const complex8_t* buf, size_t count) = 0;
    virtual bool is_connected() const = 0;
};
```

Implementations:
- `NoiseGenerator`: Default, Gaussian white noise at ~-100dBm equivalent
- `RtlTcpServer`: TCP server implementing rtl_tcp protocol (widely supported by SDR clients)
- `FileIQSource`: Reads `.c8`/`.cu8`/`.cs16`/`.wav` files, loops, paces at sample rate
- `FileIQSink`: Writes TX output to file
- `EiqpServer`: Custom bidirectional TCP protocol for inter-emulator connections
- `UdpStream`: Low-latency UDP for real-time inter-instance linking

### 5.3 rtl_tcp Server (Primary)

- TCP server on configurable port (default 1234)
- Sends 12-byte dongle info header on connect
- Streams unsigned 8-bit I/Q (firmware uses signed -- conversion: `val ^ 0x80`)
- Receives 5-byte command packets (set freq, gain, sample rate)
- Allows connecting gqrx, SDR#, GNU Radio `rtl_tcp` source blocks

### 5.4 File I/Q Support

Critical for testing without network setup:
- Auto-detect format from extension (`.c8`, `.cu8`, `.cs16`, `.wav`, `.cf32`)
- Sample rate from WAV header or user config
- Loop mode for continuous replay
- Support PortaPack's own capture format (`.C16` files from SD card)

### 5.5 Sample Format Conversion

```
cu8 <-> cs8: XOR 0x80
cs16 -> cs8: >> 8
cs8 -> cs16: << 8
cf32 -> cs8: * 127.0f, clamp
```

### 5.6 Antenna Port Manager

- Default port: noise generator (always active)
- User-configured ports: each bound to a frequency range + IQEndpoint
- When firmware tunes to a frequency, selects best-matching port
- Future: NCO frequency shifting for offset sources, polyphase rational resampler for rate mismatch

---

## Phase 6: Polish and Device Frame

**Goal:** Full-featured emulator with device chrome.

### 6.1 Device Frame Rendering

SDL window shows a rendered device frame around the LCD area (similar to Android Studio):
- PortaPack shell graphic with screen cutout
- Visual button indicators that highlight on press
- LED status dots (colored circles for the 4 HackRF LEDs)
- Antenna port indicator showing connection status

### 6.2 Multi-resolution Support

- PortaPack H1/H2: 240x320
- PortaRF H4M: 320x480
- Selectable via command-line flag or config file

### 6.3 Configuration File

`~/.emuhem/config.toml`:
```toml
[display]
resolution = "240x320"
scale = 2

[sdcard]
path = "~/.emuhem/sdcard"

[antenna]
default_port = 1234
protocol = "rtl_tcp"

[audio]
enabled = true
```

---

## Phase 7: Command Line Arguments & Crash Diagnostics

**Goal:** Make the emulator fully controllable from the command line so that an LLM agent (or any automated tooling) can launch, drive, inspect, and debug the emulator without interactive GUI use. Write structured crash dumps on failure for post-mortem analysis.

### 7.1 Argument Parser

Use a single-header C++ argument parser (e.g., `argparse` or hand-rolled with `std::span<char*>`) in `src/cli/cli_args.hpp`. All flags use GNU-style long options with short aliases where practical. Unknown flags print help and exit with code `1`.

### 7.2 Display & Headless Mode

| Flag | Short | Description |
|------|-------|-------------|
| `--headless` | `-H` | Run without opening an SDL window. Framebuffer still maintained in memory for screenshots. |
| `--scale <n>` | `-s` | Window scale factor (default `2`). Ignored in headless mode. |
| `--resolution <WxH>` | `-r` | LCD resolution: `240x320` (default) or `320x480`. |
| `--no-frame` | | Hide the device chrome, show only the LCD area. |
| `--fps <n>` | | Override frame sync rate (default `60`). Useful for fast-forward testing. |

### 7.3 App Launch & Navigation

| Flag | Short | Description |
|------|-------|-------------|
| `--app <name>` | `-a` | Launch directly into a firmware app by name (e.g., `SpectrumAnalyzer`, `FMReceiver`, `Settings`). Skips splash screen and menu navigation. |
| `--list-apps` | | Print all registered app names and exit. Useful for LLM discovery. |
| `--keys <sequence>` | `-k` | Inject a key sequence on startup. Comma-separated list of key names with optional hold durations: `down,down,enter,wait:500,escape`. Supported keys: `up`, `down`, `left`, `right`, `enter`, `escape`, `back`, `dfu`, `enc_cw` (encoder clockwise), `enc_ccw` (encoder counter-clockwise). |
| `--touch <sequence>` | `-t` | Inject touch events. Comma-separated `x:y` or `x:y:duration_ms` coordinates: `120:160,120:200:300`. |
| `--script <path>` | | Run an input script file (one command per line, same syntax as `--keys`/`--touch` with additional `sleep <ms>` command). Enables complex multi-step automation. |

### 7.4 RF & I/Q Configuration

| Flag | Short | Description |
|------|-------|-------------|
| `--iq-file <path>` | `-i` | Load I/Q samples from file (`.c8`, `.cu8`, `.cs16`, `.cf32`, `.C16`, `.wav`). Replaces noise generator. |
| `--iq-loop` | | Loop the I/Q file continuously. |
| `--iq-tcp <host:port>` | | Connect to an rtl_tcp server as I/Q source. |
| `--listen <port>` | `-l` | Start rtl_tcp server on given port for external SDR clients. |
| `--frequency <hz>` | `-f` | Set initial tuning frequency in Hz (e.g., `433920000`). |
| `--sample-rate <hz>` | | Override baseband sample rate. |
| `--tx-output <path>` | | Write TX I/Q output to file instead of discarding. |

### 7.5 Logging & Inspection

| Flag | Short | Description |
|------|-------|-------------|
| `--log-level <level>` | `-L` | Set log verbosity: `error`, `warn`, `info` (default), `debug`, `trace`. |
| `--log-file <path>` | | Write logs to file instead of stderr. |
| `--log-json` | | Emit logs as newline-delimited JSON for machine parsing: `{"ts":"...","level":"info","module":"baseband","msg":"..."}` |
| `--dump-state <path>` | | On exit, write a full emulator state snapshot (JSON): radio config, current app, shared memory contents, thread states, framebuffer hash. |
| `--dump-framebuffer <path>` | `-D` | On exit (or on signal), write the current framebuffer as a raw RGB565 `.bin` or `.png` file (format inferred from extension). |
| `--screenshot <path>` | | Take a screenshot at a specific point: combine with `--keys` or `--script` to navigate, then capture. Exits after writing. |
| `--print-messages` | | Log all `SharedMemory` `MessageQueue` traffic between M0/M4 threads to stdout (or `--log-file`). |
| `--print-radio` | | Log all radio state changes (frequency, gain, sample rate, direction). |
| `--trace-ui` | | Log all UI widget paint calls with bounding rects and widget type. |
| `--timeout <ms>` | | Exit the emulator after N milliseconds. Useful for bounded automated runs. Writes crash dump if still running at timeout. |

### 7.6 Filesystem & Config

| Flag | Short | Description |
|------|-------|-------------|
| `--sdcard <path>` | | Override SD card root directory (default `~/.emuhem/sdcard`). |
| `--config <path>` | `-c` | Path to TOML config file (default `~/.emuhem/config.toml`). |
| `--no-config` | | Ignore config file, use only CLI flags and defaults. |
| `--persistent-memory <path>` | | Override persistent memory file (default `~/.emuhem/persistent_memory.bin`). |
| `--reset-persistent` | | Clear persistent memory on startup (fresh settings). |

### 7.7 Crash Dump System (`src/platform/diagnostics/crash_dump.hpp/.cpp`)

On any unhandled exception, segfault (`SIGSEGV`), abort (`SIGABRT`), or bus error (`SIGBUS`), the emulator writes a structured crash dump before terminating. On Windows, this also hooks `SetUnhandledExceptionFilter` for SEH exceptions.

**Crash dump location:** `~/.emuhem/crashes/crash_<ISO8601_timestamp>.json` (e.g., `crash_2026-04-16T14-30-00.json`)

**Crash dump contents:**

```json
{
  "version": "1.0",
  "timestamp": "2026-04-16T14:30:00.123Z",
  "signal": "SIGSEGV",
  "exit_code": 139,
  "platform": "darwin-arm64",
  "build": {
    "commit": "abc1234",
    "build_type": "Debug",
    "compiler": "AppleClang 16.0"
  },
  "threads": [
    {
      "name": "main/m0-app",
      "id": 1,
      "is_faulting": true,
      "backtrace": [
        "0: emuhem::lcd_emu::blit_framebuffer() at lcd_emu.cpp:142",
        "1: emuhem::EventDispatcher::run() at event_dispatcher.cpp:87",
        "..."
      ]
    },
    {
      "name": "m4-baseband",
      "id": 2,
      "is_faulting": false,
      "backtrace": ["..."]
    }
  ],
  "emulator_state": {
    "current_app": "SpectrumAnalyzer",
    "baseband_processor": "proc_wideband_spectrum",
    "radio": {
      "frequency_hz": 433920000,
      "lna_gain_db": 32,
      "vga_gain_db": 24,
      "sample_rate": 2500000,
      "direction": "rx"
    },
    "shared_memory_hex": "0a1b2c3d...",
    "message_queue_depth": { "m0_to_m4": 3, "m4_to_m0": 0 }
  },
  "framebuffer_png_base64": "<base64-encoded 240x320 PNG>",
  "recent_logs": [
    "[14:29:59.980] [debug] [baseband] processor execute() returned",
    "[14:29:59.999] [error] [display] null pointer in blit_framebuffer",
    "..."
  ],
  "command_line": ["./emuhem", "--app", "SpectrumAnalyzer", "--iq-file", "test.cu8"]
}
```

**Implementation:**

- Register signal handlers in `main_emu.cpp` at startup via `std::signal` (POSIX) and `SetUnhandledExceptionFilter` (Windows)
- Signal handler writes to a pre-allocated buffer (no malloc in signal context) and flushes to disk via raw `write()` / `WriteFile()`
- Backtraces via `execinfo.h` `backtrace()` on macOS, `CaptureStackBackTrace()` + `SymFromAddr()` on Windows
- Emulator state snapshot is maintained incrementally (current app name, radio config) in a global `DiagnosticsState` struct so the signal handler doesn't need to traverse complex data structures
- Framebuffer is always available in memory -- encode to PNG using a minimal encoder (e.g., `stb_image_write.h`, header-only, no dependency)
- Circular log buffer (last 200 lines) kept in a fixed-size ring buffer for inclusion in crash dumps
- `--crash-dump-dir <path>` flag overrides the default crash directory

### 7.8 Crash Dump CLI Flags

| Flag | Short | Description |
|------|-------|-------------|
| `--crash-dump-dir <path>` | | Override crash dump output directory. |
| `--no-crash-dump` | | Disable automatic crash dump writing. |
| `--crash-on-warn` | | Treat any `warn`-level log as fatal -- write crash dump and exit. Useful for strict automated testing. |
| `--test-crash` | | Trigger a deliberate null-pointer dereference after initialization to verify the crash dump system works. |

### 7.9 LLM Workflow Examples

**Automated smoke test** -- launch an app, wait 2 seconds, screenshot, exit:
```bash
./emuhem --app SpectrumAnalyzer --timeout 2000 --screenshot /tmp/spectrum.png
```

**Debug a crash** -- reproduce with full tracing, inspect the crash dump:
```bash
./emuhem --app FMReceiver --iq-file broken.cu8 --log-level trace \
         --log-json --log-file /tmp/debug.log --print-messages
# after crash, read ~/.emuhem/crashes/crash_*.json
```

**Headless CI test** -- run without display, verify exit code:
```bash
./emuhem --headless --app Settings --keys "down,down,enter,wait:1000,escape" \
         --timeout 5000 --dump-state /tmp/state.json
```

**Input script for complex navigation** (`test_nav.script`):
```
keys down,down,enter
sleep 500
touch 120:160
sleep 200
keys escape
screenshot /tmp/result.png
```
```bash
./emuhem --headless --script test_nav.script --timeout 10000
```

**Compare framebuffer output between builds:**
```bash
./emuhem --headless --app SpectrumAnalyzer --iq-file ref.cu8 \
         --timeout 3000 --dump-framebuffer /tmp/fb_new.bin
diff /tmp/fb_baseline.bin /tmp/fb_new.bin
```

---

## File Replacement Map

| Firmware File | Emulator Replacement | Reason |
|---|---|---|
| `firmware/application/main.cpp` | `src/main_emu.cpp` | Different init flow, SDL integration |
| `firmware/application/portapack.cpp` | Partially stubbed | Hardware init -> no-ops |
| `firmware/application/core_control.cpp` | `src/platform/core/core_control_emu.cpp` | Thread-based processor loading |
| `firmware/application/irq_controls.cpp` | `src/platform/input/irq_controls_emu.cpp` | Keyboard/mouse input |
| `firmware/application/irq_lcd_frame.cpp` | `src/platform/stubs/irq_stubs.cpp` | Timer thread |
| `firmware/application/irq_rtc.cpp` | `src/platform/stubs/irq_stubs.cpp` | Timer thread |
| `firmware/common/portapack_io.hpp/.cpp` | `src/platform/display/portapack_io_emu.*` | Framebuffer instead of GPIO |
| `firmware/common/portapack_shared_memory.cpp` | `src/platform/lpc43xx/memory_map_emu.hpp` | Normal allocation |
| `firmware/application/radio.cpp` | `src/platform/radio/radio_emu.cpp` | State tracking + IQ routing |
| `firmware/application/sd_card.cpp` | `src/platform/filesystem/sd_card_emu.cpp` | Always mounted |
| `firmware/baseband/baseband_dma.cpp` | `src/platform/radio/virtual_sgpio.cpp` | Network/file I/Q source |
| `firmware/baseband/baseband_sgpio.cpp` | No-op stubs | Not needed |
| `firmware/common/hackrf_hal.cpp` | Stubs | No real hardware |
| `firmware/application/hw/*.cpp` | `src/platform/radio/rf_stubs.cpp` | Register store only |
| ChibiOS `chibios/`, `chibios-portapack/` | `src/platform/chibios_shim/` | std::thread-based |
| FatFS `ff.c` | `src/platform/filesystem/ff_emu.cpp` | POSIX passthrough |

---

## Cross-Platform (macOS + Windows) Considerations

### Build System
- CMake 3.25+ required (C++23 module support, SDL3 FetchContent)
- macOS: Clang 16+ (Xcode 15+), Homebrew SDL3
- Windows: MSVC 17.8+ (VS 2022) or Clang-cl, SDL3 via vcpkg or FetchContent
- CI: GitHub Actions with both `macos-latest` and `windows-latest` runners

### Platform Abstraction Points
All platform-specific code is confined to these areas:

| Area | macOS | Windows | Abstraction |
|------|-------|---------|-------------|
| Display/Input/Audio | SDL3 (identical) | SDL3 (identical) | SDL3 handles all |
| Filesystem paths | `std::filesystem` | `std::filesystem` | Standard C++17 |
| Network sockets | POSIX (`<sys/socket.h>`) | Winsock2 (`<winsock2.h>`) | Thin `emu_socket.hpp` wrapper |
| Config directory | `~/.emuhem/` | `%APPDATA%/EmuHem/` | `emu_paths.hpp` using `std::filesystem` |
| Thread naming | `pthread_setname_np` | `SetThreadDescription` | Optional, debug only |
| Dynamic linking | N/A | `__declspec(dllexport)` | Not needed (static build) |

### Socket Abstraction (`src/platform/net/emu_socket.hpp`)
Minimal wrapper for the rtl_tcp and EIQP servers:
- `socket_init()` / `socket_cleanup()` -- Winsock `WSAStartup`/`WSACleanup` on Windows, no-op on macOS
- `socket_t` type alias -- `int` on macOS, `SOCKET` on Windows
- `socket_close()` -- `close()` on macOS, `closesocket()` on Windows
- `socket_error()` -- `errno` on macOS, `WSAGetLastError()` on Windows

### Windows-Specific Notes
- `__asm__("nop")` uses `#ifdef _MSC_VER` -> `__nop()` intrinsic or empty
- `ssize_t` not defined on MSVC -> provide typedef
- `usleep()` not available -> use `std::this_thread::sleep_for`
- Path separators handled by `std::filesystem` (no manual `/` vs `\`)
- SDL3 on Windows uses DirectX/WASAPI backends automatically

---

## Risks and Mitigations

1. **Large ChibiOS API surface**: Start minimal, add functions as link errors reveal them. Unimplemented functions `abort()` with message.
2. **ARM intrinsics / inline assembly**: `__asm__("nop")` -> no-op macro. DSP intrinsics (`__SXTB16`, `__SMUAD`) -> C++ equivalents in a compat header.
3. **Memory-mapped I/O scattered everywhere**: Stub `LPC_GPIO`, `LPC_CGU`, etc. as global struct instances. Writes go to normal memory.
4. **Not all 1,100 files will compile on host**: Start with minimum set for Phase 1, add incrementally. Track compilation blockers in a list.
5. **Thread safety**: SharedMemory accessed from two threads. The firmware's `MessageQueue` already uses `chMtxTryLock` -> maps to `std::mutex::try_lock`. This is safe.
6. **FatFS type compatibility**: `ff_emu.h` must define identical `FRESULT`, `FILINFO`, `FIL` types for the firmware's `File` class to compile.

---

## Verification Plan

### Phase 1
- `cmake --build build` succeeds with zero errors
- Binary launches, shows SDL window with black content
- Window title shows "EmuHem - PortaPack Mayhem Emulator"
- Clean exit on window close

### Phase 2
- Mayhem splash screen appears in SDL window
- Main menu renders with correct colors and fonts
- Arrow keys navigate menu items (highlight moves)
- Enter key selects menu items
- Escape key goes back
- Mouse scroll changes encoder value
- Settings load/save persists across restarts

### Phase 3
- Opening Spectrum Analyzer app starts baseband processor thread
- Spectrum display shows noise floor (random noise)
- Switching apps correctly stops old processor, starts new one
- No crashes or deadlocks during app switching

### Phase 4
- Opening an audio app (e.g., FM receiver) with an I/Q file source produces audible audio through speakers
- Volume control works

### Phase 5
- `gqrx` or `SDR++` can connect to `localhost:1234` and see the spectrum
- Playing a `.cu8` recording through the emulator shows decoded data in the firmware UI
- Two emulator instances can exchange I/Q data over TCP

### Phase 6
- Device frame renders around LCD
- All input methods work reliably
- Configuration file is respected

### Phase 7
- `--headless --app SpectrumAnalyzer --timeout 2000` launches, runs, and exits cleanly with code `0`
- `--screenshot /tmp/test.png` produces a valid PNG matching the current framebuffer
- `--keys "down,down,enter"` navigates the menu identically to physical key presses
- `--script` file executes all commands in sequence with correct timing
- `--log-json` output is valid newline-delimited JSON parseable by `jq`
- `--dump-state` produces a complete JSON snapshot with correct radio config and app name
- `--test-crash` writes a crash dump JSON with valid backtrace, framebuffer, and emulator state
- Crash dump is written on real `SIGSEGV` / unhandled exception (verified with ASAN-triggered fault)
- `--list-apps` prints all registered apps and exits
- `--crash-on-warn` exits with crash dump when a warning is logged
- All flags work identically on macOS and Windows