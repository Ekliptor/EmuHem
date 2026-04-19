# EmuHem Implementation Status

Last updated: 2026-04-19 (Phase 7 — CLI flags + headless + scripted keys + crash handler)

## Overview

EmuHem compiles ~310 PortaPack Mayhem firmware C++ source files natively on macOS using Clang/C++23, replacing hardware layers with desktop shims. The emulator renders the full Mayhem UI in an SDL3 window with keyboard/mouse navigation and has a working baseband processing pipeline with synthetic RF data.

---

## Completed Phases

### Phase 1: Skeleton Build (2026-04-16)

SDL3 window opens, ChibiOS kernel mapped to C++23 standard library, LPC43xx peripheral registers stubbed.

- CMakeLists.txt with C++23, SDL3 via `find_package`, cross-platform support
- ChibiOS shim: threads (`std::jthread`), mutexes (`std::mutex`), events (`std::condition_variable` + `std::atomic<eventmask_t>`), semaphores (`std::counting_semaphore`)
- LPC43xx peripheral register structs (GPIO, CGU, CREG, RGU, SGPIO, GPDMA, SCU, SSP, Timer, ADC, RTC, WWDT, I2S)
- ARM intrinsic stubs: `__SMUAD`, `__SMLAD`, `__PKHBT`, `__PKHTB`, `__SSAT`, `__USAT`, `__CLZ`, `__RBIT`, `__REV`, `__SXTB16`, `__SIMD32`
- SDL3 window (240x320 @ 2x scale) with event loop

### Phase 2: UI Alive (2026-04-18)

Firmware UI renders in the SDL window. Full menu navigation with keyboard and mouse.

- 300+ firmware `.cpp` files compile and link natively
- FatFS shim (`ff.h`, `ff_emu.cpp`) with type-compatible struct layouts matching `ffconf.h` settings
- Framebuffer IO shim intercepts ILI9341 LCD commands (CASET, PASET, RAMWR, RAMRD) and writes RGB565 pixels to a 240x320 `uint16_t` array
- Firmware's M0 `EventDispatcher` runs in its own `std::thread`
- SDL main thread renders framebuffer to texture at 60fps
- Timer threads: frame sync (60Hz `EVT_MASK_LCD_FRAME_SYNC`) and RTC (1Hz `EVT_MASK_RTC_TICK`)
- Input: arrow keys = d-pad, Enter = select, Escape = back (Left+Up), Backspace = DFU, mouse scroll = encoder
- CMake header patching system for 64-bit Clang compatibility (`message.hpp`, `ui_widget.cpp`, `ui_receiver.hpp`, etc.)
- In-place header overrides for same-directory `#include` resolution

### Phase 3: Baseband Integration (2026-04-19)

Virtual baseband processing pipeline. Spectrum analyzer runs with synthetic RF data.

- **Processor registry** in `core_control_emu.cpp` maps `image_tag_t` to factory functions
- **`m4_init()`** creates processor instance, spawns M4 thread with `BasebandEventDispatcher`
- **Virtual DMA** (`baseband_dma_emu.cpp`) generates synthetic RF samples: Gaussian noise (~-22dBFS) + test tone at bin 64/256, delivered in 2048-sample `complex8_t` buffers at ~500Hz
- **Hardware stubs** (`baseband_hw_emu.cpp`): SGPIO, RSSI DMA, audio DMA, I2S -- all no-ops
- **M0-M4 signaling**: `m0apptxevent::assert_event()` signals M4 thread via `chEvtSignal`; `MessageQueue::signal()` calls `EventDispatcher::check_fifo_isr()` to wake M0
- **EventDispatcher rename**: M4's `EventDispatcher` patched to `BasebandEventDispatcher` via CMake to avoid ODR collision
- **Shutdown fix**: `ShutdownMessage` handler patched to clear `baseband_message` pointer (firmware bug without PRALINE)
- Compiled baseband files: `baseband_processor.cpp`, `baseband_thread.cpp`, `rssi_thread.cpp`, `event_m4.cpp`, `spectrum_collector.cpp`, `proc_wideband_spectrum.cpp`

### Phase 3.5: Audio Processors with SDL3 Audio Output (2026-04-19)

Three audio demodulators compile and register. SDL3 audio output stream replaces the no-op audio DMA sink.

