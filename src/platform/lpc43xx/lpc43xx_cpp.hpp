// EmuHem LPC43xx C++ wrapper shim
// Replaces firmware/common/lpc43xx_cpp.hpp with no-op implementations.
// Strips out static_assert offset checks and ARM intrinsics.

#ifndef __LPC43XX_CPP_H__
#define __LPC43XX_CPP_H__

#include <cstdint>
#include <hal.h>
#include "utility.hpp"

// Bridge for M0→M4 signaling: set by BasebandEventDispatcher::run()
extern Thread* g_m4_event_thread;

namespace lpc43xx {

namespace creg {

namespace m4txevent {

inline void enable() {}
inline void disable() {}
inline void assert_event() {}

inline void clear() {
    LPC_CREG->M4TXEVENT = 0;
}

} /* namespace m4txevent */

namespace m0apptxevent {

inline void enable() {}
inline void disable() {}
inline void assert_event() {
    // Signal the M4 event dispatcher thread (EVT_MASK_BASEBAND = EVENT_MASK(0) = 1)
    if (g_m4_event_thread) {
        chEvtSignal(g_m4_event_thread, 1U);
    }
}

inline void clear() {
    LPC_CREG->M0APPTXEVENT = 0;
}

} /* namespace m0apptxevent */

} /* namespace creg */

namespace cgu {

enum class CLK_SEL : uint8_t {
    RTC_32KHZ = 0x00,
    IRC = 0x01,
    ENET_RX_CLK = 0x02,
    ENET_TX_CLK = 0x03,
    GP_CLKIN = 0x04,
    XTAL = 0x06,
    PLL0USB = 0x07,
    PLL0AUDIO = 0x08,
    PLL1 = 0x09,
    IDIVA = 0x0c,
    IDIVB = 0x0d,
    IDIVC = 0x0e,
    IDIVD = 0x0f,
    IDIVE = 0x10,
};

struct IDIV_CTRL {
    uint32_t pd;
    uint32_t idiv;
    uint32_t autoblock;
    CLK_SEL clk_sel;

    constexpr operator uint32_t() const {
        return ((pd & 1) << 0) | ((idiv & 255) << 2) | ((autoblock & 1) << 11) | ((toUType(clk_sel) & 0x1f) << 24);
    }
};

namespace pll0audio {

struct CTRL {
    uint32_t pd;
    uint32_t bypass;
    uint32_t directi;
    uint32_t directo;
    uint32_t clken;
    uint32_t frm;
    uint32_t autoblock;
    uint32_t pllfract_req;
    uint32_t sel_ext;
    uint32_t mod_pd;
    CLK_SEL clk_sel;

    constexpr operator uint32_t() const {
        return ((pd & 1) << 0) | ((bypass & 1) << 1) | ((directi & 1) << 2) |
               ((directo & 1) << 3) | ((clken & 1) << 4) | ((frm & 1) << 6) |
               ((autoblock & 1) << 11) | ((pllfract_req & 1) << 12) |
               ((sel_ext & 1) << 13) | ((mod_pd & 1) << 14) |
               ((toUType(clk_sel) & 0x1f) << 24);
    }
};

struct MDIV {
    uint32_t mdec;
    constexpr operator uint32_t() const { return ((mdec & 0x1ffff) << 0); }
};

struct NP_DIV {
    uint32_t pdec;
    uint32_t ndec;
    constexpr operator uint32_t() const { return ((pdec & 0x7f) << 0) | ((ndec & 0x3ff) << 12); }
};

struct FRAC {
    uint32_t pllfract_ctrl;
    constexpr operator uint32_t() const { return ((pllfract_ctrl & 0x3fffff) << 0); }
};

inline void ctrl(const CTRL& value) { LPC_CGU->PLL0AUDIO_CTRL = value; }
inline void mdiv(const MDIV& value) { LPC_CGU->PLL0AUDIO_MDIV = value; }
inline void np_div(const NP_DIV& value) { LPC_CGU->PLL0AUDIO_NP_DIV = value; }
inline void frac(const FRAC& value) { LPC_CGU->PLL0AUDIO_FRAC = value; }
inline void power_up() { LPC_CGU->PLL0AUDIO_CTRL &= ~(1U << 0); }
inline void power_down() { LPC_CGU->PLL0AUDIO_CTRL |= (1U << 0); }
inline bool is_locked() { return true; } // Always locked in emulation
inline void clock_enable() { LPC_CGU->PLL0AUDIO_CTRL |= (1U << 4); }
inline void clock_disable() { LPC_CGU->PLL0AUDIO_CTRL &= ~(1U << 4); }

} /* namespace pll0audio */

namespace pll1 {

struct CTRL {
    uint32_t pd;
    uint32_t bypass;
    uint32_t fbsel;
    uint32_t direct;
    uint32_t psel;
    uint32_t autoblock;
    uint32_t nsel;
    uint32_t msel;
    CLK_SEL clk_sel;

