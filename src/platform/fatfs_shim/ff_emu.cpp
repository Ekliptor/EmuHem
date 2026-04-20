// EmuHem FatFS shim -- POSIX passthrough to ~/.emuhem/sdcard/
//
// Path encoding: firmware's std::filesystem::path uses char16_t (UTF-16),
// and callers cast .c_str() to const TCHAR*. So all paths crossing this
// boundary are UTF-16 null-terminated strings. We translate them to UTF-8
// host paths rooted at $EMUHEM_SDCARD_ROOT (default $HOME/.emuhem/sdcard).
//
// File handle lifetime: firmware owns FIL/DIR structs. We store our own
// POSIX state in side tables keyed by the firmware pointer, guarded by a
// single mutex. After every op we update fp->fptr / fp->obj.objsize / fp->err
// because firmware reads those directly via the f_tell/f_size/f_eof/f_error
// macros.

#include "ff.h"
#include "diskio.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

// ============================================================================
// Root directory
// ============================================================================

const std::string& sdcard_root() {
    static const std::string root = [] {
        if (const char* env = std::getenv("EMUHEM_SDCARD_ROOT"); env && *env) {
            return std::string{env};
        }
        const char* home = std::getenv("HOME");
        if (!home || !*home) home = "/tmp";
        return std::string{home} + "/.emuhem/sdcard";
    }();

    static bool ensured = false;
    if (!ensured) {
        std::error_code ec;
        fs::create_directories(root, ec);
        if (ec) {
            std::fprintf(stderr, "[EmuHem] sdcard_root: create_directories(%s) failed: %s\n",
                         root.c_str(), ec.message().c_str());
        } else {
            std::fprintf(stderr, "[EmuHem] SD card root: %s\n", root.c_str());
        }
        ensured = true;
    }
    return root;
}

// ============================================================================
// UTF-16 <-> UTF-8 conversion (BMP + surrogate pairs)
// ============================================================================

bool g_reported_invalid_utf = false;
void warn_invalid_utf_once(const char* where) {
    if (!g_reported_invalid_utf) {
        g_reported_invalid_utf = true;
        std::fprintf(stderr, "[EmuHem] warning: non-ASCII code point replaced in %s\n", where);
    }
}

