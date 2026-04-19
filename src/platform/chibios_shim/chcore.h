// EmuHem chcore.h stub
// Provides ARM Cortex-M context types used by some firmware headers.

#ifndef _CHCORE_H_
#define _CHCORE_H_

#include <cstdint>

struct extctx {
    uint32_t r0, r1, r2, r3, r12, lr_thd, pc, xpsr;
};

struct intctx {
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11, lr;
};

#endif // _CHCORE_H_