    constexpr operator uint32_t() const {
        return ((pd & 1) << 0) | ((bypass & 1) << 1) | ((fbsel & 1) << 6) |
               ((direct & 1) << 7) | ((psel & 3) << 8) | ((autoblock & 1) << 11) |
               ((nsel & 3) << 12) | ((msel & 0xff) << 16) |
               ((toUType(clk_sel) & 0x1f) << 24);
    }
};

inline void ctrl(const CTRL& value) { LPC_CGU->PLL1_CTRL = value; }
inline void enable() { LPC_CGU->PLL1_CTRL &= ~(1U << 0); }
inline void disable() { LPC_CGU->PLL1_CTRL |= (1U << 0); }
inline void direct() { LPC_CGU->PLL1_CTRL |= (1U << 7); }
inline bool is_locked() { return true; } // Always locked in emulation

} /* namespace pll1 */

} /* namespace cgu */

namespace rgu {

enum class Reset {
    CORE = 0, PERIPH = 1, MASTER = 2, WWDT = 4, CREG = 5, BUS = 8, SCU = 9,
    M0_SUB = 12, M4_RST = 13, LCD = 16, USB0 = 17, USB1 = 18, DMA = 19,
    SDIO = 20, EMC = 21, ETHERNET = 22, FLASHA = 25, EEPROM = 27, GPIO = 28,
    FLASHB = 29, TIMER0 = 32, TIMER1 = 33, TIMER2 = 34, TIMER3 = 35,
    RITIMER = 36, SCT = 37, MOTOCONPWM = 38, QEI = 39, ADC0 = 40, ADC1 = 41,
    DAC = 42, UART0 = 44, UART1 = 45, UART2 = 46, UART3 = 47, I2C0 = 48,
    I2C1 = 49, SSP0 = 50, SSP1 = 51, I2S = 52, SPIFI = 53, CAN1 = 54,
    CAN0 = 55, M0APP = 56, SGPIO = 57, SPI = 58, ADCHS = 60,
};

enum class Status {
    NotActive = 0b00,
    ActivatedByRGUInput = 0b01,
    ActivatedBySoftware = 0b11,
};

inline void reset(const Reset reset) {
    LPC_RGU->RESET_CTRL[toUType(reset) >> 5] = (1U << (toUType(reset) & 0x1f));
}

inline void reset_mask(const uint64_t mask) {
    LPC_RGU->RESET_CTRL[0] = mask & 0xffffffffU;
    LPC_RGU->RESET_CTRL[1] = mask >> 32;
}

inline Status status(const Reset) { return Status::NotActive; }
inline bool active(const Reset) { return false; }
inline uint32_t external_status(const Reset) { return 0; }

inline uint64_t operator|(Reset r1, Reset r2) {
    return (1ULL << toUType(r1)) | (1ULL << toUType(r2));
}

inline uint64_t operator|(uint64_t m, Reset r) {
    return m | (1ULL << toUType(r));
}

} /* namespace rgu */

namespace scu {

struct SFS {
    uint32_t mode;
    uint32_t epd;
    uint32_t epun;
    uint32_t ehs;
    uint32_t ezi;
    uint32_t zif;