std::string utf16_to_utf8(const TCHAR* s) {
    std::string out;
    if (!s) return out;
    const auto* p = reinterpret_cast<const uint16_t*>(s);
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t low = *p;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++p;
            } else {
                warn_invalid_utf_once("utf16_to_utf8");
                cp = '?';
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

void utf8_to_utf16(const std::string& in, TCHAR* out, size_t out_len) {
    auto* dst = reinterpret_cast<uint16_t*>(out);
    size_t written = 0;
    const auto put16 = [&](uint16_t v) {
        if (written + 1 < out_len) dst[written++] = v;
    };
    for (size_t i = 0; i < in.size();) {
        const auto c = static_cast<unsigned char>(in[i]);
        uint32_t cp;
        size_t n;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
            cp = (c & 0x1F) << 6;
            cp |= (static_cast<unsigned char>(in[i + 1]) & 0x3F);
            n = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < in.size()) {
            cp = (c & 0x0F) << 12;
            cp |= (static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(in[i + 2]) & 0x3F);
            n = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < in.size()) {
            cp = (c & 0x07) << 18;
            cp |= (static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12;
            cp |= (static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(in[i + 3]) & 0x3F);
            n = 4;
        } else {
            warn_invalid_utf_once("utf8_to_utf16");
            cp = '?';
            n = 1;
        }
        i += n;
        if (cp < 0x10000) {
            put16(static_cast<uint16_t>(cp));
        } else {
            cp -= 0x10000;
            put16(static_cast<uint16_t>(0xD800 | (cp >> 10)));
            put16(static_cast<uint16_t>(0xDC00 | (cp & 0x3FF)));
        }
    }
    if (out_len > 0) dst[std::min(written, out_len - 1)] = 0;
}

// ============================================================================
// Path translation (UTF-16 path -> host fs::path rooted at sdcard_root)
// ============================================================================

std::optional<fs::path> translate_path(const TCHAR* tpath) {
    if (!tpath) return std::nullopt;
    std::string p = utf16_to_utf8(tpath);

    // Normalize separators.
    for (auto& c : p) if (c == '\\') c = '/';

    // Strip drive prefix like "0:", "SD:", "1:/foo".
    if (p.size() >= 2) {
        const size_t search_end = std::min<size_t>(p.size(), 4);
        const auto colon = p.find(':');
        if (colon != std::string::npos && colon < search_end) {
            p.erase(0, colon + 1);
        }
    }

    // Strip leading '/' so the join produces a path under the SD root.
    while (!p.empty() && p.front() == '/') p.erase(0, 1);

    // Reject ".." traversal.
    const fs::path rel{p};
    for (const auto& part : rel) {
        if (part == "..") return std::nullopt;
    }

    return fs::path{sdcard_root()} / rel;
}

// ============================================================================
// errno -> FRESULT
// ============================================================================

FRESULT errno_to_fresult(int err, const fs::path* host = nullptr) {
    switch (err) {
        case ENOENT:
            if (host) {
                std::error_code ec;
                if (!fs::exists(host->parent_path(), ec)) return FR_NO_PATH;
            }
            return FR_NO_FILE;
        case EACCES:
        case EPERM:
            return FR_DENIED;
        case EEXIST:
            return FR_EXIST;
        case ENOTDIR:
            return FR_NO_PATH;
        case EISDIR:
            return FR_DENIED;
        case ENAMETOOLONG:
            return FR_INVALID_NAME;
        case ENOMEM:
            return FR_NOT_ENOUGH_CORE;
        case EROFS:
            return FR_WRITE_PROTECTED;
        default:
            return FR_DISK_ERR;
    }
}

// ============================================================================
// Handle side tables
// ============================================================================

struct FileHandle {
    int fd{-1};
    std::FILE* fp{nullptr};
    fs::path host_path{};
    BYTE mode{0};
};

struct DirHandle {
    fs::path host_path{};
    fs::directory_iterator it{};
    std::string pattern_utf8{};  // empty => no pattern filter
};

std::mutex g_fs_mutex;
std::unordered_map<FIL*, FileHandle> g_files;
std::unordered_map<DIR*, DirHandle> g_dirs;

FileHandle* lookup_file(FIL* fp) {
    auto it = g_files.find(fp);
    return it == g_files.end() ? nullptr : &it->second;
}

DirHandle* lookup_dir(DIR* dp) {
    auto it = g_dirs.find(dp);
    return it == g_dirs.end() ? nullptr : &it->second;
}

// ============================================================================
// Mode flags
// ============================================================================

int fatfs_mode_to_posix(BYTE mode) {
    int flags = 0;
    const bool want_read = mode & FA_READ;
    const bool want_write = mode & FA_WRITE;

    if (want_read && want_write) {
        flags = O_RDWR;
    } else if (want_write) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    if (mode & FA_CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
    else if (mode & FA_CREATE_NEW) flags |= O_CREAT | O_EXCL;
    else if (mode & FA_OPEN_ALWAYS) flags |= O_CREAT;
    // FA_OPEN_APPEND (0x30) implies FA_OPEN_ALWAYS; seek to end after open.

    return flags;
}

const char* fdopen_mode(BYTE mode) {
    const bool want_read = mode & FA_READ;
    const bool want_write = mode & FA_WRITE;
    if (mode & FA_CREATE_ALWAYS) return want_read ? "w+b" : "wb";
    if (mode & (FA_CREATE_NEW | FA_OPEN_ALWAYS)) return want_read ? "r+b" : "r+b";
    if (want_read && want_write) return "r+b";
    if (want_write) return "r+b";
    return "rb";
}

// ============================================================================
// FILINFO population
// ============================================================================

void fill_filinfo_from_stat(const struct stat& st, const std::string& name_utf8, FILINFO* fno) {
    fno->fsize = st.st_size;
    fno->fattrib = S_ISDIR(st.st_mode) ? AM_DIR : AM_ARC;
    if (!(st.st_mode & S_IWUSR)) fno->fattrib |= AM_RDO;

    struct tm lt {};
#ifdef _WIN32
    time_t t = st.st_mtime;
    localtime_s(&lt, &t);
#else
    ::localtime_r(&st.st_mtime, &lt);
#endif
    fno->fdate = static_cast<WORD>(((lt.tm_year - 80) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);
    fno->ftime = static_cast<WORD>((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));

    utf8_to_utf16(name_utf8, fno->fname, _MAX_LFN + 1);
    fno->altname[0] = 0;
}

bool stat_and_fill(const fs::path& host, const std::string& name_utf8, FILINFO* fno) {
    struct stat st {};
    if (::stat(host.c_str(), &st) != 0) {
        std::memset(fno, 0, sizeof(FILINFO));
        return false;
    }
    fill_filinfo_from_stat(st, name_utf8, fno);
    return true;
}

// Advance a directory iterator to the next matching entry; return nullopt at end.
std::optional<fs::directory_entry> next_entry(DirHandle& dh) {
    while (dh.it != fs::directory_iterator{}) {
        const auto entry = *dh.it;
        std::error_code ec;
        dh.it.increment(ec);  // advance regardless of error so we don't spin
        const std::string name = entry.path().filename().string();
        if (!dh.pattern_utf8.empty()) {
            if (::fnmatch(dh.pattern_utf8.c_str(), name.c_str(), FNM_CASEFOLD) != 0) {
                continue;
            }
        }
        return entry;
    }
    return std::nullopt;
}

}  // anonymous namespace

// ============================================================================
// FatFs API (extern "C")
// ============================================================================

extern "C" {

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode) {
    if (!fp) return FR_INVALID_PARAMETER;
    std::memset(fp, 0, sizeof(FIL));
    sdcard_root();

    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;

    const int flags = fatfs_mode_to_posix(mode);
    const int fd = ::open(host->c_str(), flags, 0644);
    if (fd < 0) return errno_to_fresult(errno, &*host);

    std::FILE* f = ::fdopen(fd, fdopen_mode(mode));
    if (!f) {
        const int e = errno;
        ::close(fd);
        return errno_to_fresult(e, &*host);
    }

    // Capture file size for fp->obj.objsize.
    FSIZE_t size = 0;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        long end = std::ftell(f);
        if (end >= 0) size = static_cast<FSIZE_t>(end);
    }

    FSIZE_t pos = 0;
    if (mode & FA_OPEN_APPEND) {
        pos = size;  // stream already at end
    } else {
        std::fseek(f, 0, SEEK_SET);
    }

    fp->obj.objsize = size;
    fp->fptr = pos;

    std::lock_guard<std::mutex> lk(g_fs_mutex);
    g_files.emplace(fp, FileHandle{fd, f, *host, mode});
    return FR_OK;
}

FRESULT f_close(FIL* fp) {
    if (!fp) return FR_INVALID_OBJECT;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* h = lookup_file(fp);
    if (!h) return FR_INVALID_OBJECT;
    if (h->fp) std::fclose(h->fp);  // fclose closes the underlying fd too
    g_files.erase(fp);
    return FR_OK;
}

FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br) {
    if (br) *br = 0;
    if (!fp || !buff) return FR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* h = lookup_file(fp);
    if (!h || !h->fp) return FR_INVALID_OBJECT;

    const size_t n = std::fread(buff, 1, btr, h->fp);
    if (std::ferror(h->fp)) {
        std::clearerr(h->fp);
        fp->err = FR_DISK_ERR;
        return FR_DISK_ERR;
    }
    if (br) *br = static_cast<UINT>(n);
    fp->fptr += n;
    return FR_OK;
}

FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw) {
    if (bw) *bw = 0;
    if (!fp || !buff) return FR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* h = lookup_file(fp);
    if (!h || !h->fp) return FR_INVALID_OBJECT;

    const size_t n = std::fwrite(buff, 1, btw, h->fp);
    if (std::ferror(h->fp)) {
        std::clearerr(h->fp);
        fp->err = FR_DISK_ERR;
        return FR_DISK_ERR;
    }
    if (bw) *bw = static_cast<UINT>(n);
    fp->fptr += n;
    if (fp->fptr > fp->obj.objsize) fp->obj.objsize = fp->fptr;
    return FR_OK;
}

FRESULT f_lseek(FIL* fp, FSIZE_t ofs) {
    if (!fp) return FR_INVALID_PARAMETER;
    if (ofs == CREATE_LINKMAP) return FR_OK;  // fast-seek table unsupported, not an error

    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* h = lookup_file(fp);
    if (!h || !h->fp) return FR_INVALID_OBJECT;

    // Extend file by writing zeros if seeking past EOF (matches FatFS semantics).
    if (ofs > fp->obj.objsize) {
        if (!(h->mode & FA_WRITE)) {
            // Read-only: clamp to end (matches FatFS behavior).
            ofs = fp->obj.objsize;
        } else {
            std::fseek(h->fp, 0, SEEK_END);
            const FSIZE_t to_fill = ofs - fp->obj.objsize;
            static const char zeros[512] = {};
            FSIZE_t remaining = to_fill;
            while (remaining > 0) {
                const size_t chunk = remaining > sizeof(zeros) ? sizeof(zeros) : static_cast<size_t>(remaining);
                if (std::fwrite(zeros, 1, chunk, h->fp) != chunk) {
                    fp->err = FR_DISK_ERR;
                    return FR_DISK_ERR;
                }
                remaining -= chunk;
            }
            fp->obj.objsize = ofs;
        }
    }

    if (std::fseek(h->fp, static_cast<long>(ofs), SEEK_SET) != 0) {
        fp->err = FR_DISK_ERR;
        return FR_DISK_ERR;
    }
    fp->fptr = ofs;
    return FR_OK;
}

