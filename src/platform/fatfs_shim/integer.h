// EmuHem FatFS integer type shim
// Matches firmware/chibios-portapack/ext/fatfs/src/integer.h exactly.

#ifndef _FF_INTEGER
#define _FF_INTEGER

typedef int             INT;
typedef unsigned int    UINT;
typedef unsigned char   BYTE;
typedef short           SHORT;
typedef unsigned short  WORD;
typedef unsigned short  WCHAR;
typedef long            LONG;
typedef unsigned long   DWORD;
typedef unsigned long long QWORD;

#endif