    constexpr operator uint32_t() const {
        return ((mode & 7) << 0) | ((epd & 1) << 3) | ((epun & 1) << 4) |
               ((ehs & 1) << 5) | ((ezi & 1) << 6) | ((zif & 1) << 7);
    }
};

} /* namespace scu */

namespace rtc {

namespace interrupt {
inline void clear_all() { LPC_RTC->ILR = (1U << 1) | (1U << 0); }
inline void enable_second_inc() { LPC_RTC->CIIR = (1U << 0); }
} /* namespace interrupt */

// RTC time struct (replaces ChibiOS RTCTime-based version)
struct RTC {
    uint32_t tv_date{0};
    uint32_t tv_time{0};

    constexpr RTC(uint32_t year, uint32_t month, uint32_t day,
                  uint32_t hour, uint32_t minute, uint32_t second)
        : tv_date{(year << 16) | (month << 8) | (day << 0)},
          tv_time{(hour << 16) | (minute << 8) | (second << 0)} {}

    constexpr RTC() = default;

    uint16_t year() const { return (tv_date >> 16) & 0xfff; }
    uint8_t month() const { return (tv_date >> 8) & 0x00f; }
    uint8_t day() const { return (tv_date >> 0) & 0x01f; }
    uint8_t hour() const { return (tv_time >> 16) & 0x01f; }
    uint8_t minute() const { return (tv_time >> 8) & 0x03f; }
    uint8_t second() const { return (tv_time >> 0) & 0x03f; }
};

} /* namespace rtc */

namespace spifi {

struct CTRL {
    uint32_t timeout;
    uint32_t cshigh;
    uint32_t d_prftch_dis;
    uint32_t inten;
    uint32_t mode3;
    uint32_t prftch_dis;
    uint32_t dual;
    uint32_t rfclk;
    uint32_t fbclk;
    uint32_t dmaen;

    constexpr operator uint32_t() const {
        return ((timeout & 0xffff) << 0) | ((cshigh & 1) << 16) |
               ((d_prftch_dis & 1) << 21) | ((inten & 1) << 22) |
               ((mode3 & 1) << 23) | ((prftch_dis & 1) << 27) |
               ((dual & 1) << 28) | ((rfclk & 1) << 29) |
               ((fbclk & 1) << 30) | ((dmaen & 1) << 31);
    }
};

} /* namespace spifi */

} /* namespace lpc43xx */

// ARM intrinsic stubs
#ifndef __arm__
static inline uint32_t __get_APSR() { return 0; }
static inline void __SEV() {}
static inline void __WFE() {}
static inline void __WFI() {}
static inline void __NOP() {}
static inline void __DSB() {}
static inline void __ISB() {}
static inline void __DMB() {}
static inline uint32_t __get_CONTROL() { return 0; }
static inline uint32_t __get_MSP() { return 0; }
static inline void __set_MSP(uint32_t) {}
static inline uint32_t __get_PSP() { return 0; }
static inline void __set_PSP(uint32_t) {}
#define __asm__(x)

// DSP intrinsics (ARM Cortex-M4 DSP extensions, emulated in plain C++).
// Semantics mirror the instructions in ARM ARMv7-M Architecture Reference Manual.
// All arguments are treated as unsigned 32-bit words; signedness is applied
// per-field where the instruction demands it.

static inline uint32_t __ror32(uint32_t v, uint32_t r) {
    r &= 31;
    return r == 0 ? v : ((v >> r) | (v << (32 - r)));
}

static inline int32_t __SXTB16(uint32_t rm, uint32_t ror) {
    const uint32_t v = __ror32(rm, ror);
    const int16_t lo = static_cast<int8_t>(v & 0xFF);
    const int16_t hi = static_cast<int8_t>((v >> 16) & 0xFF);
    return (static_cast<uint32_t>(static_cast<uint16_t>(hi)) << 16)
         | static_cast<uint16_t>(lo);
}

