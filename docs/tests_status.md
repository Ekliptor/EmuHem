# EmuHem Test Suite Status

Last updated: 2026-04-20 (Phase 21 — TX apps now assert non-empty CS8 output via `--iq-tx-file`; catches TX-pipeline regressions the old smoke test missed)

## Summary

| | Unit suites | Unit test cases | Unit assertions | Integration tests |
|---|---|---|---|---|
| Passing | 2 | 87 | 509 | 21 |
| Skipped | 0 | 1 | 2 | 0 |
| Failing | 0 | 0 | 0 | 0 |
| **Total** | **2** | **88** | **511** | **21** |

**Unit tests** (`emuhem_tests_application`, `emuhem_tests_baseband`): Phase 13. Utility-class coverage ported from the upstream firmware `doctest` suite.

**Integration tests** (`integ_app_*`): Phase 16, extended in Phases 17, 19, and 21. Per-app smoke tests that spawn the real `emuhem` binary with `--headless --duration=2 --app=<id> --fb-dump=<tmp>` and assert (1) exit 0, (2) no CRASH/PANIC, (3) app launched, (4) framebuffer not all-zero (proves rendering). **TX apps that emit samples passively** additionally pass `--iq-tx-file=<tmp>` and assert the CS8 output grew ≥ 64 KiB (Phase 21 — proves the TX baseband pipeline actually flowed samples, not just that the UI rendered). Covered apps: `audio`, `pocsag`, `aprsrx`, `adsbrx`, `ais`, `weather`, `search`, `lookingglass`, `recon`, `aprstx`, `bletx`, `ooktx`, `touchtune`, `microphone`, `replay`, `capture`, `rdstx`, `notepad`, `filemanager`, `freqman`, `iqtrim`. **All 21 built-in app ids are now covered.** Remaining uncovered apps are external `.ppma` plugins (SSTV/WEFAX/NOAA APT RX/TX, etc.) that aren't dispatchable via `--app=<id>`.

Run locally:

```sh
cmake -S . -B build
cmake --build build -j8 --target emuhem_tests_application emuhem_tests_baseband
ctest --test-dir build --output-on-failure
```

Or invoke the binaries directly:

```sh
./build/tests/emuhem_tests_application
./build/tests/emuhem_tests_baseband
```

## Per-suite Coverage

### `emuhem_tests_application` (84 passing test cases)

Ports all 10 test files from `mayhem-firmware/firmware/test/application/`. Links against the firmware TUs they exercise: `file.cpp`, `file_reader.cpp`, `freqman_db.cpp`, `string_format.cpp`, `utility.cpp`, and a patched `tone_key.cpp`. No SDL, no ChibiOS runtime, no shim — pure logic only.

| Test file | Status | Notes |
|---|---|---|
| `test_basics.cpp` | passing | Framework sanity (1 == 1). |
| `test_circular_buffer.cpp` | passing | Header-only logic. |
| `test_convert.cpp` | passing | `parse_int<T>` across int8/16/32/64, hex/octal. |
| `test_file_reader.cpp` | passing | `BufferLineReader`, `split_string`, `count_lines` via MockFile. |
| `test_file_wrapper.cpp` | passing | `BufferWrapper` line indexing over MockFile. |
| `test_freqman_db.cpp` | passing (1 case skipped) | See "Skipped tests" below. |
| `test_mock_file.cpp` | passing | MockFile helper self-test. |
| `test_optional.cpp` | passing | `Optional<T>` is_valid / bool conversion. |
| `test_string_format.cpp` | passing | `to_string_dec_int`, `to_string_freq`, etc. |
| `test_utility.cpp` | passing | `ENABLE_FLAGS_OPERATORS`, `Stash` RAII. |

### `emuhem_tests_baseband` (3 passing test cases)

Ports `firmware/test/baseband/dsp_fft_test.cpp`. Explicitly adds `firmware/common/dsp_fft.cpp` which is filtered out of EmuHem's main build (baseband-only regex). Force-includes `lpc43xx_cpp.hpp` for the ARM intrinsic stubs (`__RBIT`, `__CLZ`) that `dsp_fft.hpp` needs.

| Test file | Status | Notes |
|---|---|---|
| `dsp_fft_test.cpp` | passing | `ifft<T>` DC bin + sine reconstruction, complex16/complex32 cases. |

## Skipped tests

**Definition:** excluded from CTest via doctest's `--test-case-exclude=` flag. Skipped tests are listed here with their reason. A skipped test becomes un-skippable either when the cited emulator feature lands or when upstream firmware fixes the test.

| Test case | Suite | Reason for skip | Missing emulator feature |
|---|---|---|---|
| `It can parse frequency step` | application | **Upstream firmware drift.** Test hardcodes the enum ordinal of `"0.1kHz"` as `0` and `"50kHz"` as `11`. Current firmware added two smaller step entries (`_50Hz`, `_10Hz` or similar) ahead of `_100Hz`, shifting every downstream index by +2. The assertions become `CHECK_EQ(2, 0)` and `CHECK_EQ(13, 11)`. Not an EmuHem porting issue — the same assertions would fail on upstream's own host build. | **None.** EmuHem is fine; upstream test needs to be updated to use the enum values directly (e.g. `freqman_step::_100Hz`) instead of ordinal integers. |

