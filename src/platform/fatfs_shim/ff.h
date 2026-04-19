// EmuHem FatFS shim -- type-compatible replacement for ff.h R0.12c
// Struct layouts match the real FatFS exactly (conditioned on ffconf.h settings).

#ifndef _FATFS
#define _FATFS  68300

#include "integer.h"

// ffconf.h includes ch.h (for _SYNC_t = Semaphore*) which has C++ templates,
// so it must be included OUTSIDE the extern "C" block.
#include "ffconf.h"

#if _FATFS != _FFCONF
#error Wrong configuration file (ffconf.h).
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Type of path name strings
#if _LFN_UNICODE
#ifndef _INC_TCHAR
typedef WCHAR TCHAR;
#define _T(x) L ## x
#define _TEXT(x) L ## x
#endif
#else
#ifndef _INC_TCHAR
typedef char TCHAR;
#define _T(x) x
#define _TEXT(x) x
#endif
#endif

// Type of file size variables
#if _FS_EXFAT
typedef QWORD FSIZE_t;
#else
typedef DWORD FSIZE_t;
#endif

// File system object structure (FATFS)
typedef struct {
    BYTE    fs_type;
    BYTE    drv;
    BYTE    n_fats;
    BYTE    wflag;
    BYTE    fsi_flag;
    WORD    id;
    WORD    n_rootdir;
    WORD    csize;
#if _MAX_SS != _MIN_SS
    WORD    ssize;
#endif
#if _USE_LFN != 0
    WCHAR*  lfnbuf;
#endif
#if _FS_EXFAT
    BYTE*   dirbuf;
#endif
#if _FS_REENTRANT
    _SYNC_t sobj;
#endif
#if !_FS_READONLY
    DWORD   last_clst;
    DWORD   free_clst;
#endif
#if _FS_RPATH != 0
    DWORD   cdir;
#if _FS_EXFAT
    DWORD   cdc_scl;
    DWORD   cdc_size;
    DWORD   cdc_ofs;
#endif
#endif
    DWORD   n_fatent;
    DWORD   fsize;
    DWORD   volbase;
    DWORD   fatbase;
    DWORD   dirbase;
    DWORD   database;
    DWORD   winsect;
    BYTE    win[_MAX_SS];
} FATFS;

// Object ID and allocation information (_FDID)
typedef struct {
    FATFS*  fs;
    WORD    id;
    BYTE    attr;
    BYTE    stat;
    DWORD   sclust;
    FSIZE_t objsize;
#if _FS_EXFAT
    DWORD   n_cont;
    DWORD   n_frag;
    DWORD   c_scl;
    DWORD   c_size;
    DWORD   c_ofs;
#endif
#if _FS_LOCK != 0
    UINT    lockid;
#endif
} _FDID;

// File object structure (FIL)
typedef struct {
    _FDID   obj;
    BYTE    flag;
    BYTE    err;
    FSIZE_t fptr;
    DWORD   clust;
    DWORD   sect;
#if !_FS_READONLY
    DWORD   dir_sect;
    BYTE*   dir_ptr;
#endif
#if _USE_FASTSEEK
    DWORD*  cltbl;
#endif
#if !_FS_TINY
    BYTE    buf[_MAX_SS];
#endif
} FIL;

// Directory object structure (DIR)
typedef struct {
    _FDID   obj;
    DWORD   dptr;
    DWORD   clust;
    DWORD   sect;
    BYTE*   dir;
    BYTE    fn[12];
#if _USE_LFN != 0
    DWORD   blk_ofs;
#endif
#if _USE_FIND
    const TCHAR* pat;
#endif
} DIR;

// File information structure (FILINFO)
typedef struct {
    FSIZE_t fsize;
    WORD    fdate;
    WORD    ftime;
    BYTE    fattrib;
#if _USE_LFN != 0
    TCHAR   altname[13];
    TCHAR   fname[_MAX_LFN + 1];
#else
    TCHAR   fname[13];
#endif
} FILINFO;

// File function return code (FRESULT)
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

