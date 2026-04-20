# Phase 13 — CTest Suite (Port `mayhem-firmware/firmware/test/`)

## Context

EmuHem has zero unit tests today. Verification is manual (`--headless`, regression env vars). Upstream firmware ships a doctest suite at `mayhem-firmware/firmware/test/` with 11 tests covering utility classes, file readers, freqman DB, and DSP FFT — exactly the kind of pure-logic coverage that benefits from being run natively in EmuHem's build.

**Goal:** wire those tests into EmuHem's CMake via CTest so `ctest --test-dir build` runs them, catches regressions in the firmware TUs EmuHem compiles (`file.cpp`, `freqman_db.cpp`, `string_format.cpp`, `utility.cpp`, `dsp_fft.cpp`, …), and gives Phase 14+ work a safety net.

## Design

### 1. Test framework: doctest (reuse, don't fetch gtest)

The firmware test suite uses **doctest** (`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `TEST_CASE` / `CHECK` / `REQUIRE`). The single-header `doctest.h` already lives at `mayhem-firmware/firmware/test/include/doctest.h`.

No `FetchContent(googletest)`, no gtest-to-doctest rewrite. EmuHem's CMake just adds that include dir to the test targets. Saves ~all of the porting effort vs. rewriting.

### 2. Directory layout

New at repo root:

```
tests/
  CMakeLists.txt        # Test build logic + add_test() wiring
  linker_stubs.cpp      # Port from firmware/test/application/linker_stubs.cpp,
                        # adjusted for EmuHem's already-compiled shims
  README.md             # How to run; what's covered vs. skipped
```

**Test source files are NOT copied.** `add_executable` references them directly at `${CMAKE_SOURCE_DIR}/mayhem-firmware/firmware/test/application/*.cpp` — matches EmuHem's existing pattern of consuming firmware source in place. If upstream edits a test, we pick it up on next rebuild.

### 3. CMake wiring

In the top-level `CMakeLists.txt`:

```cmake
enable_testing()
add_subdirectory(tests)
```

In `tests/CMakeLists.txt`, one `add_executable` per upstream test binary (mirrors upstream's split):

- `emuhem_tests_application` — links 10 `test_*.cpp` from `firmware/test/application/` + `main.cpp` + the firmware TUs they need (`file.cpp`, `file_reader.cpp`, `freqman_db.cpp`, `string_format.cpp`, `utility.cpp`, `tone_key.cpp`) + `linker_stubs.cpp`.
- `emuhem_tests_baseband` — links `main.cpp` + `dsp_fft_test.cpp` + `${FW_DIR}/common/dsp_fft.cpp`.

Each target:

- Adds `mayhem-firmware/firmware/test/include/` to its include path (for `doctest.h`).
- Does **not** link SDL3, ChibiOS shim, or `main_emu.cpp`. Tests are pure-logic — no runtime needed.
- Uses the same CMake `string(REPLACE)` patching helpers if any upstream test file needs an EmuHem-specific tweak (none anticipated).

CTest registration:

```cmake
add_test(NAME emuhem_tests_application COMMAND emuhem_tests_application)
add_test(NAME emuhem_tests_baseband   COMMAND emuhem_tests_baseband)
```

Run with: `ctest --test-dir build --output-on-failure`.

### 4. `linker_stubs.cpp` — FatFS symbol satisfaction

Upstream's `firmware/test/application/linker_stubs.cpp` stubs FatFS calls so test binaries don't need the SD card. EmuHem has a real POSIX-backed FatFS shim (`ff_emu.cpp`) but linking it pulls in thread/filesystem machinery the tests don't need. Cleaner to port the stubs.

Port verbatim, trim any already-satisfied symbols (EmuHem compiles `event_m0.cpp`, `irq_controls.cpp`, etc.). If the test target still under-links, we fall back to adding EmuHem's `ff_emu.cpp` — simple escalation path, not a design decision up front.

### 5. Test applicability triage

Per exploration, **all 11 tests look portable.** EmuHem already has every firmware dependency they need:

| Test | Portable? | Risk |
|------|-----------|------|
| `test_basics.cpp` | yes | None (framework sanity) |
| `test_circular_buffer.cpp` | yes | None (header-only) |
| `test_convert.cpp` | yes | None (header-only) |
| `test_optional.cpp` | yes | None (header-only) |
| `test_string_format.cpp` | yes | None (TU compiled) |
| `test_utility.cpp` | yes | None (TU compiled) |
| `test_mock_file.cpp` | yes | None (in-memory) |
| `test_file_reader.cpp` | yes | Uses MockFile (in-memory), not FatFS |
| `test_file_wrapper.cpp` | yes | Uses MockFile (in-memory), not FatFS |
| `test_freqman_db.cpp` | risk | Parses inline strings; may transitively hit FatFS symbols — linker_stubs covers |
| `dsp_fft_test.cpp` | risk | `dsp_fft.cpp` is currently filtered out in EmuHem's build (baseband-only); the test target explicitly adds it |

**Skips anticipated: 0.** But the plan commits to *triage during execution* rather than claiming up-front that nothing will skip. Any test that fails to compile/link/pass lands in the status doc with the specific feature it needs.

### 6. Status documentation

New file `docs/tests_status.md` with:

- **One row per test** showing status: `passing` / `skipped` / `failing`.
- For `skipped` rows: the specific EmuHem feature that would be needed (e.g. "SGPIO DMA completion signaling", "USB serial I/O"), linked to the relevant `docs/implementation_status.md` section.
- Totals row (passing / skipped / failing / total).
- How to run locally (`ctest --test-dir build`) and in CI.
- Expected update frequency: whenever a new test is ported OR a skipped test becomes runnable due to a new emulator feature.

Also bump the "What Works" and "Completed Phases" sections in `docs/implementation_status.md` with a Phase 13 entry pointing at `tests_status.md` for the details.

### 7. Memory update

- `memory/project_emuhem_status.md`: Phase 13 block — CTest landed, N passing / N skipped tests, rule: "Before adding a new feature to EmuHem, check `docs/tests_status.md` — a skipped test may already name the exact feature needed, saving design time."
- `memory/MEMORY.md`: one-liner update.

## Files to Modify / Add

| File | Change |
|---|---|
| `CMakeLists.txt` (top-level) | `enable_testing()` + `add_subdirectory(tests)` |
| `tests/CMakeLists.txt` | **NEW** — two test targets + `add_test()` per test binary |
| `tests/linker_stubs.cpp` | **NEW** — port upstream's stubs, trimmed |
| `tests/README.md` | **NEW** — how to run, coverage summary |
| `docs/tests_status.md` | **NEW** — per-test status + missing-feature list |
| `docs/implementation_status.md` | Add Phase 13 section; update "Next Steps" item #4 (test harness) as landed |
| `memory/project_emuhem_status.md` | Phase 13 block |
| `memory/MEMORY.md` | One-line update |

No changes to `src/platform/*` (tests link against existing shims and firmware TUs unchanged).

## Out of Scope

- **gtest / Catch2 migration** — doctest works, no value in rewriting.
- **EmuHem integration tests** (scripted `--keys` + framebuffer-hash) — remains a future item under Next Steps in `implementation_status.md`.
- **CI wiring** (GitHub Actions, etc.) — surface area unknown; plan covers local-run only.
- **Windows test support** — follows the general Windows-port track in `implementation_status.md`; tests are POSIX-only until that lands.
- **Coverage reports** — `--coverage` flag + lcov not set up.

## Verification

1. **Clean build**: `cmake -S . -B build && cmake --build build -j8 --target emuhem_tests_application emuhem_tests_baseband`. No new warnings.
2. **Run tests**: `ctest --test-dir build --output-on-failure`. Report: N passing / M skipped / 0 failing.
3. **Existing regression**: `cmake --build build -j8` still builds `emuhem` cleanly; `./build/emuhem --headless --duration=1` exits 0.
4. **Test-only smoke**: each test binary runs standalone (`./build/tests/emuhem_tests_application`, `./build/tests/emuhem_tests_baseband`) and reports doctest summaries.
5. **Individual test isolation**: `./build/tests/emuhem_tests_application -tc=test_basics.cpp` filters to one file (doctest feature).
6. **Docs cross-check**: every `skipped` row in `tests_status.md` names a specific missing feature, not a generic "needs work".