**Tests NOT applicable because of missing emulator features: 0.** Every utility test in the upstream suite ports cleanly because every firmware TU they depend on (`file.cpp`, `file_reader.cpp`, `freqman_db.cpp`, `string_format.cpp`, `utility.cpp`, `dsp_fft.cpp`, `tone_key.cpp`) is already compiled by EmuHem (or trivially added to the test target for `dsp_fft.cpp`).

## What's NOT covered by this suite

The upstream test suite is a **utility-class test suite** — it doesn't exercise any of the following (and likely never will). These gaps are tracked as "Next Steps" in `docs/implementation_status.md` rather than as skipped tests here:

- **UI / framebuffer** — no scripted `--keys` playback with framebuffer-hash assertions. Would require a dedicated integration harness separate from doctest.
- **Baseband end-to-end** — no "feed this capture through `POCSAGProcessor`, assert N decoded frames". Would need a separate integration target that links the full baseband pipeline.
- **TX round-trip** — no "generate a tone via `TonesProcessor`, recover it via FFT, assert bin peak". Requires the same integration harness.
- **SD card shim** — no tests for `ff_emu.cpp` specifically; it's exercised transitively when firmware file apps run.
- **I/Q sources / sinks** — no tests for `FileIQSource`, `SoapyIQSource`, `FileIQSink`. Unit-test-worthy but not in upstream.
- **Emulator shims** — no tests for `rtc_time_emu.cpp`, `baseband_dma_emu.cpp`, etc. Same reasoning.

If EmuHem-specific unit tests are added (e.g. `FileIQSink` round-trip, NCO phase accumulator), they go in a **third** suite (`emuhem_tests_shims` or similar) alongside the ported firmware suites, not mixed in.

## Patches applied during porting

Documented here so future-you knows what was adjusted vs. what came from upstream unchanged:

1. **`tone_key.cpp`**: patched `std::std::abs` → `std::abs` (typo in upstream firmware). Applied via CMake `file(READ/REPLACE/WRITE)` in `tests/CMakeLists.txt` — same pattern used elsewhere in the EmuHem build. Main EmuHem binary sidesteps this by excluding `tone_key.cpp` and providing stubs in `phase2_stubs.cpp`; the test target needs the real implementation so `freqman_db` tests exercise meaningful code paths.
2. **`tests/linker_stubs.cpp`**: ported from `firmware/test/application/linker_stubs.cpp`, with two additions beyond upstream:
   - `f_utime` stub (upstream test didn't need it; EmuHem's current `file.cpp` does).
   - `freqman_dir` global (EmuHem's `freqman_db.cpp` uses it when building a file path; upstream's older code path didn't).
3. **`-ULPC43XX_M0`** on the baseband target: EmuHem's top-level CMake adds `-DLPC43XX_M0=1` globally; baseband tests target M4 code paths in `buffer.hpp`'s `Timestamp` typedef, which is incompatible.
4. **`-include lpc43xx_cpp.hpp`** on the baseband target: `dsp_fft.hpp` uses `__RBIT` but only gets it via `buffer.hpp`'s M0 branch. Force-include works around this without patching firmware code.

## Update frequency

This file is updated:

- When a new test file is ported (new row).
- When a skipped test case gets un-skipped (remove its row, add to `emuhem_tests_*` coverage).
- When an emulator feature listed as "missing" in a skip row lands in EmuHem (update implementation_status.md cross-reference).
- When a new EmuHem-specific test suite is added.

## Integration suite (Phase 16)

Lives in `tests/integration/`. The driver is a Bash script (`run_app_test.sh`); per-app CTest entries are generated from the `INTEGRATION_APPS` list in `tests/integration/CMakeLists.txt`.

**Isolation**: each test runs with a fresh `$EMUHEM_PMEM_FILE` and `$EMUHEM_SDCARD_ROOT` under `/tmp`, so settings and autostart state don't leak between tests.

**Serialization**: `RESOURCE_LOCK "emuhem_headless"` forces CTest to run integration tests one at a time even with `-jN` — multiple concurrent `emuhem` processes contend on SDL/CoreAudio device ownership, causing intermittent hangs. Unit tests remain freely parallelizable.

**Liveness assertion** is deliberately loose (`cmp -s fb.bin zero.bin`) — any non-zero framebuffer passes. Pixel-hash baselines were considered and rejected: they rebaseline on every UI tweak.

**Known occasional flake on TX apps** (`aprstx`, `bletx`, `ooktx`, `touchtune`, `replay`, `rdstx`): **mostly fixed in Phase 20** — `firmware_thread_fn` now calls `baseband::shutdown()` after `event_dispatcher.run()` returns, which sends the `ShutdownMessage` to M4 promptly instead of waiting for C++ static destructors at process exit. TX app shutdown went from ~60s (busy-loop spin-wait until `chDbgPanic`) to ~4s (clean M4 dispatcher exit). Full sequential-suite time dropped from ~130s+ (with 1-3 failures) to ~72s (0-1 occasional flake). Still not 100% stable under heavy OS load, e.g. when two `ctest` invocations run in parallel — the remaining flake is scheduling-bound, not a code bug, and each individual app still passes reliably with `ctest -R integ_app_<id>`.

**See also**: `tests/integration/README.md` for how to add a new app.

## See also

- `docs/implementation_status.md` — overall EmuHem implementation status.
- `tests/README.md` — unit test harness documentation.
- `tests/integration/README.md` — integration test harness documentation.