static inline int32_t __SXTH(uint32_t rm, uint32_t ror) {
    const uint32_t v = __ror32(rm, ror);
    return static_cast<int16_t>(v & 0xFFFF);
}

static inline int32_t __SXTAH(int32_t rn, uint32_t rm, uint32_t ror) {
    return rn + __SXTH(rm, ror);
}

static inline int32_t __SMUAD(uint32_t a, uint32_t b) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return (a_lo * b_lo) + (a_hi * b_hi);
}

static inline int32_t __SMLAD(uint32_t a, uint32_t b, int32_t acc) {
    return __SMUAD(a, b) + acc;
}

static inline int32_t __SMUADX(uint32_t a, uint32_t b) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return (a_lo * b_hi) + (a_hi * b_lo);
}

static inline int32_t __SMLADX(uint32_t a, uint32_t b, int32_t acc) {
    return __SMUADX(a, b) + acc;
}

static inline int32_t __SMUSD(uint32_t a, uint32_t b) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return (a_lo * b_lo) - (a_hi * b_hi);
}

static inline int32_t __SMUSDX(uint32_t a, uint32_t b) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return (a_lo * b_hi) - (a_hi * b_lo);
}

static inline int32_t __SMLSD(uint32_t a, uint32_t b, int32_t acc) {
    return __SMUSD(a, b) + acc;
}

static inline int64_t __SMLALD(uint32_t a, uint32_t b, int64_t acc) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return acc + static_cast<int64_t>(a_lo) * b_lo
               + static_cast<int64_t>(a_hi) * b_hi;
}

static inline int64_t __SMLALDX(uint32_t a, uint32_t b, int64_t acc) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return acc + static_cast<int64_t>(a_lo) * b_hi
               + static_cast<int64_t>(a_hi) * b_lo;
}

static inline int64_t __SMLSLD(uint32_t a, uint32_t b, int64_t acc) {
    const int16_t a_lo = static_cast<int16_t>(a & 0xFFFF);
    const int16_t a_hi = static_cast<int16_t>((a >> 16) & 0xFFFF);
    const int16_t b_lo = static_cast<int16_t>(b & 0xFFFF);
    const int16_t b_hi = static_cast<int16_t>((b >> 16) & 0xFFFF);
    return acc + static_cast<int64_t>(a_lo) * b_lo
               - static_cast<int64_t>(a_hi) * b_hi;
}

static inline int32_t __SMULBB(uint32_t a, uint32_t b) {
    return static_cast<int16_t>(a & 0xFFFF) * static_cast<int16_t>(b & 0xFFFF);
}
static inline int32_t __SMULBT(uint32_t a, uint32_t b) {
    return static_cast<int16_t>(a & 0xFFFF) * static_cast<int16_t>((b >> 16) & 0xFFFF);
}
static inline int32_t __SMULTB(uint32_t a, uint32_t b) {
    return static_cast<int16_t>((a >> 16) & 0xFFFF) * static_cast<int16_t>(b & 0xFFFF);
}
static inline int32_t __SMULTT(uint32_t a, uint32_t b) {
    return static_cast<int16_t>((a >> 16) & 0xFFFF) * static_cast<int16_t>((b >> 16) & 0xFFFF);
}

static inline int32_t __SMLATB(uint32_t a, uint32_t b, int32_t acc) {
    return __SMULTB(a, b) + acc;
}

static inline int32_t __SMLABB(uint32_t a, uint32_t b, int32_t acc) {
    return __SMULBB(a, b) + acc;
}

static inline int32_t __SMLABT(uint32_t a, uint32_t b, int32_t acc) {
    return __SMULBT(a, b) + acc;
}

static inline int32_t __SMLATT(uint32_t a, uint32_t b, int32_t acc) {
    return __SMULTT(a, b) + acc;
}

static inline int32_t __SMMULR(int32_t a, int32_t b) {
    const int64_t full = static_cast<int64_t>(a) * b;
    return static_cast<int32_t>((full + 0x80000000LL) >> 32);
}

static inline int32_t __sat32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(v);
}