- **Registered processors**: `NarrowbandAMAudio` (`PAMA`), `NarrowbandFMAudio` (`PNFM`), `WidebandFMAudio` (`PWFM`)
- **DSP pipeline compiled**: `dsp_decimate.cpp`, `dsp_demodulate.cpp`, `dsp_hilbert.cpp`, `dsp_modulate.cpp`, `dsp_iir.cpp`, `audio_output.cpp`, `audio_compressor.cpp`, `audio_stats_collector.cpp`
- **ARM DSP intrinsics extended** (`lpc43xx_cpp.hpp`): added `__SMMULR`, `__SMLADX`, `__SMUADX`, `__SMUSD`, `__SMUSDX`, `__SMLSD`, `__SMLALD`, `__SMLALDX`, `__SMLSLD`, `__SMULBB/BT/TB/TT`, `__SMLABT`, `__SMLATT`, `__QADD`, `__QSUB`, `__QADD16`, `__QSUB16`, `__SXTH`, `__SXTAH`, `__BFI`. Fixed `__SXTB16` to take ROR argument and `__REV16` to operate on 32-bit halfword-swap (was incorrectly 16-bit byteswap). `__SIMD32` macro now uses C-style cast to handle `const void*` sources.
- **CMake header patches**:
  - `simd.hpp` and `utility_m4.hpp`: dropped `#if defined(LPC43XX_M4)` guard so DSP helpers (`smlad`, `smlsd`, `pkhbt`, `pkhtb`, `sxtb16`, `rev16`, `multiply_conjugate_s16_s32`) compile in the unified binary
  - `audio_compressor.hpp/.cpp`: changed three `static constexpr float` using `std::pow/log10`/`std::exp` to `static inline const float` / `const float` (libc++ doesn't treat these math funcs as `constexpr`)
  - `proc_{am,nfm,wfm}_audio.cpp`: stripped `int main()` (conflicts with `main_emu.cpp`) and demoted `constexpr size_t` → `const size_t` for locals derived from instance-member `static constexpr` values (Clang is stricter than GCC-ARM here)
- **`PRALINE` define** scoped to `proc_am_audio.cpp` + `proc_nfm_audio.cpp` only, via `set_source_files_properties(... COMPILE_DEFINITIONS PRALINE)`. Selects the Phase-2 manual-thread-start pattern (`auto_start=false` in member init + explicit `baseband_thread.start()` in constructor body). `WidebandFMAudio` has no PRALINE guard and auto-starts threads like `WidebandSpectrum` does.
- **SDL3 audio sink** in `baseband_hw_emu.cpp`:
  - `init_audio_out()` opens `SDL_AudioStream` (48 kHz, stereo, s16) via `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, ...)` and resumes playback
  - `tx_empty_buffer()` rotates between two 32-sample staging slots; on each call it pushes the previous slot's contents via `SDL_PutAudioStreamData`, then returns the next slot for the caller to fill. This matches the firmware's pattern where the returned buffer is filled in place before the next call.
  - `disable()` destroys the stream cleanly
- **`SDL_INIT_AUDIO`** added to `SDL_Init` in `main_emu.cpp`

### Phase 7: CLI & Diagnostics (2026-04-19)

`main_emu.cpp` now parses long-form options, supports a fully headless mode, drives the firmware from scripted keystrokes, and prints a backtrace on fatal signals — enough to automate smoke tests without a human at the keyboard.

- **CLI flags** (`parse_cli` in `main_emu.cpp`): `--help/-h`, `--headless`, `--duration=SEC`, `--bezel=0|1`, `--iq-file=`, `--iq-loop=`, `--iq-tcp=`, `--iq-center=`, `--rtl-tcp-server=`, `--sdcard-root=`, `--pmem-file=`, `--keys=`, `--key-step=MS`. Env-equivalent flags set the env var via `::setenv(..., 1)` before the corresponding loader runs (so all downstream code, including `make_default_source`, `make_default_server`, SD root, pmem, and bezel toggle, is oblivious to the CLI path). Unknown flags print a message and exit 2.
- **Headless mode**: skips `SDL_CreateWindow/Renderer/Texture` (initializes only `SDL_INIT_AUDIO` so audio still routes), drops into a no-op sleep loop that wakes on `g_quit_requested`. Firmware thread, RTC tick, frame sync, baseband DMA, rtl_tcp server, and scripted-key injection all still run. macOS still requires main thread for SDL, so the sleep loop sits on the main thread.
- **Duration timer**: when `--duration=N` is set, a background thread flips `g_quit_requested` after N seconds, triggering the normal shutdown path. Checks in 100 ms slices so it exits promptly on manual quit too.
- **Scripted keys** (`scripted_keys_thread_fn`): waits for `g_firmware_running` + 500 ms splash settle, then walks the `--keys` string. Char mapping: `U/D/L/R` = d-pad, `S` = Select, `B` = Back (Left+Up), `F` = Dfu, `+`/`-` = encoder tick, `.` = 200 ms pause. Each press calls `emu_set_switches(mask)` + `EventDispatcher::events_flag(EVT_MASK_SWITCHES)`, sleeps `--key-step` ms (default 180), then releases with `emu_set_switches(0)`. Works in both headless and interactive modes.
- **Crash handler** (`install_crash_handler`): `sigaction` on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT with `SA_RESETHAND | SA_NODEFER`. Handler uses only async-signal-safe calls: `write` a marker, `backtrace` + `backtrace_symbols_fd` to stderr, then re-raise so the OS still produces a core dump / Crash Reporter entry with accurate state. Installed as the first action in `main`.
- **Verified**:
  - `./build/emuhem --help` prints the usage and exits 0.
  - `./build/emuhem --headless --duration=2` runs the firmware with no window, exits cleanly after 2 s with `--duration elapsed, quitting`.
  - `./build/emuhem --headless --duration=3 --keys='DDS'` logs `keys: scheduling 'DDS'` → `keys: script complete`, and firmware receives the button events.
  - `./build/emuhem --iq-center=100000000 --rtl-tcp-server='[::1]:15798' --headless --duration=1` activates the NCO (`source = nco-shift(center=100000000)->noise+tone`) and binds IPv6 loopback.
  - Unknown flag `--bogus` exits 2 with a helpful message.

### Phase 6c: Device Frame + Bracketed IPv6 (2026-04-19)

Window now renders a dark device bezel around the 240x320 LCD with a five-dot button-cluster placeholder below it; rtl_tcp host arguments accept the conventional `[ipv6]:port` bracket syntax.

- **Bezel** (`main_emu.cpp`): new `BEZEL_{LEFT,RIGHT,TOP,BOTTOM}` constants (20/20/20/80 virtual pixels). Window sizes to `(LCD + bezel) * SCALE` → 560x840 at 2x. Body is `RGB(38,38,44)`; a 3-pixel `RGB(15,15,18)` frame hugs the LCD edge; five `RGB(80,80,88)` dots sit in the bottom bezel in a D-pad layout (Left/Right/Up/Down + center Select). Purely decorative — input still comes from keyboard and mouse.
- **Mouse coordinates** now subtract the bezel origin before dividing by SCALE. Clicks outside the LCD area clamp to `[0, LCD_WIDTH)` / `[0, LCD_HEIGHT)` via the existing `enqueue_touch` guards, so decorative-dot clicks do nothing dangerous.
- **Override**: `EMUHEM_BEZEL=0` collapses all four margins to 0 and the window shrinks back to the bare `480x640` LCD. Useful for headless/automation scenarios and screenshot tests.
- **Bracketed IPv6** (`iq_source.cpp` `parse_host_port`): if the arg starts with `[`, split on the matching `]` then require `:` + port. `::1:15796` (no brackets) still works via `rfind(':')`, but `[::1]:15796` is now the preferred form and round-trips through `getaddrinfo` cleanly without bracket leakage.
- **Verified**: default launch prints `SDL window created (560x840 @ 2x, LCD 240x320, bezel L20 R20 T20 B80)` and shows the framed LCD; `EMUHEM_BEZEL=0` reverts to 480x640; `EMUHEM_RTL_TCP_SERVER='[::1]:15796'` logs `listening on ::1:15796` (no leftover brackets, no bind failure).

### Phase 6c (partial): NCO Frequency Shifting (2026-04-19)

File and noise sources can now be shifted by a numerically-controlled oscillator so recordings appear at the right spectrum offset when the user tunes the virtual radio away from the declared recording center.

- **`FrequencyShiftingSource`** (`iq_source.hpp/.cpp`): wraps any `IQSource`. For each `complex8_t` sample it multiplies by `exp(j*2*pi*(declared - tuned)*t/fs)` using a 1024-entry cos/sin lookup table indexed by the high 10 bits of a 32-bit fixed-point phase accumulator. Complex multiply runs in float then clamps back to int8.
- **Phase-increment math**: `inc = 2^32 * (declared - tuned) / fs`, signed. Computation done in double then folded into `[-2^31, 2^31)` via `fmod` so arbitrarily large shifts alias naturally.
- **Activation**: if `EMUHEM_IQ_CENTER=<hz>` is set and the resolved base source isn't `RtlTcpClientSource` (which tunes its own dongle), `make_default_source()` wraps the base in a shifter. Tuning updates arrive via the existing `on_center_frequency_changed` / `on_sample_rate_changed` hooks (plumbed in Phase 5.3).
- **Short-circuit when idle**: `inc == 0` (no shift configured, or `tuned == declared`) skips the multiply entirely — samples pass through untouched.
- **Network sources not wrapped**: rtl_tcp clients forward tuning to the remote dongle, which already centers the hardware; double-shifting would be wrong.
- **Diagnostic hook**: `EMUHEM_NCO_TEST=<rate_hz>:<tuned_hz>` at startup calls `on_sample_rate_changed` + `on_center_frequency_changed`, then reads 8 samples so the shift is observable without driving an app. Logs the shift in Hz, the 32-bit phase increment, and the rotated samples.
- **Verified**: declared=100 MHz, tuned=100.1 MHz, fs=2.4 MS/s → shift=-100 kHz, `inc=0xF5555555` (= -178956971 ≈ -2^32/24) ✓. Positive case (tuned=99 MHz) → shift=+1 MHz, `inc=0x6AAAAAAB` ≈ +2^32/2.4 ✓. Zero case (tuned==declared) → `inc=0`, samples unchanged ✓. Plain run without the env var leaves `baseband dma: source = noise+tone` (wrapper not constructed).

**Still deferred in Phase 6c:**
- Device frame / bezel rendering around the LCD (cosmetic).
- Windows Winsock2 + SD passthrough wrappers (needs a Windows box to validate).
- Bracketed `[::1]:port` host parsing for IPv6 addresses with `:` in them.

### Phase 6b: Persistent Memory Save/Load (2026-04-19)

Settings now persist across runs. The ~50 `persistent_memory::*` getters/setters are backed by a single process-local state struct, serialized to `~/.emuhem/pmem_settings.bin` (overridable via `EMUHEM_PMEM_FILE`).

- **State struct:** `EmuPmemState` in `persistent_memory_emu.cpp` — magic (`'SPEM'`) + version + every setting the shim currently surfaces (target_frequency, correction_ppb, touch_calibration, backlight_timer, all UI boolean toggles, theme, menu_color, CPLD, modem/POCSAG, converter, freq_step, encoder sensitivity, antenna_bias, battery_type, autostart_app, playing_dead, DST, ui_hide_*, etc.). Uses the firmware's own POD types where they're trivially copyable (`touch::Calibration`, `backlight_config_t`, `dst_config_t`). `static_assert(std::is_trivially_copyable_v<...>)` guards against accidental non-trivial members that would break memcpy serialization.
- **Load path:** `cache::init()` opens the file, validates magic+version+size (rejects stale layouts), copies on success or falls back to defaults. Called from `portapack::init()` in `portapack_emu.cpp`. Getters also lazy-load the first time they're called (if something runs before `portapack::init()`).
- **Save path:** `cache::persist()` writes an atomic snapshot — temp file + `fsync` + `rename(path.tmp, path)` — but only when the dirty bit is set. Setters compare-before-write (`memcmp`) and mark dirty only on actual change, so a steady-state UI doesn't thrash the disk. Firmware's `EventDispatcher::handle_rtc_tick` already calls `cache::persist()` once per second; shutdown doesn't need a special path because the RTC tick flushes it.
- **Atomic rename:** avoids torn writes if the emulator is killed mid-flush. POSIX `mkdir -p` is done by hand (looping over `/` boundaries) to avoid `<filesystem>`, which collides with the firmware's own `std::filesystem::path<char16_t>` redefinition in `file.hpp`.
- **Boilerplate:** `PMEM_GET` / `PMEM_SET` / `PMEM_SET_CONST_REF` macros collapse each accessor to one line. Non-trivial accessors (`target_frequency`, `correction_ppb`, `touch_calibration`, `menu_color`, `config_sdcard_high_speed_io`, `modem_repeat`) are hand-rolled.
- **Diagnostic hook:** `EMUHEM_PMEM_TEST=<freq_hz>` at startup calls `set_target_frequency(freq)` and logs the stored value, so the round-trip can be smoke-tested without driving the UI. Also logs `startup target_frequency=<hz>` unconditionally.
- **Verified round-trip:** `EMUHEM_PMEM_TEST=915000000` wrote a 160-byte state file; removing the env and relaunching loaded it and returned `915000000`.

### Phase 6a: Touchscreen Input + IPv6 rtl_tcp Server (2026-04-19)

Mouse clicks on the SDL window now reach firmware touch handlers (widgets, calibration screen, any `ui::View` that overrides `on_touch`). The rtl_tcp server binds on both IPv4 and IPv6.

- **Touch injection path:** SDL `MOUSE_BUTTON_DOWN/UP/MOTION` in `main_emu.cpp` → `enqueue_touch(type, x/SCALE, y/SCALE)` → mutex-guarded deque → dedicated injector thread pulls events and calls `EventDispatcher::emulateTouch(ev)`, which spins-waits until the M0 event loop's `handle_shell()` consumes `injected_touch_event`. This matches the firmware's own shell-mode touch emulator (used by the USB serial debug shell).
- **Why a worker thread:** `emulateTouch` blocks until the event is consumed (it spins with 5ms sleeps watching the `injected_touch_event` pointer). Running it directly on the SDL thread would stall the render loop during firmware busy periods, so a single dedicated injector thread drains the queue and blocks one event at a time.
- **Move dedup:** same-pixel Move events are dropped at enqueue time (the firmware re-runs the whole widget hit-test on every Move).
- **Dispatcher pointer:** `EventDispatcher* event_dispatcher_ptr` is set in `firmware_thread_fn()` right after construction and cleared when the thread exits. `emuhem_inject_touch(x, y, type)` (defined in `phase2_stubs.cpp`) is the extern "C" bridge from `main_emu.cpp`.
- **Coordinate conversion:** SDL event coordinates are already in window-space pixels; dividing by `SCALE` (2x) maps to the 240x320 LCD grid, then clamped. No calibration matrix is needed because we inject post-calibration events via `emulateTouch`, bypassing `touch::Manager` and the PortaPack calibration math entirely.
- **IPv6 rtl_tcp server:** `RtlTcpServer::start` now uses `getaddrinfo(host, port, AI_PASSIVE|AF_UNSPEC)` and iterates results, setting `IPV6_V6ONLY=0` on IPv6 sockets so they dual-stack accept IPv4 too. Empty/`*`/`0.0.0.0`/`::` bind host arguments resolve to the wildcard address. Accept loop uses `sockaddr_storage` and logs the client address via `inet_ntop` on whichever family showed up. Verified with `EMUHEM_RTL_TCP_SERVER=::1:15795` -- client on IPv6 loopback drains 6.7 MB in 1.5s.

**Out of scope for 6a (still in Phase 6):**
- Persistent memory save/load (~100 getters/setters would need to be re-backed by a `data_t` blob serialized to `~/.emuhem/pmem_settings`; deferred until there's a concrete ask).
- Device frame rendering around the LCD (bezel, status LEDs).
- Windows Winsock2 wrappers.
- NCO per-antenna-port frequency shifting.

### Phase 5.3: rtl_tcp Server + Upstream Commands + WAV/CF32 (2026-04-19)

EmuHem can now act as an rtl_tcp **server** (so gqrx / SDR++ / GNU Radio can pull I/Q out of the emulator) in addition to being a client. Upstream tuning commands propagate from server clients through the DMA, and -- when the active source is an rtl_tcp client -- out to the remote dongle. FileIQSource accepts `.wav` (PCM stereo) and `.cf32` (complex float32) alongside the existing `.c8/.cu8/.cs16`.

- **`RtlTcpServer`** (`iq_source.hpp/.cpp`): binds `EMUHEM_RTL_TCP_SERVER=host:port` on IPv4 (empty host = INADDR_ANY); accepts up to 4 simultaneous clients; sends the standard 12-byte `"RTL0"` + tuner=5 (R820T) + gains=29 header; fans out each baseband transfer to every client via a per-client cu8 ring buffer (262 KB, ~85 ms at 3.072 MS/s, drop-oldest on overflow). `TCP_NODELAY` is set so small packets don't stall. Each client has a send thread and a recv thread; the recv thread handles the 5-byte rtl_tcp command packets:
  - `0x01` set_frequency -- logged
  - `0x02` set_sample_rate -- applied via `baseband::dma::set_sample_rate` so the DMA pacing matches what the client expects
  - `0x04` set_tuner_gain -- logged
  - other commands (agc, bias-t, etc.) accepted silently
- **Server-only producer** in `baseband_dma_emu.cpp`: when a server client is connected and no firmware baseband app is active, a parallel pump thread reads samples from the source and fans them out, paced to either the configured rate or 2.4 MS/s by default. This lets the emulator serve as a pure I/Q source (e.g., to drive gqrx) without requiring a firmware app to be launched. When a firmware app is active, the normal `wait_for_buffer` path handles fan-out and the pump idles.
- **Upstream command hooks** on `IQSource`: new virtual methods `on_sample_rate_changed(u32)`, `on_center_frequency_changed(u64)`, `on_tuner_gain_changed(i32 tenths_db)`. Default is no-op; `RtlTcpClientSource` overrides to serialize the 5-byte rtl_tcp command packets and send them to the remote dongle. Duplicate values are suppressed via `last_*` atomics.
- **Plumbing** in `phase2_stubs.cpp`: `radio::set_tuning_frequency` and `radio::set_lna_gain` now call into the source's hooks via `extern "C"` bridges (`emuhem_iq_set_center_frequency`, `emuhem_iq_set_tuner_gain_tenths_db`) defined in `baseband_dma_emu.cpp`. `set_baseband_rate` was already plumbed in Phase 5.1.
- **FileIQSource `.wav`**: minimal RIFF walker looks for `fmt ` + `data` chunks; accepts stereo PCM at 8-bit (unsigned, 0x80-biased like cu8) or 16-bit (signed, >>8 to fit cs8). Channel 1 = I, channel 2 = Q.
- **FileIQSource `.cf32`**: complex float32 interleaved; assumes values in [-1, 1]; scaled to cs8 via clamp_to_i8(x * 127). Also accepts `.fc32` and `.raw` extensions.

**End-to-end verification:**
- Mock client connects to `EMUHEM_RTL_TCP_SERVER=127.0.0.1:15790`, receives the RTL0 header, sends set_sample_rate/frequency/gain, drains ~4.6M cu8 samples in 2s (matches the requested 2.4 MS/s).
- Chained test: mock rtl_tcp server on :15792 feeds EmuHem (via `EMUHEM_IQ_TCP`); an SDR client connects to EmuHem's server on :15793 and sends set_sample_rate(2.4 MHz); EmuHem forwards that upstream to the mock server, which logs `cmd=0x02 param=2400000` -- full tuning round-trip works.
- `.wav` (24k stereo 16-bit PCM) and `.cf32` (24k complex float32) both load and show `loaded 24000 samples` in the startup log.

**Out of scope for 5.3 (future):**
- Sending sample-format negotiation commands (fixed cu8 for now).
- IPv6 bind for the server.
- Windows Winsock2 wrappers (macOS/Linux only today).
- NCO frequency shifting per antenna port.

### Phase 5.2: rtl_tcp Client Source (2026-04-19)

EmuHem can now pull live I/Q from any rtl_tcp-compatible server (a real rtl-sdr dongle driven by `rtl_tcp -a 0.0.0.0`, another emulator, or a custom producer).

- **`RtlTcpClientSource`** (`iq_source.hpp/.cpp`): connects via `getaddrinfo`+`connect`, reads the 12-byte `"RTL0"` + tuner-type + gain-count header, validates the magic, then spawns a receiver thread. The thread reads cu8 bytes from the socket, XORs 0x80 to signed cs8, packs into `complex8_t`, and pushes into a mutex-guarded ring buffer (262144 samples ≈ 85 ms @ 3.072 MS/s). `read()` drains the ring and zero-pads on under-run so the DMA pacing stays on schedule. When the ring is full, oldest samples are dropped (counter logged at shutdown).
- **Factory** (`make_default_source`): precedence is `EMUHEM_IQ_FILE` → `EMUHEM_IQ_TCP=host:port` → noise. Invalid host:port format, DNS failure, connect refusal, or header mismatch all log the reason and fall back to the next option.
- **Destructor**: sets stop flag, `shutdown(SHUT_RDWR)` + `close()` to break the blocked `recv`, joins the thread, logs any dropped-sample count.

**Out of scope for 5.2 (queued as 5.3):**
- rtl_tcp **server** mode (letting gqrx/SDR++/GNU Radio pull I/Q from the emulator).
- Sending commands upstream (set frequency / gain / sample rate to the remote server) — currently the remote runs with its own defaults.
- `.wav`/`.cf32` containers.

### Phase 5.1: File I/Q Source + Realtime DMA Pacing (2026-04-19)

Replaces the hard-coded synthetic noise+tone generator with a pluggable `emuhem::IQSource` interface. A `FileIQSource` reads `.c8`/`.cs8`, `.cu8`, and `.cs16`/`.c16`/`.C16` captures into memory and serves them through the virtual DMA. The producer loop now paces itself against `radio::set_baseband_rate` using `std::chrono::steady_clock` deadlines, fixing the Phase 3.5 under-run where DMA ran far below 3.072 MS/s.

- **`src/platform/portapack_shim/iq_source.hpp`/`.cpp`**: `IQSource` abstract + `NoiseIQSource` (Gaussian noise + test tone at bin 64/256) + `FileIQSource` (loads whole file, optional loop). Format auto-detected by extension; `cu8` has the 0x80 bias removed, `cs16` is right-shifted by 8 to fit `complex8_t`. `make_default_source()` reads `EMUHEM_IQ_FILE` / `EMUHEM_IQ_LOOP` (loop defaults on).
- **`baseband_dma_emu.cpp`**: source is resolved once and owned behind a mutex. `wait_for_buffer()` calls `source->read(slice, 2048)` then paces via `std::this_thread::sleep_until(deadline)`. `set_sample_rate(hz)` recomputes the per-transfer interval and resets the deadline. `preload_source()` is called from `main_emu.cpp` at startup so env misconfigurations surface early.
- **`phase2_stubs.cpp`**: `radio::set_baseband_rate(rate)` now forwards into `baseband::dma::set_sample_rate(rate)` instead of being a no-op.
- **`main_emu.cpp`**: calls `emuhem_preload_iq_source()` right after `SDL_Init` so the file is loaded and logged at startup.
- **Pacing fallback**: when the sample rate is still 0 (before the first `set_baseband_rate` call), the producer reverts to the Phase 3 behavior of a 2 ms sleep per transfer. Once a rate is set (typical 3.072 MHz / 2.4 MHz / etc.), buffers are delivered on a steady-clock schedule matched to `2048/rate` seconds.

**Not yet in Phase 5:**
- rtl_tcp server for external SDR clients (gqrx/SDR++/GNU Radio) — follow-up (Phase 5.2).
- `.cf32`/`.wav` container formats — follow-up.
- Per-port frequency binding and NCO offset — follow-up.

### Phase 4: SD Card Passthrough (2026-04-19)

FatFS shim rewritten as a POSIX passthrough rooted at `~/.emuhem/sdcard/` (overridable via `$EMUHEM_SDCARD_ROOT`). Firmware file managers, settings load/save, capture RX/TX writers, and splash loaders now see real host files.

- **Root directory**: `$EMUHEM_SDCARD_ROOT` if set, else `$HOME/.emuhem/sdcard`. Auto-created on first FatFS call via `std::filesystem::create_directories`.
- **Path translation**: UTF-16 `TCHAR*` → UTF-8 → strip drive prefix (`0:`, `SD:`, etc.) → strip leading `/` → reject `..` traversal → join with SD root. BMP + surrogate pairs handled inline (no `std::codecvt`).
- **File handles**: firmware-owned `FIL`/`DIR` structs stay as-is; `std::unordered_map<FIL*,FileHandle>` and `std::unordered_map<DIR*,DirHandle>` guarded by a mutex hold the POSIX state. After every op the shim writes `fp->fptr` / `fp->obj.objsize` / `fp->err` so the firmware's `f_tell`/`f_size`/`f_eof`/`f_error` macros remain consistent.
- **Operations implemented**: `f_open`, `f_close`, `f_read`, `f_write`, `f_lseek` (with zero-fill when seeking past EOF on writable files), `f_truncate`, `f_sync` (`fflush`+`fsync`), `f_opendir`, `f_closedir`, `f_readdir`, `f_findfirst`/`f_findnext` (`fnmatch` with `FNM_CASEFOLD`), `f_stat`, `f_unlink`, `f_rename`, `f_mkdir`, `f_getfree` (`statvfs` → free clusters@512B), `f_chmod` (AM_RDO only), `f_utime` (`utimensat`), `f_putc`/`f_puts`/`f_gets`/`f_printf` (UTF-8 on disk per `_STRF_ENCODE=3`). Directory iteration uses `std::filesystem::directory_iterator` to avoid a `DIR` typedef collision between `ff.h` and `<dirent.h>`.
- **FILINFO**: size from `stat`; attrib = `AM_DIR`/`AM_ARC` with `AM_RDO` when `S_IWUSR` is clear; `fdate`/`ftime` converted from `st_mtime` via `localtime_r` into packed FAT format; `fname` converted to UTF-16.
- **errno → FRESULT**: `ENOENT` → FR_NO_FILE (FR_NO_PATH if parent missing), `EACCES/EPERM` → FR_DENIED, `EEXIST` → FR_EXIST, `ENOTDIR` → FR_NO_PATH, `EISDIR` → FR_DENIED, `ENAMETOOLONG` → FR_INVALID_NAME, `ENOMEM` → FR_NOT_ENOUGH_CORE, `EROFS` → FR_WRITE_PROTECTED, else FR_DISK_ERR.
- **Kept as-is**: `_SYNC_t` semaphore glue, `get_fattime`, Unicode helpers (`ff_convert`, `ff_wtoupper`), `disk_*` stubs.

---

---

## Current Architecture

### Threading Model

```
Main thread (macOS requirement for SDL):
  SDL event loop → input translation → framebuffer blit at 60fps

Firmware thread (M0 application core):
  EventDispatcher::run() → handle switches, encoder, LCD frame sync,
  application queue (M4→M0 messages), RTC tick

M4 thread (baseband, spawned by m4_init):
  BasebandEventDispatcher::run() → handle baseband messages (M0→M4),
  spectrum events

BasebandThread (spawned by processor constructor):
  wait_for_buffer() → processor->execute(buffer) loop

RSSIThread (spawned by processor constructor):
  wait_for_buffer() → statistics collection loop

Frame sync timer thread: 60Hz EVT_MASK_LCD_FRAME_SYNC
RTC timer thread: 1Hz EVT_MASK_RTC_TICK
```

### Message Flow

```
M0 → M4:
  baseband::send_message(&msg)
  → shared_memory.baseband_message = &msg
  → creg::m0apptxevent::assert_event()
  → chEvtSignal(g_m4_event_thread, EVT_MASK_BASEBAND)
  → BasebandEventDispatcher dispatches to processor->on_message()
  → shared_memory.baseband_message = nullptr (unblocks M0 spin)

M4 → M0:
  shared_memory.application_queue.push(msg)
  → MessageQueue::signal()
  → EventDispatcher::check_fifo_isr()
  → chEvtSignal(m0_thread, EVT_MASK_APPLICATION)
  → M0 EventDispatcher::handle_application_queue()
  → message_map routes to registered handlers (e.g., WaterfallView)
```

### Spectrum Data Flow

```
Virtual DMA → synthetic noise + test tone (2048 complex8_t samples)
  → BasebandThread::run() → WidebandSpectrum::execute()
  → accumulate 256-bin spectrum → SpectrumCollector::feed()
  → fft_swap() → post_message() → EVT_MASK_SPECTRUM
  → SpectrumCollector::update() → 256-point FFT → dB conversion
  → ChannelSpectrum pushed into FIFO
  → ChannelSpectrumConfigMessage → application_queue → M0
  → WaterfallView stores FIFO pointer
  → frame sync → WaterfallView drains FIFO → renders waterfall
```

---

## Directory Layout

```
EmuHem/
  CMakeLists.txt                              # Build system with header patching
  src/
    main_emu.cpp                              # Entry point, SDL loop, firmware thread
    platform/
      chibios_shim/
        ch.h                                  # ChibiOS kernel API → C++23 std library
        hal.h                                 # LPC43xx peripheral register stubs
        chcore.h, chheap.h                    # ARM context stubs
        chibios_shim.cpp                      # Thread, mutex, event, semaphore impl
        hal_instances.cpp                     # Global peripheral instances
      fatfs_shim/
        ff.h, integer.h, diskio.h             # FatFS type-compatible headers
        ff_emu.cpp                            # Stub implementations (f_open → FR_NO_FILE)
      lpc43xx/
        lpc43xx_cpp.hpp                       # Register stubs, ARM intrinsics, M0↔M4 bridge
        memory_map.hpp, spi_image.hpp         # Memory regions, image tags
        platform_detect_emu.c                 # Returns PortaPack model
      portapack_shim/
        portapack_io.hpp                      # Framebuffer IO (ILI9341 state machine)
        portapack_io_emu.cpp                  # LCD command → framebuffer pixel writes
        portapack_emu.cpp                     # Global objects, portapack::init()
        persistent_memory_emu.cpp             # ~100 settings stubs with safe defaults
        core_control_emu.cpp                  # Processor registry, m4_init(), M4 lifecycle
        irq_controls_emu.cpp                  # SDL keyboard → switch/encoder state
        rtc_time_emu.cpp                      # Host clock → firmware RTC
        debug_emu.cpp                         # Debug output → stderr
        phase2_stubs.cpp                      # ~600 lines: radio, audio, battery, I2C, etc.
        baseband_dma_emu.cpp                  # Virtual DMA with synthetic RF samples
        baseband_hw_emu.cpp                   # SGPIO, RSSI, audio DMA, I2S stubs
        i2s.hpp                               # I2S shim (no-op, replaces hardware header)
        hackrf_hal.hpp                        # Clock constants, I2C address
        usb_serial_device_to_host.h           # USB serial stub header
  mayhem-firmware/                            # Git submodule: firmware source
  hackrf/                                     # Git submodule: HackRF source
  docs/
    architecture.md                           # Full architecture plan (all phases)
    implementation_status.md                  # This file
```

---

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
./build/emuhem
```

**Requirements:** macOS with Clang 16+ (Xcode 15+), SDL3 (Homebrew), CMake 3.25+.

**Compile definitions:** `EMUHEM=1`, `LOCATE_IN_RAM=` (strips section attributes), `LPC43XX_M0=1`, `CORTEX_M4=0`, `FLASH_SIZE_MB=1`, plus various address constants.

**Key compiler flags:** `-fms-extensions` (pointer-to-int truncation), `-Wno-c++11-narrowing`, `-Wno-gnu-designator`, `-Wno-invalid-constexpr`.

**Linker:** `-Wl,-undefined,dynamic_lookup` for ~6 remaining deferred symbols from excluded app vtables.

---

## Registered Baseband Processors

| Image Tag | Class | Status |
|-----------|-------|--------|
| `PSPE` (wideband spectrum) | `WidebandSpectrum` | Compiled, registered |
| `PAMA` (AM audio) | `NarrowbandAMAudio` | Compiled, registered (Phase 3.5) |
| `PNFM` (narrowband FM) | `NarrowbandFMAudio` | Compiled, registered (Phase 3.5) |
| `PWFM` (wideband FM) | `WidebandFMAudio` | Compiled, registered (Phase 3.5) |
| Others (53 remaining) | Various | Not yet compiled |

---

## What Works

- Full firmware UI renders: splash screen, main menu, all sub-menus, settings, debug screens
- Keyboard navigation through entire menu tree
- Mouse scroll for encoder rotation
- Baseband processor lifecycle: start, message passing, shutdown, restart
- Virtual DMA generates synthetic RF samples for spectrum processing
- FFT pipeline runs (256-point FFT via `fft_c_preswapped`)
- AM/NFM/WFM audio demodulators compile and register; `m4_init` instantiates them on demand
- Audio DMA writes route to SDL3 `SDL_AudioStream` at 48 kHz stereo s16
- Full DSP intrinsic coverage (`__SMUAD/SMLAD/SMLADX/SMUSD/SMLSD/SMLSLD/SMLALDX/SMULxx/QADD/QSUB/QADD16/QSUB16/SXTB16/SXTH/SXTAH/BFI/SSAT/USAT/PKHBT/PKHTB/REV/REV16/CLZ/RBIT/SMMULR`)
- M0↔M4 message passing via SharedMemory queues
- **SD card passthrough**: FatFS operations (open/read/write/opendir/readdir/stat/unlink/rename/mkdir/getfree/chmod/utime) hit real files under `~/.emuhem/sdcard/` (or `$EMUHEM_SDCARD_ROOT`). File browser, settings load/save, splash loader, and capture writers work against host files.
- **File I/Q source + realtime DMA pacing**: virtual DMA reads samples from `.c8`/`.cu8`/`.cs16` captures via `EMUHEM_IQ_FILE` and paces buffer delivery to the real baseband sample rate (from `radio::set_baseband_rate`), fixing under-runs the synthetic noise source exhibited at audio rates.
- **rtl_tcp client I/Q source**: via `EMUHEM_IQ_TCP=host:port`, EmuHem connects to a remote rtl_tcp server and streams live cu8 samples into the baseband pipeline (ring-buffered, cu8→cs8 on the fly). Upstream tuning (sample rate, center frequency, LNA gain) is pushed back to the remote dongle via the rtl_tcp command packets.
- **rtl_tcp server**: via `EMUHEM_RTL_TCP_SERVER=host:port`, external SDR clients (gqrx, SDR++, GNU Radio) connect to EmuHem and pull the current baseband I/Q as cu8. Clients may send set_frequency / set_sample_rate / set_gain upstream; set_sample_rate is applied to the DMA pacing. A server-only producer thread runs when no firmware app is active so EmuHem can serve I/Q without requiring an app launch.
- **FileIQSource formats**: `.c8` / `.cs8` / `.cu8` / `.cs16` / `.c16` (Phase 5.1), `.wav` PCM stereo (8- or 16-bit), `.cf32` complex float32 (Phase 5.3).
- **Touchscreen via mouse**: Left-click/drag on the SDL window injects touch Start/Move/End events into the firmware via `EventDispatcher::emulateTouch`, so any widget that overrides `on_touch` now reacts. IPv6 is supported on the rtl_tcp server (dual-stack IPv4/IPv6).
- **Persistent settings**: target_frequency, touch calibration, theme, hide-flags, converter config, audio mute, etc. all survive across runs via `~/.emuhem/pmem_settings.bin` (or `EMUHEM_PMEM_FILE`).
- **NCO frequency shifting**: `EMUHEM_IQ_CENTER=<hz>` wraps file / noise sources in a numerically-controlled oscillator that mixes the stream by `(declared - tuned)` Hz, so a capture recorded at one center is rendered at the right offset when the emulator is tuned elsewhere. rtl_tcp client sources are left unwrapped (their remote dongle handles tuning).
- **Device frame**: window renders a dark bezel around the LCD with a decorative D-pad dot cluster below; disable with `EMUHEM_BEZEL=0`.
- **Bracketed IPv6 host args**: `EMUHEM_RTL_TCP_SERVER='[::1]:port'` and `EMUHEM_IQ_TCP='[::1]:port'` are accepted alongside the unbracketed form.
- **CLI**: `--help`, `--headless`, `--duration=SEC`, `--bezel=0|1`, `--iq-file=`, `--iq-loop=`, `--iq-tcp=`, `--iq-center=`, `--rtl-tcp-server=`, `--sdcard-root=`, `--pmem-file=`, `--keys=...` (U/D/L/R/S/B/F/+/-/.), `--key-step=MS`. Env-equivalent flags set `EMUHEM_*` before loading.
- **Crash handler**: backtrace to stderr on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT, then re-raise for OS-level crash reports.
- Clean startup and shutdown with no crashes or thread leaks

## What Doesn't Work Yet

- **Windows** for rtl_tcp server/client and SD passthrough: POSIX sockets + POSIX filesystem headers only (needs Winsock2 + Win32 fs wrappers).
- **App launch by name**: `--keys='UDS'` style sequences work, but no `--app=<name>` flag that launches a specific app bypassing menu navigation.

---

## Next Steps

1. **Phase 6c (remaining)**: Windows SD passthrough + Winsock2 wrappers (needs a Windows test box).
2. **Phase 7 (remaining)**: `--app=<name>` direct-launch flag, automated test harness on top of `--headless --keys=`.