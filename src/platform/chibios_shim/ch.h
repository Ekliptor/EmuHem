// EmuHem ChibiOS Shim -- Maps ChibiOS/RT kernel API to C++23 std library
// This replaces the real ChibiOS kernel for desktop emulation.

#ifndef _CH_H_
#define _CH_H_

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include <cassert>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Basic ChibiOS types
// ---------------------------------------------------------------------------
using msg_t = int32_t;
using systime_t = uint32_t;
using eventmask_t = uint32_t;
using eventflags_t = uint32_t;
using flagsmask_t = uint32_t;
using tprio_t = uint8_t;
using tmode_t = uint8_t;
using tstate_t = uint8_t;
using trefs_t = uint8_t;
using tslices_t = uint32_t;
using cnt_t = int32_t;
using stkalign_t = uint64_t;

using ioportid_t = uint8_t;
using ioportmask_t = uint32_t;

// ---------------------------------------------------------------------------
// Thread states
// ---------------------------------------------------------------------------
#define THD_STATE_READY         0
#define THD_STATE_CURRENT       1
#define THD_STATE_SUSPENDED     2
#define THD_STATE_WTSEM         3
#define THD_STATE_WTMTX         4
#define THD_STATE_WTCOND        5
#define THD_STATE_SLEEPING      6
#define THD_STATE_WTEXIT        7
#define THD_STATE_WTOREVT       8
#define THD_STATE_WTANDEVT      9
#define THD_STATE_SNDMSGQ       10
#define THD_STATE_SNDMSG        11
#define THD_STATE_WTMSG         12
#define THD_STATE_WTQUEUE       13
#define THD_STATE_FINAL         14

#define THD_TERMINATE           4

// ---------------------------------------------------------------------------
// Priority constants
// ---------------------------------------------------------------------------
#define IDLEPRIO     1
#define LOWPRIO      2
#define NORMALPRIO   64
#define HIGHPRIO     127
#define ABSPRIO      255

// ---------------------------------------------------------------------------
// Status / result constants
// ---------------------------------------------------------------------------
#define RDY_OK       0
#define RDY_TIMEOUT  -1
#define RDY_RESET    -2
#define Q_OK         0
#define Q_TIMEOUT    -1
#define Q_RESET      -2
#define Q_EMPTY      -3
#define Q_FULL       -4

// ---------------------------------------------------------------------------
// Time constants and macros
// ---------------------------------------------------------------------------
#define CH_FREQUENCY 1000

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define TIME_IMMEDIATE  ((systime_t)0)
#define TIME_INFINITE   ((systime_t)-1)

#define S2ST(sec)   ((systime_t)((sec) * CH_FREQUENCY))
#define MS2ST(msec) ((systime_t)(msec))
#define US2ST(usec) ((systime_t)((usec) / 1000))
#define ST2MS(st)   ((unsigned)(st))
#define ST2US(st)   ((unsigned)((st) * 1000))

// ---------------------------------------------------------------------------
// Event macros
// ---------------------------------------------------------------------------
#define EVENT_MASK(n) ((eventmask_t)(1U << (n)))
#define ALL_EVENTS    ((eventmask_t)-1)

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
struct Thread;
struct Mutex;
struct Semaphore;
struct EventSource;
struct EventListener;

// ---------------------------------------------------------------------------
// Linked list helpers (used by Thread and various queues)
// ---------------------------------------------------------------------------
struct ThreadsList {
    Thread* p_next{nullptr};
};

struct ThreadsQueue {
    Thread* p_next{nullptr};
    Thread* p_prev{nullptr};
};

// ---------------------------------------------------------------------------
// context (processor context placeholder)
// ---------------------------------------------------------------------------
struct context {
    void* sp{nullptr};
};

// ---------------------------------------------------------------------------
// Thread structure -- must match the fields accessed by firmware
// ---------------------------------------------------------------------------
struct Thread {
    Thread* p_next{nullptr};
    Thread* p_prev{nullptr};
    tprio_t p_prio{NORMALPRIO};
    struct context p_ctx{};
    Thread* p_newer{nullptr};
    Thread* p_older{nullptr};
    const char* p_name{nullptr};
    stkalign_t* p_stklimit{nullptr};
    tstate_t p_state{THD_STATE_READY};
    tmode_t p_flags{0};
    trefs_t p_refs{1};
    tslices_t p_preempt{0};
    volatile systime_t p_time{0};