static inline int32_t __QADD(int32_t a, int32_t b) {
    return __sat32(static_cast<int64_t>(a) + b);
}

static inline int32_t __QSUB(int32_t a, int32_t b) {
    return __sat32(static_cast<int64_t>(a) - b);
}

static inline int16_t __sat16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

static inline uint32_t __QADD16(uint32_t a, uint32_t b) {
    const int16_t r_lo = __sat16(static_cast<int16_t>(a & 0xFFFF) + static_cast<int16_t>(b & 0xFFFF));
    const int16_t r_hi = __sat16(static_cast<int16_t>((a >> 16) & 0xFFFF) + static_cast<int16_t>((b >> 16) & 0xFFFF));
    return (static_cast<uint32_t>(static_cast<uint16_t>(r_hi)) << 16)
         | static_cast<uint16_t>(r_lo);
}

static inline uint32_t __QSUB16(uint32_t a, uint32_t b) {
    const int16_t r_lo = __sat16(static_cast<int16_t>(a & 0xFFFF) - static_cast<int16_t>(b & 0xFFFF));
    const int16_t r_hi = __sat16(static_cast<int16_t>((a >> 16) & 0xFFFF) - static_cast<int16_t>((b >> 16) & 0xFFFF));
    return (static_cast<uint32_t>(static_cast<uint16_t>(r_hi)) << 16)
         | static_cast<uint16_t>(r_lo);
}

static inline uint32_t __PKHBT(uint32_t a, uint32_t b, uint32_t shift) {
    return (a & 0xFFFF) | ((b << shift) & 0xFFFF0000);
}

static inline uint32_t __PKHTB(uint32_t a, uint32_t b, uint32_t shift) {
    return (a & 0xFFFF0000) | (((b >> shift) & 0xFFFF));
}

static inline uint32_t __BFI(uint32_t rd, uint32_t rn, uint32_t lsb, uint32_t width) {
    const uint32_t mask = width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (rd & ~(mask << lsb)) | ((rn & mask) << lsb);
}

static inline int32_t __SSAT(int32_t val, uint32_t sat) {
    int32_t max_val = (1 << (sat - 1)) - 1;
    int32_t min_val = -(1 << (sat - 1));
    if (val > max_val) return max_val;
    if (val < min_val) return min_val;
    return val;
}

static inline uint32_t __USAT(int32_t val, uint32_t sat) {
    uint32_t max_val = (1U << sat) - 1;
    if (val < 0) return 0;
    if (static_cast<uint32_t>(val) > max_val) return max_val;
    return static_cast<uint32_t>(val);
}

static inline uint32_t __CLZ(uint32_t val) {
    if (val == 0) return 32;
    return static_cast<uint32_t>(__builtin_clz(val));
}

static inline uint32_t __RBIT(uint32_t val) {
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        result = (result << 1) | (val & 1);
        val >>= 1;
    }
    return result;
}

static inline uint32_t __REV(uint32_t val) {
    return __builtin_bswap32(val);
}

static inline uint32_t __REV16(uint32_t val) {
    // ARM rev16: reverse byte order within each halfword of a 32-bit word.
    return ((val & 0xFF00FF00u) >> 8) | ((val & 0x00FF00FFu) << 8);
}

// SIMD pointer access macro (used by DSP code to read packed 16-bit I/Q pairs).
// Uses a C-style cast so the macro also works when `addr` is declared as
// `const void*` (dsp_decimate uses this pattern when iterating read-only buffers).
#define __SIMD32_TYPE int32_t
#define __SIMD32(addr) (*(__SIMD32_TYPE**)&(addr))

#endif // !__arm__

// IRQ number stubs
#define M4CORE_IRQn    1
#define M0CORE_IRQn    1
#define LPC43XX_M4TXEVENT_IRQ_PRIORITY 3
#define LPC43XX_M0APPTXEVENT_IRQ_PRIORITY 3

// HAL_USE_RTC flag -- we don't use ChibiOS RTC, provide our own above
#define HAL_USE_RTC 0

#endif /*__LPC43XX_CPP_H__*/
