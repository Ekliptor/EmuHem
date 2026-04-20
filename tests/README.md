# EmuHem Unit Tests

Ports the upstream firmware doctest suite at `mayhem-firmware/firmware/test/` to run natively against the firmware TUs EmuHem compiles. See `docs/tests_status.md` for per-test status and a running list of emulator features that would unblock any currently-skipped tests.

## Running

```sh
cmake -S . -B build
cmake --build build -j8 --target emuhem_tests_application emuhem_tests_baseband
ctest --test-dir build --output-on-failure
```

Or run a single suite directly:

```sh
./build/tests/emuhem_tests_application
./build/tests/emuhem_tests_baseband
```

doctest supports per-test-case filtering:

```sh
./build/tests/emuhem_tests_application -tc="*circular*"
./build/tests/emuhem_tests_application --list-test-cases
```

## What's here

- `CMakeLists.txt` — test build + CTest registration (two binaries: application, baseband).
- `linker_stubs.cpp` — FatFS symbol stubs ported from `firmware/test/application/linker_stubs.cpp`. Tests use in-memory `MockFile` so real SD is never needed; stubs satisfy link-time references only.

## What's NOT here

- **Test `.cpp` files** — referenced in place from `mayhem-firmware/firmware/test/` (single source of truth; upstream edits flow through on rebuild).
- **doctest.h** — single-header framework at `mayhem-firmware/firmware/test/include/doctest.h`, pulled in via `target_include_directories`.
- **SDL / ChibiOS / main_emu.cpp** — tests are pure-logic and do not link against the emulator runtime.

## Coverage model

The suite targets firmware *utility* classes (string formatting, file readers, freqman DB, DSP FFT) — not the emulator itself. It catches regressions in the firmware TUs EmuHem compiles. Emulator-specific integration tests (scripted `--keys`, framebuffer-hash assertions) are a future item tracked in `docs/implementation_status.md`.