    union {
        msg_t rdymsg{0};
        msg_t exitcode;
        void* wtobjp;
        eventmask_t ewmask;
    } p_u;

    ThreadsList p_waiting{};
    ThreadsQueue p_msgqueue{};
    msg_t p_msg{0};
    eventmask_t p_epending{0};
    Mutex* p_mtxlist{nullptr};
    tprio_t p_realprio{NORMALPRIO};
    void* p_mpool{nullptr};

    // --- Emulator extensions (not in real ChibiOS) ---
    std::thread* emu_thread{nullptr};
    std::mutex emu_mutex;
    std::condition_variable emu_cv;
    std::atomic<bool> emu_terminate{false};
    std::atomic<bool> emu_suspended{false};
    int emu_wakeup_value{0};
};

// ---------------------------------------------------------------------------
// Mutex
// ---------------------------------------------------------------------------
struct Mutex {
    Thread* m_next{nullptr};    // linked list of mutexes owned by a thread
    Thread* m_owner{nullptr};
    std::recursive_mutex emu_mutex;
};

// ---------------------------------------------------------------------------
// Semaphore
// ---------------------------------------------------------------------------
struct Semaphore {
    ThreadsQueue s_queue{};
    cnt_t s_cnt{0};
    std::mutex emu_mutex;
    std::condition_variable emu_cv;
};

// ---------------------------------------------------------------------------
// Event Source / Listener
// ---------------------------------------------------------------------------
struct EventListener {
    EventListener* el_next{nullptr};
    Thread* el_listener{nullptr};
    eventmask_t el_mask{0};
    eventflags_t el_flags{0};
};

struct EventSource {
    EventListener* es_next{nullptr};
};

// ---------------------------------------------------------------------------
// Virtual Timer (minimal stub)
// ---------------------------------------------------------------------------
struct VirtualTimer {
    void* vt_next{nullptr};
    void* vt_prev{nullptr};
    systime_t vt_time{0};
    void* vt_func{nullptr};
    void* vt_par{nullptr};
};

// ---------------------------------------------------------------------------
// Working area macro
// ---------------------------------------------------------------------------
#define WORKING_AREA(name, size) \
    alignas(16) uint8_t name[(size) + sizeof(Thread) + 128]

#define THD_WORKING_AREA_SIZE(n) ((n) + sizeof(Thread) + 128)

// ---------------------------------------------------------------------------
// Thread function type
// ---------------------------------------------------------------------------
using tfunc_t = msg_t(*)(void*);

// ---------------------------------------------------------------------------
// Thread API
// ---------------------------------------------------------------------------
Thread* chThdCreateStatic(void* wsp, size_t size, tprio_t prio, tfunc_t pf, void* arg);
Thread* chThdCreateFromHeap(void* heap, size_t size, tprio_t prio, tfunc_t pf, void* arg);
Thread* chThdSelf(void);
inline void chRegSetThreadName(const char*) {}
void chThdExit(msg_t msg);
void chThdTerminate(Thread* tp);
msg_t chThdWait(Thread* tp);
bool chThdShouldTerminate(void);
systime_t chThdGetTicks(Thread* tp);
tprio_t chThdGetPriority(void);
tprio_t chThdSetPriority(tprio_t newprio);

void chThdSleepMilliseconds(unsigned msec);
void chThdSleepMicroseconds(unsigned usec);
void chThdSleep(systime_t time);
void chThdYield(void);

// ---------------------------------------------------------------------------
// Scheduler API (low-level, used by ThreadWait and BufferExchange)
// ---------------------------------------------------------------------------
Thread* chSchReadyI(Thread* tp);
void chSchGoSleepS(tstate_t newstate);
msg_t chSchGoSleepTimeoutS(tstate_t newstate, systime_t timeout);
void chSchWakeupS(Thread* ntp, msg_t msg);

