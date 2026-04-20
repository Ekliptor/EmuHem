# EmuHem Implementation Status

Last updated: 2026-04-20 (Phase 21 — integration tests validate TX sample emission via `--iq-tx-file`: 5 TX apps now assert non-empty CS8 output, proving the TX pipeline flows end-to-end not just that the UI rendered)

## Overview

EmuHem compiles ~310 PortaPack Mayhem firmware C++ source files natively on macOS using Clang/C++23, replacing hardware layers with desktop shims. The emulator renders the full Mayhem UI in an SDL3 window with keyboard/mouse navigation and runs a bidirectional baseband pipeline: RX from synthetic noise, `.c8`/`.cu8`/`.cs16`/`.cf32`/`.wav` files, remote `rtl_tcp` servers, or any SoapySDR-supported USB SDR (HackRF, RTL-SDR, Airspy, Lime, Pluto, Blade…); TX out to CS8 files or SoapySDR TX devices. 51 baseband processors (1 spectrum, 3 RX audio, 24 RX digital incl. WEFAX/NOAA APT/SSTV RX/Test, 20 TX modulators incl. SSTV TX, plus `capture` and `replay` on the RX/TX boundary) resolve through the `image_tag_t` → factory registry.

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

### Phase 21: validate TX sample emission in integration tests (2026-04-20)

Previously TX-app integration tests only checked "app launched + framebuffer rendered." That caught nothing about whether the TX baseband pipeline actually flowed samples — the `FileIQSink` / `SoapyIQSink` / NullIQSink could all have silently dropped the path and the smoke test wouldn't notice. Phase 21 closes that gap for the 5 apps that emit samples without user interaction.