// FatFs module application interface
FRESULT f_open (FIL* fp, const TCHAR* path, BYTE mode);
FRESULT f_close (FIL* fp);
FRESULT f_read (FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write (FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek (FIL* fp, FSIZE_t ofs);
FRESULT f_truncate (FIL* fp);
FRESULT f_sync (FIL* fp);
FRESULT f_opendir (DIR* dp, const TCHAR* path);
FRESULT f_closedir (DIR* dp);
FRESULT f_readdir (DIR* dp, FILINFO* fno);
FRESULT f_findfirst (DIR* dp, FILINFO* fno, const TCHAR* path, const TCHAR* pattern);
FRESULT f_findnext (DIR* dp, FILINFO* fno);
FRESULT f_mkdir (const TCHAR* path);
FRESULT f_unlink (const TCHAR* path);
FRESULT f_rename (const TCHAR* path_old, const TCHAR* path_new);
FRESULT f_stat (const TCHAR* path, FILINFO* fno);
FRESULT f_chmod (const TCHAR* path, BYTE attr, BYTE mask);
FRESULT f_utime (const TCHAR* path, const FILINFO* fno);
FRESULT f_chdir (const TCHAR* path);
FRESULT f_chdrive (const TCHAR* path);
FRESULT f_getcwd (TCHAR* buff, UINT len);
FRESULT f_getfree (const TCHAR* path, DWORD* nclst, FATFS** fatfs);
FRESULT f_getlabel (const TCHAR* path, TCHAR* label, DWORD* vsn);
FRESULT f_setlabel (const TCHAR* label);
FRESULT f_forward (FIL* fp, UINT(*func)(const BYTE*,UINT), UINT btf, UINT* bf);
FRESULT f_expand (FIL* fp, FSIZE_t szf, BYTE opt);
FRESULT f_mount (FATFS* fs, const TCHAR* path, BYTE opt);
FRESULT f_mkfs (const TCHAR* path, BYTE opt, DWORD au, void* work, UINT len);
FRESULT f_fdisk (BYTE pdrv, const DWORD* szt, void* work);
int f_putc (TCHAR c, FIL* fp);
int f_puts (const TCHAR* str, FIL* cp);
int f_printf (FIL* fp, const TCHAR* str, ...);
TCHAR* f_gets (TCHAR* buff, int len, FIL* fp);

#define f_eof(fp) ((int)((fp)->fptr == (fp)->obj.objsize))
#define f_error(fp) ((fp)->err)
#define f_tell(fp) ((fp)->fptr)
#define f_size(fp) ((fp)->obj.objsize)
#define f_rewind(fp) f_lseek((fp), 0)
#define f_rewinddir(dp) f_readdir((dp), 0)
#define f_rmdir(path) f_unlink(path)

#ifndef EOF
#define EOF (-1)
#endif

// File access mode flags
#define FA_READ             0x01
#define FA_WRITE            0x02
#define FA_OPEN_EXISTING    0x00
#define FA_CREATE_NEW       0x04
#define FA_CREATE_ALWAYS    0x08
#define FA_OPEN_ALWAYS      0x10
#define FA_OPEN_APPEND      0x30

#define CREATE_LINKMAP  ((FSIZE_t)0 - 1)

#define FM_FAT      0x01
#define FM_FAT32    0x02
#define FM_EXFAT    0x04
#define FM_ANY      0x07
#define FM_SFD      0x08

#define FS_FAT12    1
#define FS_FAT16    2
#define FS_FAT32    3
#define FS_EXFAT    4

#define AM_RDO  0x01
#define AM_HID  0x02
#define AM_SYS  0x04
#define AM_DIR  0x10
#define AM_ARC  0x20

// RTC function
#if !_FS_READONLY && !_FS_NORTC
DWORD get_fattime (void);
#endif

// Unicode support functions
#if _USE_LFN != 0
WCHAR ff_convert (WCHAR chr, UINT dir);
WCHAR ff_wtoupper (WCHAR chr);
#if _USE_LFN == 3
void* ff_memalloc (UINT msize);
void ff_memfree (void* mblock);
#endif
#endif

// Sync functions
#if _FS_REENTRANT
int ff_cre_syncobj (BYTE vol, _SYNC_t* sobj);
int ff_req_grant (_SYNC_t sobj);
void ff_rel_grant (_SYNC_t sobj);
int ff_del_syncobj (_SYNC_t sobj);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _FATFS */
