# EmuHem

Desktop emulator for the [PortaPack Mayhem](https://github.com/portapack-mayhem/mayhem-firmware)
firmware, running natively on macOS (and soon Windows) without HackRF+PortaPack hardware.

EmuHem compiles the unmodified Mayhem firmware C++ sources and swaps the
hardware-touching layers (LPC43xx peripherals, ChibiOS RTOS, LCD, touchscreen,
SGPIO/DMA, audio codec, SD card, RF chain) for desktop shims built on SDL3 and
the C++23 standard library. The result is a single `emuhem` binary that boots
the real firmware image — UI, apps, baseband DSP — with synthetic or real I/Q
fed through a virtual DMA.

## Status

**Phase 12 — TX path lands.** 43 of ~55 baseband image tags registered. 18 TX
modulators now emit observable I/Q via the `IQSink` abstraction
(`NullIQSink` / `FileIQSink` / `SoapyIQSink`). RX chain supports file replay
(.c8/.cu8/.cs16/.cf32/.wav), rtl_tcp client, rtl_tcp server fan-out, and live
USB SDRs through SoapySDR. See [`docs/implementation_status.md`](docs/implementation_status.md)
for the full matrix.

Outstanding: SSTV RX/TX, WEFAX RX, NOAA APT RX, capture/replay, AM TV, mictx,
Windows Winsock2 wrappers, `--app=<name>` direct launch.

## Architecture

Source-level HAL replacement — no firmware is rewritten or forked. See
[`docs/architecture.md`](docs/architecture.md) for the design in full.

| Firmware layer | Desktop replacement |
|---|---|
| ChibiOS threads/mutexes/events | `std::jthread`, `std::mutex`, condition variables |
| LPC43xx registers, ARM DSP intrinsics | Header stubs + software equivalents |
| ILI9341 LCD | SDL3 texture (240×320, 2× scale, optional bezel) |
| Resistive touchscreen | Mouse → post-calibration touch injection |
| Buttons + rotary encoder | Keyboard (arrows, enter, esc) + scroll wheel |
| I²S audio codec | SDL3 audio stream (48 kHz stereo s16) |
| SGPIO / DMA baseband buffer | Paced virtual DMA (`baseband_dma_emu.cpp`) |
| RFFC507x + MAX2837 + MAX5864 | `IQSource` / `IQSink` (file / rtl_tcp / SoapySDR) |
| FatFS SD card | POSIX passthrough rooted at `~/.emuhem/sdcard/` |
| Persistent settings | `~/.emuhem/pmem_settings.bin` |

Two firmware threads (M0 application, M4 baseband) run as host threads in a
single process. `SharedMemory` + `MessageQueue` wiring is preserved verbatim;
only the thread primitives change.

## Requirements

- macOS 13+ (Apple Silicon or Intel), Xcode command-line tools
- CMake ≥ 3.25
- Clang with C++23 support (Apple Clang 15+ is fine)
- SDL3 (`brew install sdl3`)
- **Optional** SoapySDR for live USB SDRs (`brew install soapysdr soapyhackrf soapyrtlsdr`)

Windows support is in-progress; the tree compiles conditionally for MSVC/Clang-cl
but the Winsock2 wrappers for rtl_tcp are not yet written.

## Build

```sh
git clone --recursive https://github.com/<you>/EmuHem.git
cd EmuHem
cmake -B build
cmake --build build -j
./build/emuhem
```

The first configure patches a handful of firmware source files in-place (GCC-ARM-only
idioms that Clang rejects, and ODR clashes between the M0 and M4 event dispatchers).
Patches are idempotent; see the header of `CMakeLists.txt` for the list.

## Running

```sh
./build/emuhem --help
```

### Input

| Key | Action |
|---|---|
| Arrow keys | D-pad |
| Enter | Select |
| Esc | Back |
| `+` / `-` | Encoder |
| Mouse click/drag | Touch (post-calibration, injected directly) |
| Scroll wheel | Encoder |

### I/Q sources (RX)

| Flag | Env var | Meaning |
|---|---|---|
| `--iq-file=PATH` | `EMUHEM_IQ_FILE` | Replay a capture (.c8/.cu8/.cs16/.cf32/.wav) |
| `--iq-loop=0` | `EMUHEM_IQ_LOOP` | Play once instead of looping |
| `--iq-tcp=host:port` | `EMUHEM_IQ_TCP` | Pull from a remote rtl_tcp server |
| `--iq-center=HZ` | `EMUHEM_IQ_CENTER` | NCO shift for off-center captures |
| `--soapy=ARGS` | `EMUHEM_IQ_SOAPY` | Live USB SDR via SoapySDR |
| `--soapy-rate/-freq/-gain` | `EMUHEM_IQ_SOAPY_*` | SoapySDR tuning defaults |
| *(none)* | — | Noise + test tone (default) |

Firmware tuning changes propagate upstream on both the rtl_tcp client and SoapySDR
paths (set_frequency / set_sample_rate / set_gain).

### I/Q sinks (TX)

| Flag | Env var | Meaning |
|---|---|---|
| `--iq-tx-file=PATH` | `EMUHEM_IQ_TX_FILE` | Capture modulator output as CS8 |
| `--iq-tx-soapy=ARGS` | `EMUHEM_IQ_TX_SOAPY` | Transmit on a HackRF-class SDR |
| `--iq-tx-soapy-rate/-freq/-gain` | `EMUHEM_IQ_TX_SOAPY_*` | TX tuning defaults |

`./build/emuhem --iq-tx-file=out.c8 ... && ./build/emuhem --iq-file=out.c8` is a
lossless loopback.

### rtl_tcp server fan-out

Set `EMUHEM_RTL_TCP_SERVER=0.0.0.0:1234` (IPv6 `[::1]:1234` also works) to serve
EmuHem's baseband to external tools like gqrx / SDR++ / GNU Radio. Works for
both RX (whatever source is active) and TX (the modulator's output).

### Automation

```sh
./build/emuhem --headless --duration=10 --keys='DDS.' --iq-file=capture.c8
```

Scripted-key chars: `UDLR`=dpad, `S`=Select, `B`=Back, `F`=DFU, `+/-`=encoder,
`.`=pause. Key step via `--key-step=MS`. Headless mode skips window/renderer.

### Persistence

- SD card content: `~/.emuhem/sdcard/` (override: `EMUHEM_SDCARD_ROOT`)
- Settings: `~/.emuhem/pmem_settings.bin` (override: `EMUHEM_PMEM_FILE`)

## Repository layout

```
EmuHem/
├── CMakeLists.txt               # Build + firmware source patching
├── src/
│   ├── main_emu.cpp             # Emulator entry point, SDL main loop
│   └── platform/
│       ├── chibios_shim/        # Threads, mutexes, events → std lib
│       ├── fatfs_shim/          # FatFS → POSIX passthrough
│       ├── lpc43xx/             # Register stubs, ARM DSP intrinsics
│       └── portapack_shim/      # IO, baseband DMA, audio, core_control,
│                                # persistent memory, I/Q source+sink
├── docs/                        # Architecture, phase notes, CTest plan
├── mayhem-firmware/             # Upstream firmware (compiled as-is + patches)
├── hackrf/                      # HackRF common headers (platform_detect etc.)
└── PortaRF/                     # Latest PortaRF hardware design reference
```

## License

Match the upstream Mayhem firmware license (GPLv3). Emulator-specific shims
are under the same license.

## Acknowledgements

- The Mayhem firmware maintainers and contributors
- The HackRF project
- Great Scott Gadgets for the original HackRF design