// ---------------------------------------------------------------------------
// Event API
// ---------------------------------------------------------------------------
void chEvtInit(EventSource* esp);
void chEvtSignal(Thread* tp, eventmask_t mask);
void chEvtSignalI(Thread* tp, eventmask_t mask);
eventmask_t chEvtWaitAny(eventmask_t mask);
eventmask_t chEvtWaitAnyTimeout(eventmask_t mask, systime_t timeout);
eventmask_t chEvtWaitAll(eventmask_t mask);
void chEvtBroadcast(EventSource* esp);
void chEvtBroadcastI(EventSource* esp);
void chEvtBroadcastFlags(EventSource* esp, eventflags_t flags);
void chEvtBroadcastFlagsI(EventSource* esp, eventflags_t flags);
void chEvtRegister(EventSource* esp, EventListener* elp, eventmask_t mask);
void chEvtRegisterMask(EventSource* esp, EventListener* elp, eventmask_t mask);
void chEvtUnregister(EventSource* esp, EventListener* elp);
eventflags_t chEvtGetAndClearFlags(EventListener* elp);

// Alias
#define chEvtSignalF chEvtSignal

// ---------------------------------------------------------------------------
// Mutex API
// ---------------------------------------------------------------------------
void chMtxInit(Mutex* mp);
void chMtxLock(Mutex* mp);
bool chMtxTryLock(Mutex* mp);
void chMtxUnlock(void);
void chMtxUnlockS(void);
void chMtxUnlockAll(void);

// ---------------------------------------------------------------------------
// Semaphore API
// ---------------------------------------------------------------------------
void chSemInit(Semaphore* sp, cnt_t n);
msg_t chSemWait(Semaphore* sp);
msg_t chSemWaitTimeout(Semaphore* sp, systime_t timeout);
void chSemSignal(Semaphore* sp);
void chSemSignalI(Semaphore* sp);
void chSemReset(Semaphore* sp, cnt_t n);
void chSemResetI(Semaphore* sp, cnt_t n);

// ---------------------------------------------------------------------------
// System lock API (global lock emulating interrupt disable)
// ---------------------------------------------------------------------------
void chSysLock(void);
void chSysUnlock(void);
void chSysLockFromIsr(void);
void chSysUnlockFromIsr(void);
Thread* chSysGetIdleThread(void);

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
systime_t chTimeNow(void);
bool chTimeIsWithin(systime_t start, systime_t end);

// ---------------------------------------------------------------------------
// Virtual Timer
// ---------------------------------------------------------------------------
#define chVTIsArmedI(vtp) (false)
void chVTSetI(VirtualTimer* vtp, systime_t delay, void* func, void* par);
void chVTResetI(VirtualTimer* vtp);

// ---------------------------------------------------------------------------
// Heap / Core Memory (minimal)
// ---------------------------------------------------------------------------
void* chHeapAlloc(void* heapp, size_t size);
void chHeapFree(void* p);
size_t chHeapStatus(void* heapp, size_t* sizep);
size_t chCoreStatus(void);

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
void chDbgPanic(const char* msg);
#define chDbgAssert(c, r, m) do { if (!(c)) chDbgPanic(m); } while(0)
#define chDbgCheck(c, f) do { if (!(c)) chDbgPanic(#f); } while(0)

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
void port_wait_for_interrupt(void);
void chSysInit(void);
void chSysHalt(void);

// ---------------------------------------------------------------------------
// I/O Queues (minimal stubs for compilation -- USB serial uses these)
// ---------------------------------------------------------------------------
struct GenericQueue {
    uint8_t* q_buffer{nullptr};
    uint8_t* q_top{nullptr};
    uint8_t* q_wrptr{nullptr};
    uint8_t* q_rdptr{nullptr};
    size_t q_counter{0};
    ThreadsQueue q_waiting{};
    void* q_notify{nullptr};
    void* q_link{nullptr};
};

using InputQueue = GenericQueue;
using OutputQueue = GenericQueue;

msg_t chIQPutI(InputQueue* iqp, uint8_t b);
msg_t chIQGetTimeout(InputQueue* iqp, systime_t timeout);
size_t chIQReadTimeout(InputQueue* iqp, uint8_t* bp, size_t n, systime_t timeout);
void chIQResetI(InputQueue* iqp);
msg_t chOQPut(OutputQueue* oqp, uint8_t b);
msg_t chOQPutTimeout(OutputQueue* oqp, uint8_t b, systime_t timeout);
msg_t chOQGetI(OutputQueue* oqp);
size_t chOQWriteTimeout(OutputQueue* oqp, const uint8_t* bp, size_t n, systime_t timeout);
void chOQResetI(OutputQueue* oqp);