FRESULT f_truncate(FIL* fp) {
    if (!fp) return FR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* h = lookup_file(fp);
    if (!h || !h->fp) return FR_INVALID_OBJECT;
    std::fflush(h->fp);
    if (::ftruncate(h->fd, static_cast<off_t>(fp->fptr)) != 0) {
        fp->err = FR_DISK_ERR;
        return FR_DISK_ERR;
    }
    fp->obj.objsize = fp->fptr;
    return FR_OK;
}

FRESULT f_sync(FIL* fp) {
    if (!fp) return FR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* h = lookup_file(fp);
    if (!h || !h->fp) return FR_INVALID_OBJECT;
    if (std::fflush(h->fp) != 0) return FR_DISK_ERR;
    if (::fsync(h->fd) != 0 && errno != EINVAL /* pipe/socket */) return FR_DISK_ERR;
    return FR_OK;
}

FRESULT f_opendir(DIR* dp, const TCHAR* path) {
    if (!dp) return FR_INVALID_PARAMETER;
    std::memset(dp, 0, sizeof(DIR));
    sdcard_root();

    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;

    std::error_code ec;
    fs::directory_iterator it{*host, ec};
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory) return FR_NO_PATH;
        if (ec == std::errc::not_a_directory) return FR_NO_PATH;
        return FR_DISK_ERR;
    }

    std::lock_guard<std::mutex> lk(g_fs_mutex);
    g_dirs.emplace(dp, DirHandle{*host, std::move(it), {}});
    return FR_OK;
}

FRESULT f_closedir(DIR* dp) {
    if (!dp) return FR_INVALID_OBJECT;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    g_dirs.erase(dp);
    return FR_OK;
}