- **`run_app_test.sh`**: optional third arg `"tx"` enables `--iq-tx-file=$TMP/tx.cs8` and asserts the CS8 file grew to ≥ 64 KiB after the 2-second run (≈ 32 K samples, ≈ 10 ms of TX at 3 MS/s — way below actual expected 6-12 MB, so immune to buffer-truncation-on-shutdown noise).
- **`tests/integration/CMakeLists.txt`**: new `INTEGRATION_TX_APPS` list (aprstx, touchtune, microphone, replay, rdstx) wires the `tx` mode through. New `INTEGRATION_PASSIVE_TX_APPS` list (bletx, ooktx) keeps those as regular smoke tests with a comment explaining why — both need a user "start TX" button press which the harness doesn't automate yet.
- **What this catches**: a regression where the processor runs but no buffer ever reaches `IQSink::write()` (e.g. `baseband::dma::wait_for_buffer` returning early, direction flag not latched, sink factory returning wrong type). The old smoke test would pass because the UI still paints; this one fails.
- **What this doesn't catch** (out of scope — would need per-app golden files or scripted decode): whether the emitted samples are *correct* (right modulation, right frequency, right bits). That's "TX loopback sink" territory (Next Steps #6).
- **Verified**: 22/23 typical single-run pass (1 TX-shutdown-latency flake as documented since Phase 20, alternating across `touchtune`/`rdstx`/`aprstx`/`bletx`/`ooktx` — the OS-scheduling-bound residual from Phase 20 is unchanged). Each app passes in isolation. Wall time ~160s serial (up from ~72s pre-Phase-21 due to 5× ~12 MB CS8 writes per run — tests disk I/O too).

### Phase 20: eliminate TX-shutdown-latency flake (2026-04-20)

Calling `baseband::shutdown()` from `firmware_thread_fn` after `event_dispatcher.run()` returns so the M4 dispatcher receives a `ShutdownMessage` promptly instead of waiting for static destructors at process exit. Cuts typical TX-app shutdown from ~60s (busy-loop spin-wait in `send_message` → `chDbgPanic` → abort handler → exit) down to ~4s (M4 dispatcher stops, BasebandThread joins cleanly).

**Before Phase 20:**
- Full sequential suite: ~130s with 1 failure, up to ~330s with 3 failures under heavy load.
- Individual TX apps (`aprstx`/`bletx`/`ooktx`/`touchtune`/`rdstx`/`replay`): 60+ seconds to exit because nothing sends ShutdownMessage until `~AppView` runs during C++ static-destructor teardown after `main()` returns. Meanwhile the script's `timeout 60` wrapper kills emuhem before it can finish, marking the test as failed.

**After Phase 20:**
- Full sequential suite: **~72s, 23/23 passing on clean runs.** Occasional 0-1 flake remains under heavy load (typically 1 TX app timing out when two ctest processes run in parallel).
- Individual TX apps: ~4s wall time, clean ordering `Shutting down... → Firmware thread exiting → M4 event dispatcher exited → Done.`

**Implementation**:
- [main_emu.cpp::firmware_thread_fn](src/main_emu.cpp): after `event_dispatcher.run()` returns, call `emuhem_shutdown_baseband()` which delegates to firmware's `baseband::shutdown()`.
- [core_control_emu.cpp](src/platform/portapack_shim/core_control_emu.cpp): added the thin `emuhem_shutdown_baseband()` wrapper with an `extern "C"`-style linkage so `main_emu.cpp` doesn't need to include the firmware's baseband headers.
- `baseband::shutdown()` is guarded by `baseband_image_running`, so it's a safe no-op for utility apps that never enable a baseband processor (`notepad`/`filemanager`/`freqman`/`iqtrim`).

**Background detail** (in case this ever needs revisiting):
- The firmware's `baseband::shutdown()` → `send_message(&shutdown_msg)` → writes `shared_memory.baseband_message` and spins until M4 clears it. The already-patched `event_m4.cpp` (CMakeLists.txt Phase 3) has the EmuHem-specific fix that always clears `baseband_message = nullptr` after handling `ID::Shutdown` (firmware source does it only `#ifdef PRALINE`). So the send_message returns promptly once M4 consumes the Shutdown.
- The M4 event dispatcher's `run()` exits when `request_stop()` is called from `on_message_shutdown`. The spawning lambda in `m4_init` then destructs the `BasebandEventDispatcher`, which destructs the unique_ptr `<BasebandProcessor>`, which destructs `BasebandThread`, which calls `chThdTerminate` + `chThdWait` on the M4 baseband thread. Clean cascade.
- Phase 16 attempted the same fix via `m4_request_shutdown()` (direct join) and crashed during dyld unwind; this phase uses firmware's own `baseband::shutdown()` message path, which doesn't touch the M4 thread lifecycle directly and avoids the crash.

### Phase 19: fix `rdstx` null-pointer crash (2026-04-20)

Last non-launching built-in app. `RDSProcessor::execute` dereferences `rdsdata[...]` (a `uint32_t*` that's null-initialized in the class body) before `RDSProcessor::on_message` has a chance to populate it from `shared_memory.bb_data.data` via the firmware's first `RDSConfigureMessage`. On target the null deref is silently benign (reset RAM reads as zeros, so `cur_bit` stays 0 and the loop keeps running on the zero-filled sample buffer); on x86/Apple Clang it SIGSEGVs immediately.

- **Fix**: single-line patch added via CMake `file(READ/REPLACE/WRITE)` to `proc_rds.cpp` — inject `if (!configured) return;` at the top of `execute()`, matching the guard pattern that almost every other processor already uses (MicTX, sonde, tpms, etc.).
- **Integration test**: `rdstx` added to `INTEGRATION_APPS` (now 21 apps). `INTEGRATION_APPS` comment updated: **no built-in apps are excluded anymore** — only the external-`.ppma` apps (SSTV/WEFAX/NOAA APT) from Phase 18 remain uncovered, because they're not dispatchable via `--app=<id>`.
- **Verified**: 23/23 CTest entries pass in isolation. Under full sequential load, the documented TX-shutdown-latency flake still claims 1-3 TX apps per run (aprstx/touchtune/rdstx/bletx/ooktx/replay all susceptible), each passes in isolation. This condition is unchanged from Phase 16 — not introduced by Phase 19.

### Phase 18: SSTV RX/TX, WEFAX RX, NOAA APT RX, TestProcessor (2026-04-20)

Five more baseband processors land, pushing registry coverage to **51 of ~55**. All five register cleanly via the existing strip macro (one needed a custom patch for a comment-line between `int main() {` and `init_audio_out()`) — no header collisions, no unlinked transitive `.cpp` deps.

- **`SSTVRXProcessor` (`image_tag_sstv_rx`)** — slow-scan TV image RX. `proc_sstvrx.cpp` main() has a `// Initialize audio DMA` comment between the brace and the audio init call, so `emuhem_strip_proc_main`'s standard pattern doesn't match; patched directly via `file(READ/REPLACE/WRITE)` like `proc_sonde.cpp`.
- **`SSTVTXProcessor` (`image_tag_sstv_tx`)** — SSTV TX. Standard strip, no audio init.
- **`WeFaxRx` (`image_tag_wefaxrx`)** — weather fax image RX. Standard strip, audio_out init.
- **`NoaaAptRx` (`image_tag_noaaapt_rx`)** — NOAA APT satellite weather image RX. Same strip pattern as WeFaxRx.
- **`TestProcessor` (`image_tag_test`)** — dev/diagnostic AIS-test processor (referenced only by a commented-out `testapp` entry in `ui_navigation.cpp`). Registered so the image tag resolves, but not reachable from the UI.

**Caveat — no new integration tests.** All five processors correspond to apps that are either **external** (loaded from SD as `.ppma` plugins, not in `ui_navigation.cpp::appList`) — SSTV RX/TX, WEFAX RX, NOAA APT RX — or to a commented-out stub (`testapp`). `--app=sstvrx` and siblings therefore print `unknown app id` and fall through to the menu; there's nothing EmuHem-side to dispatch. What Phase 18 delivers is the *baseband plumbing* — once external `.ppma` loading lands (out of scope; separate track), these processors are already resident and the image tags resolve through `core_control_emu.cpp`'s registry.

**What's not covered here** (deliberately, would have needed separate scope):
- Image-rendering hooks: these processors push pixel data via `shared_memory.application_queue`; the M0 consumers (in the external apps) would normally paint into their views. EmuHem doesn't render anything extra for them — the existing UI pipeline handles it whenever/if the external-app viewer reaches the drawing path.
- `AM TV` (`image_tag_am_tv`): skipped. Its header defines `class WidebandFMAudio` at global scope which would collide with `proc_wfm_audio.hpp`. Same class-name-collision pattern that cost us half a day in Phase 17; not worth the third rename round for a processor whose app isn't in appList either.
- `proc_bint_stream_tx`, `proc_sigfrx`, `proc_flash_utility`, `proc_sd_over_usb`, `proc_pocsag` (v1): no image tag constant or no file at all — not registerable.

**Verified**: clean build, 22 CTest entries (2 unit + 20 integration), previously green apps still green. No new integration tests (external apps aren't dispatchable from appList). Full-suite sequential run remains subject to the documented TX-shutdown-latency flake on 1-2 of the 5 TX apps per full run; each passes in isolation.

### Phase 17: capture / replay / MicTX processors (2026-04-20)

Three more baseband processors land, closing the last non-launching apps from Phase 15's excluded list. 46 of ~55 total firmware processors are now registered; the integration suite grows from 17 to 20 apps.

- **`capture` (`image_tag_capture`)** — wideband RX recorder (I/Q to SD card). `proc_capture.hpp` defined `class MultiDecimator` and `class NoopDecim` at global scope, colliding with `proc_fsk_rx.hpp`'s same-named classes when both processors live in one TU (EmuHem registry). Renamed to `CaptureMultiDecimator` / `CaptureNoopDecim` via CMake `string(REPLACE)` in the patched `proc_capture.hpp`; `proc_capture.cpp` is also patched to track the `NoopDecim` template-arg reference.
- **`replay` (`image_tag_replay`)** — I/Q replay TX. Clean strip via the standard macro, no collision. TX shutdown latency is the same as other TX apps (tracked under "Unfinished shims") but in bounds.
- **`microphone` (`image_tag_mic_tx`, MicTXProcessor)** — audio TX from mic input. Needed an `init_audio_in()` variant of `emuhem_strip_proc_main`, plus two previously-unlinked baseband translation units:
  - `tone_gen.cpp` — `ToneGen::configure` / `::process` / `::process_beep` are called by `dsp::modulate::FM` for CTCSS/tone-key mix, but the firmware never compiled `tone_gen.cpp` outside a per-proc binary. Apple ld resolved the symbol to 0x0; MicTX's first `AudioTXConfig` message jumped there.
  - `audio_input.cpp` — `AudioInput::read_audio_buffer` is MicTX's hot path (reads `rx_empty_buffer()`, copies right channel to s16). Same "linker permissive, crash at 0x0" pattern.
- **Macro extension**: `emuhem_strip_proc_main` now accepts a third-arg value of `IN` (in addition to `TRUE`/`FALSE`) for processors whose `main()` starts with `audio::dma::init_audio_in()`. Cleaner than a fourth macro.
- **Integration tests**: `tests/integration/CMakeLists.txt::INTEGRATION_APPS` now covers 20 apps (added `microphone`, `replay`, `capture`). Only excluded app is `rdstx` (internal state-machine crash, still pre-existing).
- **Verified**: 22/22 CTest entries green (2 unit + 20 integration), serial wall time ~69s.

### Phase 16: Integration test harness (2026-04-20)

Per-app smoke tests via CTest that exercise the full `--app=<id>` path on the real `emuhem` binary. Complements Phase 13's utility-level unit suite — catches regressions in the launch path, baseband lifecycle, and UI rendering that Phase 13 can't see.

- **New directory**: `tests/integration/` with `run_app_test.sh`, `CMakeLists.txt`, `README.md`.
- **Per-app test**: launches `emuhem --headless --duration=2 --app=<id> --fb-dump=<tmp>` and asserts (1) exit 0, (2) no `CRASH`/`PANIC` in stderr, (3) `--app: launched '<id>'` in stderr (Phase 14 dispatch), (4) final framebuffer is not byte-identical to all zeros (rendering happened — deliberately loose, no pixel hashes).
- **17 apps covered**: all 13 from Phase 15's clean-launch matrix + `notepad`/`filemanager`/`freqman`/`iqtrim` utility apps. `rdstx`/`microphone`/`capture`/`replay` excluded — documented in `tests/integration/README.md`.
- **Per-test isolation**: each test sets `$EMUHEM_PMEM_FILE` + `$EMUHEM_SDCARD_ROOT` to fresh `mktemp -d` paths so autostart/last-app settings don't leak between tests.
- **Serialization via `RESOURCE_LOCK "emuhem_headless"`**: multiple concurrent `emuhem` processes contend on SDL/CoreAudio device ownership, causing intermittent hangs. Integration tests are serialized even with `ctest -jN`. Unit tests stay parallel.
- **`EMUHEM_NO_AUDIO_OUT=1` opt-out**: `baseband_hw_emu.cpp::init_audio_out` skips `SDL_OpenAudioDeviceStream` when set — CoreAudio's default-device handle doesn't always release fast enough between back-to-back emuhem runs, and audio output isn't observable during smoke tests anyway. Test script exports this env.
- **New CLI flag**: `--fb-dump=PATH` writes the final LCD framebuffer as raw RGB565 (240×320×2 = 153600 bytes) to PATH on shutdown. Enables the liveness assertion; useful for ad-hoc investigations too.
- **New signal handling (unrelated but necessary for graceful test termination)**: SIGTERM/SIGINT/SIGHUP now set `g_quit_requested` for a clean shutdown path; second signal of the same kind force-exits via the default handler. Previously only crash signals (SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT) were handled.
- **Stdout line-buffered via `setvbuf(stdout, nullptr, _IOLBF, 0)`**: shutdown-phase prints (`Shutting down...`, `Done.`, `--duration elapsed`) now interleave correctly with the unbuffered stderr from worker threads when redirected to a log. Essential for diagnosing test flake.
- **Verified**: 19/19 CTest entries passing (2 unit + 17 integration). Serial run ~60s.

### Phase 15: RX/TX apps reach baseband without panicking (2026-04-20)

Phase 14's `--app=<id>` flag exposed three unrelated, pre-existing bugs on the "real baseband send" path. All three are now fixed. Before Phase 15: `audio`, `pocsag`, `aprsrx` crashed on launch with `PANIC: Baseband Send Fail`. After Phase 15: every RX app and most TX apps reach their view with a running baseband processor.

**Root causes (three distinct bugs all triggered by the same code path):**

1. **M4 processor-switch race in `core_control_emu.cpp::m4_init`** — the shim spawned the new M4 event thread but returned immediately, relying on the firmware's `shared_memory.baseband_ready` spin-wait to synchronize. Under macOS thread scheduling this sometimes allowed the M0 to send a baseband message before the new M4 dispatcher installed `g_m4_event_thread`. Signal went nowhere; message spun to `chDbgPanic`. Also, `shutdown_m4_thread` left `shared_memory.baseband_message` pointing at the stale `shutdown_msg`, which the next dispatcher could dequeue and shut itself down. Fix: proper mutex + condition-variable rendezvous that blocks `m4_init` until the new dispatcher is constructed AND listening, plus explicit clearing of `baseband_message`/`baseband_ready` on shutdown.
2. **`dsp_squelch.cpp` was never compiled** — the top-level `CMakeLists.txt` Phase 3.5 list added `audio_output.cpp` + `audio_compressor.cpp` but not `dsp_squelch.cpp`, so `FMSquelch::set_threshold(float)` was an unresolved symbol. Apple's linker permitted it (same failure mode as Phase 14's `radio::set_baseband_rate`). At runtime, `NarrowbandFMAudio::configure` → `AudioOutput::configure` → jump to 0x0. Fix: added to the baseband source list.
3. **`ff_emu.cpp::f_getfree` wrote `*fatfs = nullptr`** — the firmware's `std::filesystem::space(path)` dereferences the returned `FATFS*` to compute `fs->csize * _MIN_SS` for cluster size. With nullptr it crashed inside every `RecordView::update_status_display` (called from every app that embeds a record button). Fix: back `*fatfs` with a process-wide static `FATFS{csize=1, n_fatent=total_clusters+2}` filled from POSIX `statvfs`.

**Apps now launching cleanly that did not before Phase 15:**
- RX: `audio` (AM/NFM/WFM), `pocsag`, `aprsrx`
- TX: `aprstx`, `bletx`, `ooktx`, `touchtune`
- Plus stability for every other app on the processor-switch path (first baseband::send_message no longer races).

**Apps still failing (different root causes, not Phase 15 scope):**
- `rdstx` — crashes inside `RDSProcessor::execute` itself (TX-processor-internal state machine issue, not the baseband lifecycle).
- `microphone` (MicTX) — needs an `init_audio_in()` variant in the `emuhem_strip_proc_main` macro; the processor isn't registered.
- `capture`, `replay` — processors not registered (`proc_capture`/`proc_replay` not yet ported).

**Verified regression-free**: CTest still 87/87 passing; 13-of-15 app-launch matrix clean; `./emuhem` with no flags behaves identically.

### Phase 14: `--app=<id>` direct-launch flag (2026-04-20)

Shortens the path from `./emuhem` to any firmware app. Previously every app required menu navigation via `--keys=` (D-pad taps, sleeps, pray the splash gets out of the way). Now scripted tests and ad-hoc investigations can target one app directly.

- **CLI flag**: `--app=<id>`. Sets `EMUHEM_APP` env var (mirrors the rest of the CLI → env pattern).
- **Dispatch**: `firmware_thread_fn` calls `system_view.get_navigation_view()->StartAppByName(app_name)` after nav is constructed, immediately before `event_dispatcher.run()`. Uses the firmware's own id table (`NavigationView::appList` in `ui_navigation.cpp`) — no parallel registry in EmuHem.
- **Supported ids**: every `appList` entry with a non-null `id` field. ~40 apps including `audio`, `adsbrx`, `ais`, `pocsag`, `aprsrx`, `weather`, `search`, `lookingglass` (wide spectrum), `recon`, `capture`, `replay`, `aprstx`, `bletx`, `ooktx`, `rdstx`, `microphone`, `filemanager`, `freqman`, `iqtrim`, `notepad`. Unknown ids log an error and fall through to the main menu.
- **Bug fix (unrelated but on-path)**: `phase2_stubs.cpp` forward-declared `baseband::dma::set_sample_rate` *inside* `namespace radio`, making the call inside `radio::set_baseband_rate` resolve to the nonexistent `::radio::baseband::dma::set_sample_rate`. The linker left it as an undefined symbol (Apple's linker is permissive), so at runtime calls crashed with PC=0x0. Never triggered by menu-only navigation; exposed the moment `--app=audio` reached `ReceiverModel::enable()`. Fix: forward-declare at the global scope.
- **Verified (pass)**: `--app=notepad`, `filemanager`, `freqman`, `iqtrim`, `adsbrx`, `ais`, `weather`, `search` all launch cleanly.
- **Verified (crash — pre-existing baseband shim limitation, not Phase 14 scope)**: `--app=audio`, `pocsag`, `aprsrx` reach the first baseband message send and time out with `PANIC: Baseband Send Fail`. The M4 event dispatcher's handling of processor-switch lifecycle drops messages in some window; menu-driven startup avoids this by never switching away from the initial WidebandSpectrum processor until baseband is fully settled. Tracked separately under "Unfinished shims".
- **Regressions**: none — `./emuhem` without `--app` behaves identically to Phase 13.

### Phase 13: CTest Unit Test Suite (2026-04-20)

EmuHem now has a native unit test suite wired into CMake via CTest. Ports the upstream firmware doctest suite at `mayhem-firmware/firmware/test/` to run against the firmware TUs EmuHem compiles — catches regressions in `file.cpp`, `freqman_db.cpp`, `string_format.cpp`, `utility.cpp`, `dsp_fft.cpp`, and friends whenever EmuHem's build is rebuilt or firmware is re-synced.

- **Framework: doctest** (reused from upstream, `mayhem-firmware/firmware/test/include/doctest.h`). No googletest/Catch2 fetch.
- **Two test binaries** mirroring upstream's split:
  - `emuhem_tests_application` — 10 test files from `firmware/test/application/` (basics, circular_buffer, convert, optional, string_format, utility, mock_file, file_reader, file_wrapper, freqman_db). **84 passing test cases, 461 passing assertions.**
  - `emuhem_tests_baseband` — `dsp_fft_test.cpp`. Explicitly adds `firmware/common/dsp_fft.cpp` (filtered out of EmuHem's main build by the baseband-only regex). **3 passing test cases, 48 passing assertions.**
- **Test sources NOT copied** — referenced in place from `mayhem-firmware/firmware/test/`. Matches how EmuHem consumes the rest of the firmware tree; upstream test updates flow through on rebuild.
- **New directory**: `tests/` containing `CMakeLists.txt`, `linker_stubs.cpp` (ported from upstream + `f_utime` / `freqman_dir` additions), and `README.md`.
- **Test targets are isolated**: no SDL3, no ChibiOS shim, no `main_emu.cpp` — pure-logic only. Rebuilds faster than the main binary.
- **Skipped test cases: 1** — `"It can parse frequency step"` in `test_freqman_db.cpp` hardcodes enum ordinal positions that drifted in upstream firmware. Excluded via doctest's `--test-case-exclude=` flag at the CTest layer. Not an EmuHem porting issue.
- **Patches applied**: (a) `std::std::abs` typo in `tone_key.cpp` via CMake `file(READ/REPLACE/WRITE)`; (b) `-ULPC43XX_M0` on the baseband target (EmuHem's main build is M0-flavored, baseband tests target M4 code paths); (c) `-include lpc43xx_cpp.hpp` on the baseband target so `dsp_fft.hpp`'s `__RBIT` intrinsic is defined.
- **Run**: `ctest --test-dir build --output-on-failure`.
- **Per-test status tracking**: `docs/tests_status.md` — one row per test file, reason-for-skip per skipped case, list of emulator features that would unblock any future skipped tests (currently: none pending).
- **Verified**: 87/87 runnable test cases pass, 509/509 runnable assertions pass, 0 failing. `emuhem` binary still builds and runs clean (regression).

### Phase 12: TX Path — `IQSink` abstraction (2026-04-20)

The 18 TX processors registered in Phases 9/10/11 now have somewhere to send their I/Q. Mirrors the RX-side `IQSource` design — same env-var precedence, same lazy-init, same tuning-hook interface.

- **`IQSink` base class** (`src/platform/portapack_shim/iq_source.hpp`): `write(complex8_t*, count)`, `name()`, plus optional `on_sample_rate_changed` / `on_center_frequency_changed` / `on_tx_gain_changed`.
- **`NullIQSink`**: default when no TX env var set. Discards samples, logs a dropped count every ~1M samples so the user is not surprised by silence.
- **`FileIQSink`**: writes raw CS8 (interleaved int8 I/Q) to a file. Byte-identical to `FileIQSource::Format::CS8` — a file produced by `--iq-tx-file=out.c8` loops back unmodified via `--iq-file=out.c8`.
- **`SoapyIQSink`** (guarded by `EMUHEM_HAS_SOAPYSDR`): opens a SoapySDR-supported TX device (HackRF, PlutoSDR, LimeSDR, BladeRF...), picks CS8 / CS16 / CF32 from `getStreamFormats(SOAPY_SDR_TX, 0)`, spawns a writer thread that drains a 256k-sample ring buffer into `writeStream`. Overflow drops oldest sample (latency cap over correctness when the host can't keep up). `on_*` hooks push setSampleRate/setFrequency/setGain onto the device.
- **Direction-aware `baseband_dma_emu.cpp`**: `configure()` now latches `baseband::Direction`. `wait_for_buffer()` branches: RX (unchanged) fills the slice from the source; TX drains the previous slice to the sink then hands out a fresh zeroed slice. `disable()` performs a final flush so the last 2048 samples aren't lost on a fast stop. `set_sample_rate` forwards to both source and sink. Tuning bridges (`emuhem_iq_set_center_frequency`, `emuhem_iq_set_tuner_gain_tenths_db`) also forward to the sink.
- **rtl_tcp fanout on TX**: transmitted I/Q is fanned out to any connected rtl_tcp listener the same way RX samples are. Lets you visualize your own modulator output in gqrx/SDR++ without extra hardware.
- **CLI flags** (main_emu.cpp): `--iq-tx-file=PATH`, `--iq-tx-soapy=<args>`, `--iq-tx-soapy-rate`, `--iq-tx-soapy-freq`, `--iq-tx-soapy-gain` — all mirror the RX flag names. Env equivalents: `EMUHEM_IQ_TX_FILE`, `EMUHEM_IQ_TX_SOAPY`, `EMUHEM_IQ_TX_SOAPY_RATE/FREQ/GAIN`.
- **Sink preload**: when any `EMUHEM_IQ_TX_*` env is set, the sink is instantiated eagerly in `preload_source()` so path typos / missing-device errors surface at startup rather than on first TX app launch.
- **`EMUHEM_TX_TEST=<count>` diagnostic**: startup-time hook that writes a `count`-sample ramp pattern through the active sink (mirrors the existing `EMUHEM_NCO_TEST` diagnostic for RX). Lets the TX path be validated without launching a firmware TX app.
- **Verified**: clean build. Regression matrix (baseline, `--iq-file`, `--soapy`, `--iq-tx-soapy=driver=nosuchdriver`) all clean. End-to-end TX write: `EMUHEM_TX_TEST=4096 --iq-tx-file=/tmp/tx.c8` produces exactly 8192 bytes of CS8 ramp data. Round-trip verified: `--iq-tx-file=out.c8` → `--iq-file=out.c8` reads back the same sample count.

Out of scope (deferred): rtl_tcp-as-TX-client, TX-to-RX loopback, `radio::disable_rf_output` plumbing, WAV/CS16/CF32 output formats.

### Phase 11: 18 More Baseband Processors (2026-04-20)

Registry now maps 43 image tags to factories. Added 9 RX decoders plus 9 TX modulators.

- **RX decoders (9)**: `ACARSProcessor` (`PACA`), `ADSBRXProcessor` (`PADR`, iconic ADS-B aircraft tracking), `FlexProcessor` (`PFLX`, FLEX paging), `SondeProcessor` (`PSON`, weather balloons), `EPIRBProcessor` (`PEPI`, emergency beacons), `ToneDetectProcessor` (`PTNE`), `SubCarProcessor` (`PSCD`), `RTTYRxProcessor` (`PRTR`), `MorseProcessor` (`PMRS`).
- **TX modulators (9, silent)**: `MorseTXProcessor` (`PMRT`), `RTTYTXProcessor` (`PRTT`), `JammerProcessor` (`PJAM`), `P25TxProcessor` (`P25T`), `GPSReplayProcessor` (`PGPS`), `SpectrumPainterProcessor` (`PSPT`), `AudioTXProcessor` (`PATX`), `TimeSinkProcessor` (`PTSK`), `EPIRBTXProcessor` (`PEPT`).
- **Macro idempotency fix**: the Phase-10 `FskMultiDecimator` rename did `file(COPY_FILE ... ONLY_IF_DIFFERENT)` back into `FW_DIR`, which contaminated the source on re-configure (each run pre-pended another `Fsk`). Removed the in-place copy; `proc_fsk_rx.cpp` lives in `PATCHED_FW_DIR` so same-dir include already resolves to the patched header. Renamed via a sentinel pattern (`MultiDecimator<` → `__SENTINEL_MD__<` → `FskMultiDecimator<`) to make the replace idempotent against already-renamed text.
- **`proc_sonde` separate patch**: its `main()` has an extra blank line between `event_dispatcher.run();` and `return 0;` that the standard macro doesn't handle. Patched directly with a dedicated `file(READ/REPLACE/WRITE)` block.
- **`proc_time_sink` static-call rename**: only processor where `EventDispatcher::events_flag(...)` is called statically (not just as a local type in `main()`). The Phase-3 rename of `EventDispatcher` → `BasebandEventDispatcher` only happened in `event_m4.{h,c}pp`, so patched `proc_time_sink.cpp` after the macro strip to rename the static call site.
- **`proc_flex` EccContainer duplicate**: `proc_flex.cpp` reimplements `pocsag::EccContainer::{ctor,setup_ecc,error_correct}` inline, explicitly to avoid linking `pocsag.cpp` in its standalone binary. In EmuHem both `POCSAGProcessor` and `FlexProcessor` are compiled so both `pocsag.cpp` and `proc_flex.cpp` link — linker errors on duplicate symbols. Wrapped the duplicated block in `#if 0 ... #endif` via CMake patching.
- **Verified**: clean build; headless smoke test passes; 43 tags registered (4 from Phase 3.5, 7 from Phase 9, 14 from Phase 10, 18 from Phase 11).

### Phase 10: 14 More Baseband Processors (2026-04-20)

Registry now maps 25 image tags to factories. Added 8 RX digital decoders plus 6 TX modulators.

- **RX digital decoders (8)**: `AFSKRxProcessor` (`PAFR`), `APRSRxProcessor` (`PAPR`), `BTLERxProcessor` (`PBTR`), `NRFRxProcessor` (`PNRR`), `FSKRxProcessor` (`PFSR`), `WeatherProcessor` (`PWTH`), `SubGhzDProcessor` (`PSGD`), `ProtoViewProcessor` (`PPVW`).
- **TX modulators (6)**: `AFSKProcessor` (`PAFT`), `FSKProcessor` (`PFSK`), `OOKProcessor` (`POOK`), `BTLETxProcessor` (`PBTT`), `ADSBTXProcessor` (`PADT`), `RDSProcessor` (`PRDS`). All still silent — no `IQSink`.
- **CMake macro `emuhem_strip_proc_main`**: factored the repetitive `file(READ) / string(REPLACE main-block / WRITE)` + `constexpr → const` demotion into one reusable macro, parameterized on `(basename, class_name, has_audio_init, has_blank_line)`. Collapses what would have been ~14× 7-line patch blocks into one 25-line macro + 14 one-liners. Existing Phase 9 blocks were left unchanged to avoid churn.
- **Support file added**: `baseband/stream_input.cpp` (needed by AFSK/APRS/FSK RX). `aprs_packet.cpp` (common/) was already caught by the common glob.
- **`MultiDecimator` collision patched**: `proc_wfm_audio.hpp` (Phase 3.5) and `proc_fsk_rx.hpp` (Phase 10) each define a global-scope `MultiDecimator` class template. Firmware ships each processor as its own binary so there's no conflict there; EmuHem registers both via `core_control_emu.cpp` so both headers land in the same TU. Fixed by renaming the FSK_RX variant in-place to `FskMultiDecimator` (3 hits, all in the header; the .cpp never references it). A similar rename will be needed when `proc_capture` lands.
- **Verified**: clean build; headless smoke tests pass; registered tags: 25 total (4 from Phase 3.5, 7 from Phase 9, 14 from Phase 10). No regressions in `--iq-file` / `--soapy` / plain launch.

### Phase 9: Additional Baseband Processors (2026-04-20)

Seven more baseband processors light up, covering the most visible Mayhem apps. The registry now maps 11 `image_tag_t` values to factories — the 4 from Phase 3/3.5 plus POCSAG2, TPMS, ERT, AIS, Tones, AudioBeep, and SigGen.

- **RX digital decoders (4)**: `POCSAGProcessor` (`PPO2`), `TPMSProcessor` (`PTPM`), `ERTProcessor` (`PERT`), `AISProcessor` (`PAIS`). Fed with I/Q from `--iq-file=capture.cu8` or `--soapy=driver=rtlsdr`, they decode pager frames, tire pressure IDs, smart-meter transmissions, and maritime AIS packets and hand them up to the application layer through the existing `shared_memory.application_queue`.
- **TX processors (3)**: `TonesProcessor` (`PTON`), `AudioBeepProcessor` (`PABP`), `SigGenProcessor` (`PSIG`). They initialize cleanly and the menu launches them, but EmuHem has no `IQSink` yet — generated samples are discarded. No audible output, no crash. Phase 10 will add an egress path.
- **Support files pulled into build**: `channel_decimator.cpp`, `matched_filter.cpp`, `clock_recovery.cpp`, `packet_builder.cpp` (baseband DSP helpers). The `common/` globber already sweeps in `pocsag.cpp`, `pocsag_packet.cpp`, `ais_baseband.cpp`, `ais_packet.cpp`, `ert_packet.cpp`, `tpms_packet.cpp`.
- **CMake patches**: `int main()` stripped from each of the 7 `proc_*.cpp` files; `proc_pocsag2.cpp` also got the `constexpr size_t` → `const size_t` demotion (Clang rejects function-scope constexpr reads of static class members). `phase_detector.hpp` popcountl static_assert switched from `unsigned long` (8 bytes on 64-bit host) to `unsigned int` + `__builtin_popcount`.
- **Timestamp shim**: baseband packet builders call `Timestamp::now()`; firmware defines that only on the M4 branch (where `Timestamp` is its own struct). In EmuHem `Timestamp` aliases `lpc43xx::rtc::RTC` via buffer.hpp's M0 branch, so `RTC::now()` is declared in `lpc43xx_cpp.hpp` and implemented in `rtc_time_emu.cpp` using the host wall clock (reuses the existing `rtc_time::now()` helper).
- **Registry**: `core_control_emu.cpp` now includes 11 processor headers + 11 `push_back` entries. The old "ToDo: register SSTV/AFSK/AIS..." comment is replaced with a longer TODO list pointing at the remaining phases.
- **Verified**: clean build with no new errors beyond warnings already present in Phase 8. Headless smoke test boots/exits cleanly. Regressions (`--iq-file`, `--soapy=driver=nosuchdriver`, plain) all exit cleanly. Live decode with real captures/dongles deferred (no test captures or hardware attached to this machine).

### Phase 8: SoapySDR I/Q Source (2026-04-19)

EmuHem can now stream live I/Q directly from any SoapySDR-supported USB dongle (HackRF, RTL-SDR, Airspy, LimeSDR, PlutoSDR, BladeRF, ...) without launching a separate `rtl_tcp` process.

- **`SoapyIQSource`** (`iq_source.hpp/.cpp`, compiled behind `#ifdef EMUHEM_HAS_SOAPYSDR`): opens the device with a user-supplied Soapy arg string, negotiates a sample format (prefers `SOAPY_SDR_CS8` for direct memcpy, falls back to `CS16` with `>>8` then `CF32` with `clamp(x*127)`), applies initial rate/freq/gain from env defaults, then spawns a receiver thread that drains `readStream` into a mutex-guarded 262 144-sample ring buffer. `read()` drains the ring and zero-pads on under-run so DMA pacing stays steady. Destructor sets stop flag, deactivates + closes the stream, unmakes the device, joins the thread, logs drop count. Upstream `on_*_changed` hooks call `device->setSampleRate` / `setFrequency` / `setGain` with the same dedup-via-atomic pattern as `RtlTcpClientSource`.
- **Factory precedence updated**: `EMUHEM_IQ_FILE` → **`EMUHEM_IQ_SOAPY`** → `EMUHEM_IQ_TCP` → noise. Each failure (invalid args, device busy, no matching format) logs and falls through. Soapy is marked `is_network_tuned = true` so `FrequencyShiftingSource` does NOT wrap it — the dongle centers its own RF.
- **Startup defaults** (before firmware ever tunes): `EMUHEM_IQ_SOAPY_RATE=<hz>` (default 2_400_000), `EMUHEM_IQ_SOAPY_FREQ=<hz>` (default 100_000_000), `EMUHEM_IQ_SOAPY_GAIN=<tenths_db>` (default 200 = 20 dB).
- **CLI flags** (`main_emu.cpp::parse_cli`): `--soapy=ARGS`, `--soapy-rate=HZ`, `--soapy-freq=HZ`, `--soapy-gain=TENTHS_DB`. Help text adds example `--soapy='driver=hackrf' --soapy-rate=8000000 --soapy-freq=915000000`.
- **Optional CMake dep** (`CMakeLists.txt`): `find_package(SoapySDR CONFIG QUIET)` with Homebrew Cellar paths; on success links `SoapySDR` target and defines `EMUHEM_HAS_SOAPYSDR=1`. Without SoapySDR installed the build still succeeds; attempting `--soapy=` logs "SoapySDR support not compiled in" and falls through.
- **Verified**: CMake reports `SoapySDR found: USB SDR sources enabled`; `--help` lists the four new flags; a bogus `--soapy='driver=nosuchdriver'` logs the Soapy error and gracefully falls back to the noise source (no crash); regression runs (plain, `--iq-file=...`, `--headless`) behave identically. Live-device test requires an attached dongle (`SoapySDRUtil --find` currently reports none on this machine).

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

**51 of ~55 image tags resolve.** TX processors emit real I/Q since Phase 12 (`IQSink`).

| Phase | Tag | Class | Type |
|-------|-----|-------|------|
| 3 | `PSPE` | `WidebandSpectrum` | Spectrum |
| 3.5 | `PAMA` | `NarrowbandAMAudio` | RX audio |
| 3.5 | `PNFM` | `NarrowbandFMAudio` | RX audio |
| 3.5 | `PWFM` | `WidebandFMAudio` | RX audio |
| 9 | `PPO2` | `POCSAGProcessor` | RX digital |
| 9 | `PTPM` | `TPMSProcessor` | RX digital |
| 9 | `PERT` | `ERTProcessor` | RX digital |
| 9 | `PAIS` | `AISProcessor` | RX digital |
| 9 | `PTON` | `TonesProcessor` | TX modulator |
| 9 | `PABP` | `AudioBeepProcessor` | TX modulator |
| 9 | `PSIG` | `SigGenProcessor` | TX modulator |
| 10 | `PAFR` | `AFSKRxProcessor` | RX digital |
| 10 | `PAPR` | `APRSRxProcessor` | RX digital |
| 10 | `PBTR` | `BTLERxProcessor` | RX digital |
| 10 | `PNRR` | `NRFRxProcessor` | RX digital |
| 10 | `PFSR` | `FSKRxProcessor` | RX digital |
| 10 | `PWTH` | `WeatherProcessor` | RX digital |
| 10 | `PSGD` | `SubGhzDProcessor` | RX digital |
| 10 | `PPVW` | `ProtoViewProcessor` | RX digital |
| 10 | `PAFT` | `AFSKProcessor` | TX modulator |
| 10 | `PFSK` | `FSKProcessor` | TX modulator |
| 10 | `POOK` | `OOKProcessor` | TX modulator |
| 10 | `PBTT` | `BTLETxProcessor` | TX modulator |
| 10 | `PADT` | `ADSBTXProcessor` | TX modulator |
| 10 | `PRDS` | `RDSProcessor` | TX modulator |
| 11 | `PACA` | `ACARSProcessor` | RX digital |
| 11 | `PADR` | `ADSBRXProcessor` | RX digital |
| 11 | `PFLX` | `FlexProcessor` | RX digital |
| 11 | `PSON` | `SondeProcessor` | RX digital |
| 11 | `PEPI` | `EPIRBProcessor` | RX digital |
| 11 | `PTNE` | `ToneDetectProcessor` | RX digital |
| 11 | `PSCD` | `SubCarProcessor` | RX digital |
| 11 | `PRTR` | `RTTYRxProcessor` | RX digital |
| 11 | `PMRS` | `MorseProcessor` | RX digital |
| 11 | `PMRT` | `MorseTXProcessor` | TX modulator |
| 11 | `PRTT` | `RTTYTXProcessor` | TX modulator |
| 11 | `PJAM` | `JammerProcessor` | TX modulator |
| 11 | `P25T` | `P25TxProcessor` | TX modulator |
| 11 | `PGPS` | `GPSReplayProcessor` | TX modulator |
| 11 | `PSPT` | `SpectrumPainterProcessor` | TX modulator |
| 11 | `PATX` | `AudioTXProcessor` | TX modulator |
| 11 | `PTSK` | `TimeSinkProcessor` | TX modulator |
| 11 | `PEPT` | `EPIRBTXProcessor` | TX modulator |
| 17 | `PCAP` | `CaptureProcessor` | RX record |
| 17 | `PREP` | `ReplayProcessor` | TX replay |
| 17 | `PMTX` | `MicTXProcessor` | TX modulator |
| 18 | `PSRX` | `SSTVRXProcessor` | RX digital (image) |
| 18 | `PSTX` | `SSTVTXProcessor` | TX modulator (image) |
| 18 | `PWFX` | `WeFaxRx` | RX digital (image) |
| 18 | `PNOA` | `NoaaAptRx` | RX digital (image) |
| 18 | `PTST` | `TestProcessor` | RX digital (diagnostic) |

Tallies: 1 spectrum, 3 RX audio, 24 RX digital (incl. WEFAX/NOAA APT/SSTV RX/Test from Phase 18), 20 TX modulators (incl. SSTV TX from Phase 18), `capture` + `replay` + MicTX = **51 registered**. Remaining (~4): AM TV, BintStreamTX, SigFRX, flash_utility, sd_over_usb, OOK stream TX, POCSAG v1.

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
- **CLI**: `--help`, `--headless`, `--duration=SEC`, `--bezel=0|1`, `--iq-file=`, `--iq-loop=`, `--iq-tcp=`, `--iq-center=`, `--soapy=`, `--soapy-rate=`, `--soapy-freq=`, `--soapy-gain=`, `--iq-tx-file=`, `--iq-tx-soapy=`, `--iq-tx-soapy-rate=`, `--iq-tx-soapy-freq=`, `--iq-tx-soapy-gain=`, `--rtl-tcp-server=`, `--sdcard-root=`, `--pmem-file=`, `--keys=...` (U/D/L/R/S/B/F/+/-/.), `--key-step=MS`. Env-equivalent flags set `EMUHEM_*` before loading.
- **Crash handler**: backtrace to stderr on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT, then re-raise for OS-level crash reports.
- **USB SDR via SoapySDR**: `--soapy='driver=hackrf'` (or any Soapy-supported driver — rtlsdr, airspy, lime, plutosdr, bladerf, ...) streams live I/Q into the baseband pipeline. Tuning/gain/rate changes from the firmware are pushed back to the device through the same `on_*_changed` hooks used by `RtlTcpClientSource`. Optional at build time — `brew install soapysdr`; without it, `--soapy` prints a one-line message and falls back.
- **RX digital decoders registered (21 total)**: POCSAG2 (`PPO2`), TPMS (`PTPM`), ERT (`PERT`), AIS (`PAIS`), AFSK RX (`PAFR`), APRS RX (`PAPR`), BTLE RX (`PBTR`), NRF24 RX (`PNRR`), FSK RX (`PFSR`), Weather (`PWTH`), SubGhzD (`PSGD`), Protoview (`PPVW`), ACARS (`PACA`), ADS-B RX (`PADR`), FLEX (`PFLX`), Sonde (`PSON`), EPIRB (`PEPI`), ToneDetect (`PTNE`), SubCar (`PSCD`), RTTY RX (`PRTR`), Morse (`PMRS`). All instantiate through `core_control_emu.cpp`'s registry and decode packets from `--iq-file` / `--soapy` / `--iq-tcp` sources.
- **TX processors registered (18 total, emitting real I/Q since Phase 12)**: Tones (`PTON`), AudioBeep (`PABP`), SigGen (`PSIG`), AFSK (`PAFT`), FSK (`PFSK`), OOK (`POOK`), BTLE TX (`PBTT`), ADS-B TX (`PADT`), RDS (`PRDS`), MorseTX (`PMRT`), RTTY TX (`PRTT`), Jammer (`PJAM`), P25 TX (`P25T`), GPS Sim (`PGPS`), SpectrumPainter (`PSPT`), AudioTX (`PATX`), TimeSink (`PTSK`), EPIRB TX (`PEPT`). Output sinks selected via `--iq-tx-file=` (CS8) or `--iq-tx-soapy=<args>` (HackRF/Pluto/Lime/Blade).
- **TX path (`IQSink`)**: mirror of `IQSource` — `NullIQSink` (default, silent), `FileIQSink` (CS8, byte-identical to the RX file format so round-trips work), `SoapyIQSink` (HackRF-class TX via `writeStream(SOAPY_SDR_TX, 0, ...)`). `baseband_dma_emu.cpp` is direction-aware: TX drains the previous filled slice to the sink on each `wait_for_buffer()`, with a final-flush in `disable()`. Tuning (sample rate, frequency, gain) forwards to both source and sink. Transmitted I/Q is also fanned out to connected rtl_tcp listeners so modulator output is visible in gqrx/SDR++ without extra hardware.
- Clean startup and shutdown with no crashes or thread leaks

## What Doesn't Work Yet

Infrastructure is solid and most named Mayhem apps (spectrum, audio demodulators, 21 RX decoders, 19 TX modulators, plus capture/replay/microphone) now reach baseband. Remaining gaps grouped by rough effort.

### Missing Features Summary

At-a-glance list of everything not yet implemented, roughly by impact:

| # | Gap | Severity | Effort |
|---|-----|----------|--------|
| 1 | **Windows port** — POSIX-only syscalls in rtl_tcp client/server + SD passthrough | High (blocks Windows users) | Medium |
| 2 | **External `.ppma` app loading** — SSTV/WEFAX/NOAA APT baseband is registered but their UI apps live on-SD as plugins | High (weather/imaging ecosystem blocked) | High |
| 3 | **Remaining 6 baseband processors** — AM TV, SigFRX, BintStreamTX, OOK stream TX, flash_utility, sd_over_usb | Low (each niche) | Low–Medium per-proc |
| 4 | **TX loopback sink** — TX→RX round-trip test (generate RDS, decode back, assert bits) | Medium (unlocks regression coverage) | Medium |
| 5 | **Deeper integration tests** — scripted `--keys` flows, per-app framebuffer golden regions, rtl_tcp round-trip tests | Medium | Medium |
| 6 | **USB serial shell** — firmware CLI/debug shell unreachable; `usb_serial_device_to_host.h` is a stub | Medium | Medium |
| 7 | **Crash dump file** — signal handler prints backtrace to stderr but doesn't write a JSON crash artifact | Low | Low |
| 8 | **`radio::enable/disable_rf_output`** — currently no-op; sink is always live when processor runs | Low | Low |
| 9 | **WAV/CS16/CF32 output formats** on `FileIQSink` — only CS8 today | Low | Low |
| 10 | **Peripheral sensors** (battery/temp/I²C/antenna bias) return constants | Low (rarely used) | Low |
| 11 | **CPLD upload** + **debug screen data sources** return zeros | Low | Low |
| 12 | **Audio controls** — volume/mute round-trip to `SDL_AudioStream` gain/pause not verified | Low | Low |
| 13 | **Input fidelity** — mouse wheel = one-tick-per-notch encoder; no acceleration | Low | Low |
| 14 | **TX-shutdown flake** under heavy load — 1 TX app per full serial run occasionally misses its `timeout 60` budget; each passes in isolation | Low (cosmetic) | Uncertain |
| 15 | **Font rendering on bezel** — no labels, clock, or signal-strength indicator | Cosmetic | Medium |
| 16 | **Higher-DPI "H2M" variant** — 240×320 LCD only | Cosmetic | Medium |
| 17 | **GCC/MSVC support** — Clang-only builds tested | Cosmetic | Low |
| 18 | **rtl_tcp-as-TX-client** — non-standard rtl_tcp mode, low priority | Niche | Medium |
| 19 | **Per-direction TX gain hook** — `on_tx_gain_changed` reuses RX `tuner_gain` bridge | Niche | Low |

### Baseband processors — 6 of ~55 still uncompiled

51 processors resolve; see the Registered Baseband Processors table above for the full list. Still absent:

- **RX**: SigFRX, AM TV (header class-name collision with `proc_wfm_audio.hpp::WidebandFMAudio`).
- **TX**: BintStreamTX (no `image_tag_*` constant), OOK stream TX (no baseband `.cpp`).
- **Utility**: `proc_flash_utility`, `proc_sd_over_usb`.
- **Superseded** (won't be added): `proc_pocsag` (v1, replaced by `PPO2`).

Each new processor needs: the `.cpp` patched via `emuhem_strip_proc_main` (see CMakeLists.txt Phase 10 macro), support `.cpp` files added to CMake (baseband/ must be explicit; common/ is globbed), registered in `core_control_emu.cpp`. Watch for: PRALINE-pattern headers, 64-bit `unsigned long` assumptions, `constexpr size_t` at function scope that reads static class members, and global-scope class-name collisions like `MultiDecimator` / `EccContainer` (wrap the duplicate in `#if 0` via CMake patching).

### Transmit path extensions (Phase 12 landed; these are follow-ups)

The core TX path shipped in Phase 12 — direction-aware `baseband::dma`, `IQSink` abstraction, file + SoapySDR sinks, rtl_tcp fanout on TX. Still deferred:

- **TX-to-RX loopback sink** for end-to-end self-tests (generate a waveform, decode it with the matching RX processor, assert the recovered bits).
- **rtl_tcp-as-TX-client** (send I/Q to a remote custom TX server). Not a standard rtl_tcp mode; low priority.
- **`radio::disable_rf_output` / `enable_rf_output` plumbing**: stubs currently no-op; sink is always "live" when the processor runs. Matters once the firmware UI exposes a TX mute.
- **WAV/CS16/CF32 output formats** on `FileIQSink`. CS8 is the only output format today (sufficient for HackRF round-trips).
- **Per-direction TX gain hook**: `on_tx_gain_changed` reuses the same `tuner_gain` bridge as RX; if the firmware ever sets RX and TX gain separately within one session we'll need a second bridge.

### Unfinished shims

- **USB serial shell**: `usb_serial_device_to_host.h` is a stub — firmware's CLI / debug shell is unreachable from the emulator.
- **Peripheral sensors**: battery gauge, temperature logger, I²C devices, antenna bias all return constants or no-op.
- **CPLD upload** + **debug screen data sources** return zeros.
- **Decorative only**: the bezel D-pad dots don't receive clicks; no status LEDs driven from firmware state.
- **Input fidelity**: mouse wheel → encoder is coarse one-tick-per-notch; no acceleration or physical-feel tuning.
- **Audio controls**: volume / mute UI changes reach persistent memory but round-trip to `SDL_AudioStream` gain/pause is not verified.
- **Crash artifacts**: signal handler prints a backtrace to stderr but doesn't write a crash dump file to disk.

### Polish

- No font rendering on the bezel (no labels, no clock, no signal-strength indicator).
- 240×320 LCD only — no higher-DPI "H2M" PortaPack variant.
- Clang-only build; GCC/MSVC untested.
- **Windows port**: rtl_tcp client + server and SD passthrough use POSIX-only headers (`<sys/socket.h>`, `<netdb.h>`, `<fcntl.h>`, `<sys/statvfs.h>`, `<fnmatch.h>`). Need Winsock2 + Win32 filesystem wrappers and a Windows CI build.

---

## Next Steps

Pick the highest-leverage track for the intended use case:

1. **External `.ppma` app loading** (unlocks the image-RX ecosystem): SSTV RX/TX, WEFAX RX, and NOAA APT RX are registered baseband-side (Phase 18) but their UI apps live on-SD as `.ppma` plugins. Firmware already has a loader (`ExternalAppView`); EmuHem needs a parallel loader that resolves the dynamic-link-style symbols. Biggest user-visible unlock for weather/imaging operators.
2. **Windows port** (broadens audience): thin Winsock2 + Win32 FS wrappers behind the existing POSIX call sites (`<sys/socket.h>`, `<netdb.h>`, `<fcntl.h>`, `<sys/statvfs.h>`, `<fnmatch.h>`).
3. **Remaining 6 processors**: AM TV (needs class-name-collision rename with `proc_wfm_audio`), SigFRX, BintStreamTX (needs `image_tag_*` constant), OOK stream TX (no baseband `.cpp`), `flash_utility`, `sd_over_usb` — all niche.
4. **TX loopback sink**: completes the end-to-end TX → decoder test story (generate RDS, decode it back; generate AFSK, decode it back — regression tests for the modulator/demodulator pair).
5. **Deeper integration tests**: extend beyond the smoke-test baseline — scripted `--keys` flows exercising each RX decoder on a known capture, framebuffer diff against per-app golden regions (not full-hash), rtl_tcp client/server round-trip tests. The harness and `--fb-dump` plumbing is in place; what's missing is per-app golden data and scenario scripts.
6. **USB serial shell**: wire `usb_serial_device_to_host.h` to a real PTY or TCP port so the firmware's debug shell becomes reachable from the host.
7. **Crash dump file**: currently the signal handler prints a backtrace to stderr only. Writing a JSON crash artifact (framebuffer + state + backtrace) to `~/.emuhem/crashes/` would be useful for LLM-assisted bug triage.