#define chIQIsEmptyI(q) ((q)->q_counter == 0)
#define chOQIsFullI(q)  ((q)->q_counter == 0)
#define chIQIsFullI(q)  (false)
#define chOQIsEmptyI(q) (true)
#define chQSizeI(q)     (0)

// ---------------------------------------------------------------------------
// Serial / BaseChannel / BaseAsynchronousChannel stubs
// ---------------------------------------------------------------------------
struct BaseChannel {
    const void* vmt{nullptr};
};

// ChibiOS HAL channel VMT macro (used as struct body in firmware headers)
#define _base_asynchronous_channel_methods \
    void* _dummy_vmt;

struct BaseAsynchronousChannelVMT {
    _base_asynchronous_channel_methods
};

struct BaseAsynchronousChannel {
    const BaseAsynchronousChannelVMT* vmt{nullptr};
    EventSource event{};
};

struct SerialDriver {
    const void* vmt{nullptr};
    EventSource event{};
    InputQueue iqueue{};
    OutputQueue oqueue{};
};

#define chnGetEventSource(ip) (&(ip)->event)
#define chnGetTimeout(ip, t) Q_TIMEOUT
#define chnPutTimeout(ip, c, t) Q_OK
#define chnWrite(ip, bp, n) (n)
#define chnWriteTimeout(ip, bp, n, t) (n)
#define chnRead(ip, bp, n) (0)
#define chnReadTimeout(ip, bp, n, t) (0)

// ---------------------------------------------------------------------------
// SDC driver stubs
// ---------------------------------------------------------------------------
#define BLK_READY 0
#define BLK_ACTIVE 1
#define HAL_SUCCESS 0
#define HAL_FAILED 1

struct SDCConfig {
    int dummy{0};
};

struct SDCDriver {
    int state{BLK_READY};
    Thread* thread{nullptr};
    SDCConfig* config{nullptr};
};

extern SDCDriver SDCD1;

bool sdcConnect(SDCDriver* sdcp);
bool sdcDisconnect(SDCDriver* sdcp);
void sdcStart(SDCDriver* sdcp, const SDCConfig* config);
void sdcStop(SDCDriver* sdcp);
inline bool sdcIsCardInserted(SDCDriver*) { return true; }

// ---------------------------------------------------------------------------
// RTC driver stubs
// ---------------------------------------------------------------------------
struct RTCTime {
    uint32_t tv_date{0};
    uint32_t tv_time{0};
};

struct RTCDriver {
    int dummy{0};
};

extern RTCDriver RTCD1;

void rtcGetTime(RTCDriver* rtcp, RTCTime* timespec);
void rtcSetTime(RTCDriver* rtcp, const RTCTime* timespec);
bool sdcRead(SDCDriver* sdcp, uint32_t startblk, uint8_t* buf, uint32_t n);
bool sdcWrite(SDCDriver* sdcp, uint32_t startblk, const uint8_t* buf, uint32_t n);
bool sdcGetInfo(SDCDriver* sdcp, void* info);

// ---------------------------------------------------------------------------
// I2C driver stub
// ---------------------------------------------------------------------------
using i2caddr_t = uint8_t;

#define I2CD_NO_ERROR   0
#define I2CD_BUS_ERROR  1
#define I2CD_ACK_FAILURE 2
#define I2CD_TIMEOUT    3

struct I2CConfig {
    int dummy{0};
};

struct I2CDriver {
    int state{0};
    const I2CConfig* config{nullptr};
    Thread* thread{nullptr};
    Mutex mutex{};
};

extern I2CDriver I2CD0;

void i2cStart(I2CDriver* i2cp, const I2CConfig* config);
void i2cStop(I2CDriver* i2cp);
msg_t i2cMasterTransmitTimeout(I2CDriver* i2cp, uint8_t addr,
                                const uint8_t* txbuf, size_t txbytes,
                                uint8_t* rxbuf, size_t rxbytes,
                                systime_t timeout);
msg_t i2cMasterReceiveTimeout(I2CDriver* i2cp, uint8_t addr,
                               uint8_t* rxbuf, size_t rxbytes,
                               systime_t timeout);
void i2cAcquireBus(I2CDriver* i2cp);
void i2cReleaseBus(I2CDriver* i2cp);
uint32_t i2cGetErrors(I2CDriver* i2cp);