FRESULT f_readdir(DIR* dp, FILINFO* fno) {
    if (!dp) return FR_INVALID_PARAMETER;
    if (!fno) {
        // f_rewinddir: re-open the directory.
        std::lock_guard<std::mutex> lk(g_fs_mutex);
        auto* dh = lookup_dir(dp);
        if (!dh) return FR_INVALID_OBJECT;
        std::error_code ec;
        dh->it = fs::directory_iterator{dh->host_path, ec};
        return ec ? FR_DISK_ERR : FR_OK;
    }

    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* dh = lookup_dir(dp);
    if (!dh) {
        std::memset(fno, 0, sizeof(FILINFO));
        return FR_INVALID_OBJECT;
    }
    auto entry = next_entry(*dh);
    if (!entry) {
        std::memset(fno, 0, sizeof(FILINFO));
        return FR_OK;  // end of directory
    }
    const std::string name = entry->path().filename().string();
    stat_and_fill(entry->path(), name, fno);
    return FR_OK;
}

FRESULT f_findfirst(DIR* dp, FILINFO* fno, const TCHAR* path, const TCHAR* pattern) {
    if (!dp || !fno) return FR_INVALID_PARAMETER;
    std::memset(fno, 0, sizeof(FILINFO));
    FRESULT r = f_opendir(dp, path);
    if (r != FR_OK) return r;

    {
        std::lock_guard<std::mutex> lk(g_fs_mutex);
        auto* dh = lookup_dir(dp);
        if (dh) dh->pattern_utf8 = utf16_to_utf8(pattern);
    }
    return f_findnext(dp, fno);
}

FRESULT f_findnext(DIR* dp, FILINFO* fno) {
    if (!dp || !fno) return FR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto* dh = lookup_dir(dp);
    if (!dh) {
        std::memset(fno, 0, sizeof(FILINFO));
        return FR_INVALID_OBJECT;
    }
    auto entry = next_entry(*dh);
    if (!entry) {
        std::memset(fno, 0, sizeof(FILINFO));
        return FR_NO_FILE;
    }
    const std::string name = entry->path().filename().string();
    stat_and_fill(entry->path(), name, fno);
    return FR_OK;
}

FRESULT f_mkdir(const TCHAR* path) {
    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;
    if (::mkdir(host->c_str(), 0755) != 0) {
        if (errno == EEXIST) return FR_EXIST;
        return errno_to_fresult(errno, &*host);
    }
    return FR_OK;
}

FRESULT f_unlink(const TCHAR* path) {
    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;
    std::error_code ec;
    if (!fs::remove(*host, ec)) {
        if (ec == std::errc::no_such_file_or_directory) return FR_NO_FILE;
        return FR_DENIED;
    }
    return FR_OK;
}

FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new) {
    auto ho = translate_path(path_old);
    auto hn = translate_path(path_new);
    if (!ho || !hn) return FR_INVALID_NAME;
    if (::rename(ho->c_str(), hn->c_str()) != 0) {
        return errno_to_fresult(errno, &*ho);
    }
    return FR_OK;
}

FRESULT f_stat(const TCHAR* path, FILINFO* fno) {
    if (fno) std::memset(fno, 0, sizeof(FILINFO));
    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;
    struct stat st {};
    if (::stat(host->c_str(), &st) != 0) {
        return errno_to_fresult(errno, &*host);
    }
    if (fno) {
        const std::string name = host->filename().string();
        fill_filinfo_from_stat(st, name, fno);
    }
    return FR_OK;
}

FRESULT f_chmod(const TCHAR* path, BYTE attr, BYTE mask) {
    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;
    if (!(mask & AM_RDO)) return FR_OK;  // only AM_RDO is meaningfully mapped

    struct stat st {};
    if (::stat(host->c_str(), &st) != 0) return errno_to_fresult(errno, &*host);
    mode_t new_mode = st.st_mode;
    if (attr & AM_RDO) new_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else new_mode |= S_IWUSR;
    ::chmod(host->c_str(), new_mode);
    return FR_OK;
}

