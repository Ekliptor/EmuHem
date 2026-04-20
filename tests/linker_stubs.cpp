// Linker stubs for EmuHem unit tests.
//
// Ported from mayhem-firmware/firmware/test/application/linker_stubs.cpp.
// The test binaries link against firmware TUs (file.cpp, freqman_db.cpp,
// file_reader.cpp, etc.) that reference FatFS symbols. We don't want to drag
// in EmuHem's full POSIX-backed FatFS shim (ff_emu.cpp) for pure-logic tests,
// so satisfy the symbols with trivial stubs instead. Every test that needs
// real file I/O uses the in-memory MockFile utility; these stubs are only
// needed to resolve references from code paths the tests don't execute.

#include <filesystem>
#include <string>

#include "ff.h"

FRESULT f_close(FIL*) {
    return FR_OK;
}
FRESULT f_closedir(DIR*) {
    return FR_OK;
}
FRESULT f_findfirst(DIR*, FILINFO*, const TCHAR*, const TCHAR*) {
    return FR_OK;
}
FRESULT f_findnext(DIR*, FILINFO*) {
    return FR_OK;
}
FRESULT f_getfree(const TCHAR*, DWORD*, FATFS**) {
    return FR_OK;
}
FRESULT f_lseek(FIL*, FSIZE_t) {
    return FR_OK;
}
FRESULT f_mkdir(const TCHAR*) {
    return FR_OK;
}
FRESULT f_open(FIL*, const TCHAR*, BYTE) {
    return FR_OK;
}
FRESULT f_read(FIL*, void*, UINT, UINT*) {
    return FR_OK;
}
FRESULT f_rename(const TCHAR*, const TCHAR*) {
    return FR_OK;
}
FRESULT f_stat(const TCHAR*, FILINFO*) {
    return FR_OK;
}
FRESULT f_sync(FIL*) {
    return FR_OK;
}
FRESULT f_truncate(FIL*) {
    return FR_OK;
}
FRESULT f_unlink(const TCHAR*) {
    return FR_OK;
}
FRESULT f_write(FIL*, const void*, UINT, UINT*) {
    return FR_OK;
}
// Added beyond upstream's stubs — EmuHem's `file.cpp` calls f_utime to sync
// mtimes, which the upstream test stubs did not need because upstream's older
// file.cpp didn't use it.
FRESULT f_utime(const TCHAR*, const FILINFO*) {
    return FR_OK;
}

// `file_path.cpp` defines the directory-name globals firmware code references
// (freqman_dir, etc.). The upstream tests didn't need to link file_path.cpp
// because their freqman_db code path never touched the globals. EmuHem's test
// build does — satisfy the symbol here rather than dragging file_path.cpp in
// (it transitively pulls UI headers).
// Must be `extern const` to get external linkage — plain `const` at namespace
// scope defaults to internal linkage in C++ and produces an `L`-prefixed
// local symbol the test link can't see.
extern const std::filesystem::path freqman_dir;
const std::filesystem::path freqman_dir = u"FREQMAN";

// Debug log — firmware's __debug_log is routed through a shim in EmuHem, but
// the test targets don't link that shim. Satisfy the symbol here.
void __debug_log(const std::string&) {}