// ---------------------------------------------------------------------------
// SPI driver stub
// ---------------------------------------------------------------------------
struct SPIConfig {
    int dummy{0};
};

struct SPIDriver {
    int state{0};
    const SPIConfig* config{nullptr};
    Mutex mutex{};
};

extern SPIDriver SPID2;

void spiStart(SPIDriver* spip, const SPIConfig* config);
void spiStop(SPIDriver* spip);
void spiSelect(SPIDriver* spip);
void spiUnselect(SPIDriver* spip);
void spiSend(SPIDriver* spip, size_t n, const void* txbuf);
void spiReceive(SPIDriver* spip, size_t n, void* rxbuf);
void spiExchange(SPIDriver* spip, size_t n, const void* txbuf, void* rxbuf);
void spiAcquireBus(SPIDriver* spip);
void spiReleaseBus(SPIDriver* spip);

// ---------------------------------------------------------------------------
// PAL (GPIO) stubs
// ---------------------------------------------------------------------------
using iopadid_t = uint8_t;

#define PAL_MODE_INPUT            0
#define PAL_MODE_OUTPUT_PUSHPULL  1
#define PAL_MODE_OUTPUT_OPENDRAIN 2
#define PAL_MODE_RESET            3

void palSetPad(ioportid_t port, uint8_t pad);
void palClearPad(ioportid_t port, uint8_t pad);
uint8_t palReadPad(ioportid_t port, uint8_t pad);
void palSetPadMode(ioportid_t port, uint8_t pad, uint32_t mode);
static inline void palTogglePad(ioportid_t, uint8_t) {}
static inline void palWritePad(ioportid_t, uint8_t, uint8_t) {}

// ---------------------------------------------------------------------------
// HAL polled delay (no-op in emulation)
// ---------------------------------------------------------------------------
static inline void halPolledDelay(uint32_t) {}

// ---------------------------------------------------------------------------
// Interrupt handlers (empty wrappers)
// ---------------------------------------------------------------------------
#define CH_IRQ_HANDLER(name) void name(void)
#define CH_IRQ_PROLOGUE()
#define CH_IRQ_EPILOGUE()

// ---------------------------------------------------------------------------
// NVIC stubs
// ---------------------------------------------------------------------------
#define CORTEX_PRIORITY_MASK(n) (0)
static inline void nvicEnableVector(uint32_t, uint32_t) {}
static inline void nvicDisableVector(uint32_t) {}

// ---------------------------------------------------------------------------
// Shell (minimal stubs)
// ---------------------------------------------------------------------------
struct ShellConfig {
    BaseChannel* sc_channel{nullptr};
    const void* sc_commands{nullptr};
};

static inline void shellInit(void) {}
static inline Thread* shellCreateStatic(const ShellConfig*, void*, size_t, tprio_t) { return nullptr; }

// ---------------------------------------------------------------------------
// chprintf
// ---------------------------------------------------------------------------
int chprintf(void* chp, const char* fmt, ...);
int chsnprintf(char* buf, size_t size, const char* fmt, ...);

// ---------------------------------------------------------------------------
// Ensure ch.h is self-contained for the firmware
// ---------------------------------------------------------------------------
#define CH_USE_EVENTS TRUE
#define CH_USE_MUTEXES TRUE
#define CH_USE_SEMAPHORES TRUE
#define CH_USE_WAITEXIT TRUE
#define CH_USE_HEAP TRUE
#define CH_USE_DYNAMIC TRUE
#define CH_USE_REGISTRY TRUE
#define CH_USE_MESSAGES TRUE
#define CH_USE_MEMPOOLS TRUE
#define CH_DBG_THREADS_PROFILING TRUE
#define CH_DBG_ENABLE_STACK_CHECK FALSE
#define CH_TIME_QUANTUM 20

#ifdef __cplusplus
}

// C++ overloads for RTC functions with lpc43xx::rtc::RTC type
// (firmware uses this instead of RTCTime because HAL_USE_RTC=0)
namespace lpc43xx { namespace rtc { struct RTC; } }
void rtcGetTime(RTCDriver* rtcp, lpc43xx::rtc::RTC* datetime);
void rtcSetTime(RTCDriver* rtcp, const lpc43xx::rtc::RTC* datetime);
#endif

#endif /* _CH_H_ */