FRESULT f_utime(const TCHAR* path, const FILINFO* fno) {
    if (!fno) return FR_INVALID_PARAMETER;
    auto host = translate_path(path);
    if (!host) return FR_INVALID_NAME;

    struct tm lt {};
    lt.tm_year = ((fno->fdate >> 9) & 0x7F) + 80;
    lt.tm_mon = ((fno->fdate >> 5) & 0x0F) - 1;
    lt.tm_mday = fno->fdate & 0x1F;
    lt.tm_hour = (fno->ftime >> 11) & 0x1F;
    lt.tm_min = (fno->ftime >> 5) & 0x3F;
    lt.tm_sec = (fno->ftime & 0x1F) * 2;
    lt.tm_isdst = -1;
    time_t t = std::mktime(&lt);
    if (t == (time_t)-1) return FR_OK;  // best-effort

    struct timespec times[2];
    times[0].tv_sec = t; times[0].tv_nsec = 0;
    times[1].tv_sec = t; times[1].tv_nsec = 0;
    ::utimensat(AT_FDCWD, host->c_str(), times, 0);
    return FR_OK;
}

FRESULT f_chdir(const TCHAR*) { return FR_OK; }
FRESULT f_chdrive(const TCHAR*) { return FR_OK; }

FRESULT f_getcwd(TCHAR* buff, UINT len) {
    if (buff && len > 0) buff[0] = 0;
    return FR_OK;
}

FRESULT f_getfree(const TCHAR*, DWORD* nclst, FATFS** fatfs) {
    // Firmware `std::filesystem::space()` dereferences the returned FATFS* to
    // compute cluster_bytes = fs->csize * _MIN_SS and total_bytes via
    // fs->n_fatent. With a nullptr return that crashes. Back the pointer with
    // a process-wide static so the fields are live. csize=1 means "1 sector
    // per cluster" so *nclst below can be reported in 512-byte units without
    // scaling.
    static FATFS g_emu_fatfs{};
    static bool initialized = false;
    if (!initialized) {
        std::memset(&g_emu_fatfs, 0, sizeof(g_emu_fatfs));
        g_emu_fatfs.csize = 1;  // one sector per cluster; _MIN_SS = 512
        initialized = true;
    }

    sdcard_root();
    if (fatfs) *fatfs = &g_emu_fatfs;

    struct statvfs vfs {};
    if (::statvfs(sdcard_root().c_str(), &vfs) != 0) {
        if (nclst) *nclst = 0;
        g_emu_fatfs.n_fatent = 2;  // forces "total bytes = 0" via (n_fatent - 2)
        return FR_DISK_ERR;
    }

    // Report free/total sizes. Firmware uses fs->csize (=1) * 512 as
    // cluster_bytes, fs->n_fatent - 2 as total cluster count, and *nclst as
    // free cluster count.
    const uint64_t sector_bytes = 512;
    const uint64_t total_bytes = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
    const uint64_t free_bytes  = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
    const uint64_t total_clusters = std::min<uint64_t>(total_bytes / sector_bytes, 0xFFFFFFFFu - 2);
    const uint64_t free_clusters  = std::min<uint64_t>(free_bytes  / sector_bytes, 0xFFFFFFFFu);
    if (nclst) *nclst = static_cast<DWORD>(free_clusters);
    g_emu_fatfs.n_fatent = static_cast<DWORD>(total_clusters + 2);
    return FR_OK;
}

FRESULT f_getlabel(const TCHAR*, TCHAR* label, DWORD* vsn) {
    if (label) label[0] = 0;
    if (vsn) *vsn = 0;
    return FR_OK;
}

FRESULT f_setlabel(const TCHAR*) { return FR_OK; }

FRESULT f_forward(FIL*, UINT (*)(const BYTE*, UINT), UINT, UINT* bf) {
    if (bf) *bf = 0;
    return FR_OK;
}

FRESULT f_expand(FIL*, FSIZE_t, BYTE) { return FR_OK; }

FRESULT f_mount(FATFS* fs, const TCHAR*, BYTE) {
    if (fs) std::memset(fs, 0, sizeof(FATFS));
    sdcard_root();
    return FR_OK;
}

FRESULT f_mkfs(const TCHAR*, BYTE, DWORD, void*, UINT) { return FR_OK; }
FRESULT f_fdisk(BYTE, const DWORD*, void*) { return FR_OK; }

// ----- String I/O -----

int f_putc(TCHAR c, FIL* fp) {
    TCHAR buf[2] = {c, 0};
    return f_puts(buf, fp);
}

int f_puts(const TCHAR* str, FIL* fp) {
    if (!str || !fp) return EOF;
    const std::string utf8 = utf16_to_utf8(str);
    UINT written = 0;
    if (f_write(fp, utf8.data(), static_cast<UINT>(utf8.size()), &written) != FR_OK) return EOF;
    return static_cast<int>(written);
}

int f_printf(FIL* fp, const TCHAR* fmt, ...) {
    // Minimal: format ASCII portion only. Firmware rarely uses this path.
    if (!fp || !fmt) return 0;
    const std::string narrow_fmt = utf16_to_utf8(fmt);
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), narrow_fmt.c_str(), ap);
    va_end(ap);
    if (n <= 0) return n;
    UINT written = 0;
    f_write(fp, buf, static_cast<UINT>(n), &written);
    return static_cast<int>(written);
}

TCHAR* f_gets(TCHAR* buff, int len, FIL* fp) {
    if (!buff || len <= 0 || !fp) return nullptr;
    std::string line;
    while (static_cast<int>(line.size()) < len - 1) {
        char c;
        UINT got = 0;
        if (f_read(fp, &c, 1, &got) != FR_OK || got == 0) break;
        line.push_back(c);
        if (c == '\n') break;
    }
    if (line.empty()) {
        buff[0] = 0;
        return nullptr;
    }
    utf8_to_utf16(line, buff, static_cast<size_t>(len));
    return buff;
}

// RTC for FatFS timestamps (used only by file writing paths if they call it).
DWORD get_fattime(void) {
    std::time_t t = std::time(nullptr);
    struct std::tm* tm = std::localtime(&t);
    return ((DWORD)(tm->tm_year - 80) << 25)
         | ((DWORD)(tm->tm_mon + 1) << 21)
         | ((DWORD)tm->tm_mday << 16)
         | ((DWORD)tm->tm_hour << 11)
         | ((DWORD)tm->tm_min << 5)
         | ((DWORD)(tm->tm_sec / 2));
}

// Unicode support
WCHAR ff_convert(WCHAR chr, UINT) { return chr; }
WCHAR ff_wtoupper(WCHAR chr) {
    if (chr >= L'a' && chr <= L'z') return chr - 0x20;
    return chr;
}

void* ff_memalloc(UINT msize) { return std::malloc(msize); }
void ff_memfree(void* mblock) { std::free(mblock); }

// Sync functions -- use ChibiOS Semaphore shim
int ff_cre_syncobj(BYTE, _SYNC_t* sobj) {
    if (sobj) {
        *sobj = static_cast<Semaphore*>(std::malloc(sizeof(Semaphore)));
        if (*sobj) {
            chSemInit(*sobj, 1);
            return 1;
        }
    }
    return 0;
}

int ff_req_grant(_SYNC_t sobj) {
    if (sobj) {
        chSemWait(sobj);
        return 1;
    }
    return 0;
}

void ff_rel_grant(_SYNC_t sobj) {
    if (sobj) chSemSignal(sobj);
}

int ff_del_syncobj(_SYNC_t sobj) {
    if (sobj) std::free(sobj);
    return 1;
}

// Disk I/O -- stays stubbed; firmware doesn't hit this layer when FatFS is replaced.
DSTATUS disk_initialize(BYTE) { return 0; }
DSTATUS disk_status(BYTE) { return 0; }
DRESULT disk_read(BYTE, BYTE*, DWORD, UINT) { return RES_OK; }
DRESULT disk_write(BYTE, const BYTE*, DWORD, UINT) { return RES_OK; }
DRESULT disk_ioctl(BYTE, BYTE, void*) { return RES_OK; }

}  // extern "C"
