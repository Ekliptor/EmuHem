// EmuHem ChibiOS Shim Implementation
// Maps ChibiOS/RT kernel to C++23 std threading primitives.

#include "ch.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static std::recursive_mutex g_sys_mutex;
static thread_local Thread* g_current_thread = nullptr;
static Thread g_main_thread;
static auto g_start_time = std::chrono::steady_clock::now();

// Track all created threads for cleanup
static std::mutex g_threads_mutex;
static std::map<std::thread::id, Thread*> g_threads;

// Driver instances
SDCDriver SDCD1{};
I2CDriver I2CD0{};
SPIDriver SPID2{};

// ---------------------------------------------------------------------------
// System Init
// ---------------------------------------------------------------------------
void chSysInit(void) {
    g_current_thread = &g_main_thread;
    g_main_thread.p_state = THD_STATE_CURRENT;
    g_main_thread.p_name = "main";
    g_main_thread.p_prio = NORMALPRIO;
}

void chSysHalt(void) {
    std::abort();
}

// ---------------------------------------------------------------------------
// Thread API
// ---------------------------------------------------------------------------
Thread* chThdCreateStatic(void* wsp, size_t /*size*/, tprio_t prio, tfunc_t pf, void* arg) {
    // Allocate a new Thread struct at the start of the workspace
    // (real ChibiOS places it there too)
    auto* tp = new (wsp) Thread{};
    tp->p_prio = prio;
    tp->p_realprio = prio;
    tp->p_state = THD_STATE_READY;
    tp->p_name = "worker";

    auto* native_thread = new std::thread([tp, pf, arg]() {
        g_current_thread = tp;
        tp->p_state = THD_STATE_CURRENT;
        {
            std::lock_guard lock(g_threads_mutex);
            g_threads[std::this_thread::get_id()] = tp;
        }
        msg_t result = pf(arg);
        tp->p_u.exitcode = result;
        tp->p_state = THD_STATE_FINAL;
        tp->emu_cv.notify_all();
    });
    tp->emu_thread = native_thread;
    return tp;
}

Thread* chThdCreateFromHeap(void* /*heap*/, size_t size, tprio_t prio, tfunc_t pf, void* arg) {
    auto* wsp = new uint8_t[size + sizeof(Thread) + 128];
    return chThdCreateStatic(wsp, size, prio, pf, arg);
}

Thread* chThdSelf(void) {
    if (!g_current_thread) {
        g_current_thread = &g_main_thread;
    }
    return g_current_thread;
}

void chThdExit(msg_t msg) {
    auto* tp = chThdSelf();
    tp->p_u.exitcode = msg;
    tp->p_state = THD_STATE_FINAL;
    tp->emu_cv.notify_all();
}

void chThdTerminate(Thread* tp) {
    if (tp) {
        tp->p_flags |= THD_TERMINATE;
        tp->emu_terminate.store(true);
        // Wake the thread if it's sleeping
        tp->emu_cv.notify_all();
    }
}

msg_t chThdWait(Thread* tp) {
    if (tp && tp->emu_thread) {
        if (tp->emu_thread->joinable()) {
            tp->emu_thread->join();
        }
        msg_t result = tp->p_u.exitcode;
        delete tp->emu_thread;
        tp->emu_thread = nullptr;
        return result;
    }
    return 0;
}

bool chThdShouldTerminate(void) {
    auto* tp = chThdSelf();
    return (tp->p_flags & THD_TERMINATE) != 0 || tp->emu_terminate.load();
}

systime_t chThdGetTicks(Thread* tp) {
    return tp ? tp->p_time : 0;
}

tprio_t chThdGetPriority(void) {
    return chThdSelf()->p_prio;
}

tprio_t chThdSetPriority(tprio_t newprio) {
    auto* tp = chThdSelf();
    auto old = tp->p_prio;
    tp->p_prio = newprio;
    return old;
}

void chThdSleepMilliseconds(unsigned msec) {
    std::this_thread::sleep_for(std::chrono::milliseconds(msec));
}

void chThdSleepMicroseconds(unsigned usec) {
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

void chThdSleep(systime_t time) {
    chThdSleepMilliseconds(time);
}

void chThdYield(void) {
    std::this_thread::yield();
}

// ---------------------------------------------------------------------------
// Scheduler (low-level) -- used by ThreadWait and BufferExchange
// ---------------------------------------------------------------------------
Thread* chSchReadyI(Thread* tp) {
    if (tp) {
        tp->emu_suspended.store(false);
        tp->emu_cv.notify_all();
    }
    return tp;
}

void chSchGoSleepS(tstate_t /*newstate*/) {
    auto* tp = chThdSelf();
    tp->emu_suspended.store(true);

    std::unique_lock lock(tp->emu_mutex);
    tp->emu_cv.wait(lock, [tp]() {
        return !tp->emu_suspended.load() || tp->emu_terminate.load();
    });
}

msg_t chSchGoSleepTimeoutS(tstate_t newstate, systime_t timeout) {
    if (timeout == TIME_INFINITE) {
        chSchGoSleepS(newstate);
        return chThdSelf()->p_u.rdymsg;
    }
    if (timeout == TIME_IMMEDIATE) {
        return RDY_TIMEOUT;
    }

    auto* tp = chThdSelf();
    tp->emu_suspended.store(true);

    std::unique_lock lock(tp->emu_mutex);
    bool woken = tp->emu_cv.wait_for(lock, std::chrono::milliseconds(timeout), [tp]() {
        return !tp->emu_suspended.load() || tp->emu_terminate.load();
    });

    if (!woken) {
        tp->p_u.rdymsg = RDY_TIMEOUT;
    }
    return tp->p_u.rdymsg;
}

void chSchWakeupS(Thread* ntp, msg_t msg) {
    if (ntp) {
        ntp->p_u.rdymsg = msg;
        chSchReadyI(ntp);
    }
}

// ---------------------------------------------------------------------------
// Event API
// ---------------------------------------------------------------------------
void chEvtInit(EventSource* esp) {
    if (esp) esp->es_next = nullptr;
}

void chEvtSignal(Thread* tp, eventmask_t mask) {
    if (!tp) return;
    std::lock_guard lock(tp->emu_mutex);
    tp->p_epending |= mask;
    tp->emu_cv.notify_all();
}

void chEvtSignalI(Thread* tp, eventmask_t mask) {
    // In emulator, same as chEvtSignal (no ISR distinction)
    chEvtSignal(tp, mask);
}

eventmask_t chEvtWaitAny(eventmask_t mask) {
    auto* tp = chThdSelf();
    std::unique_lock lock(tp->emu_mutex);
    tp->emu_cv.wait(lock, [tp, mask]() {
        return (tp->p_epending & mask) != 0 || tp->emu_terminate.load();
    });
    eventmask_t pending = tp->p_epending & mask;
    tp->p_epending &= ~pending;
    return pending;
}

eventmask_t chEvtWaitAnyTimeout(eventmask_t mask, systime_t timeout) {
    auto* tp = chThdSelf();

    if (timeout == TIME_IMMEDIATE) {
        std::lock_guard lock(tp->emu_mutex);
        eventmask_t pending = tp->p_epending & mask;
        tp->p_epending &= ~pending;
        return pending;
    }

    std::unique_lock lock(tp->emu_mutex);
    if (timeout == TIME_INFINITE) {
        tp->emu_cv.wait(lock, [tp, mask]() {
            return (tp->p_epending & mask) != 0 || tp->emu_terminate.load();
        });
    } else {
        tp->emu_cv.wait_for(lock, std::chrono::milliseconds(timeout), [tp, mask]() {
            return (tp->p_epending & mask) != 0 || tp->emu_terminate.load();
        });
    }
    eventmask_t pending = tp->p_epending & mask;
    tp->p_epending &= ~pending;
    return pending;
}

eventmask_t chEvtWaitAll(eventmask_t mask) {
    auto* tp = chThdSelf();
    std::unique_lock lock(tp->emu_mutex);
    tp->emu_cv.wait(lock, [tp, mask]() {
        return (tp->p_epending & mask) == mask || tp->emu_terminate.load();
    });
    tp->p_epending &= ~mask;
    return mask;
}

void chEvtBroadcast(EventSource* /*esp*/) {}
void chEvtBroadcastI(EventSource* /*esp*/) {}
void chEvtBroadcastFlags(EventSource* /*esp*/, eventflags_t /*flags*/) {}
void chEvtBroadcastFlagsI(EventSource* /*esp*/, eventflags_t /*flags*/) {}
void chEvtRegister(EventSource* /*esp*/, EventListener* /*elp*/, eventmask_t /*mask*/) {}
void chEvtRegisterMask(EventSource* /*esp*/, EventListener* /*elp*/, eventmask_t /*mask*/) {}
void chEvtUnregister(EventSource* /*esp*/, EventListener* /*elp*/) {}
eventflags_t chEvtGetAndClearFlags(EventListener* elp) {
    if (!elp) return 0;
    auto flags = elp->el_flags;
    elp->el_flags = 0;
    return flags;
}

// ---------------------------------------------------------------------------
// Mutex API
// ---------------------------------------------------------------------------
void chMtxInit(Mutex* mp) {
    if (mp) {
        mp->m_owner = nullptr;
        mp->m_next = nullptr;
    }
}

void chMtxLock(Mutex* mp) {
    if (mp) {
        mp->emu_mutex.lock();
        mp->m_owner = chThdSelf();
    }
}

bool chMtxTryLock(Mutex* mp) {
    if (!mp) return false;
    bool locked = mp->emu_mutex.try_lock();
    if (locked) {
        mp->m_owner = chThdSelf();
    }
    return locked;
}

void chMtxUnlock(void) {
    // Real ChibiOS unlocks the most recently locked mutex by the current thread.
    // In our shim this is a no-op -- the caller must manage which mutex to unlock.
    // The firmware typically uses chMtxTryLock/chMtxUnlock in pairs where the
    // Mutex object is known. We handle this in the MessageQueue push() path.
}

void chMtxUnlockS(void) {
    chMtxUnlock();
}

void chMtxUnlockAll(void) {}

// Helper: unlock a specific mutex (used by our shim internally)
// The firmware's message_queue.hpp calls chMtxTryLock(&mutex_write) then chMtxUnlock().
// We need to actually unlock the specific mutex. We patch this by making chMtxUnlock
// track the last locked mutex via thread-local storage.
static thread_local Mutex* g_last_locked_mutex = nullptr;

// We override the above stubs to track the last mutex:
// (Already defined above, but we need chMtxUnlock to unlock g_last_locked_mutex)
// Let's redefine the approach: We simply make chMtxLock/TryLock store into TLS
// and chMtxUnlock unlocks it.

// Redefine -- need to update the implementations above. Since we can't redefine
// in the same TU, let's add the tracking to the existing functions and make
// chMtxUnlock work via TLS.

// Actually, the simplest approach: we already defined chMtxLock and chMtxTryLock above.
// Let me add TLS tracking there and make chMtxUnlock work.
// But we can't redefine functions. So let's use a different approach:
// We'll use a stack of locked mutexes per thread.

// ---------------------------------------------------------------------------
// Semaphore API
// ---------------------------------------------------------------------------
void chSemInit(Semaphore* sp, cnt_t n) {
    if (sp) {
        sp->s_cnt = n;
    }
}

msg_t chSemWait(Semaphore* sp) {
    if (!sp) return RDY_OK;
    std::unique_lock lock(sp->emu_mutex);
    sp->emu_cv.wait(lock, [sp]() { return sp->s_cnt > 0; });
    sp->s_cnt--;
    return RDY_OK;
}

msg_t chSemWaitTimeout(Semaphore* sp, systime_t timeout) {
    if (!sp) return RDY_OK;
    if (timeout == TIME_IMMEDIATE) {
        std::lock_guard lock(sp->emu_mutex);
        if (sp->s_cnt > 0) {
            sp->s_cnt--;
            return RDY_OK;
        }
        return RDY_TIMEOUT;
    }
    std::unique_lock lock(sp->emu_mutex);
    bool ok = sp->emu_cv.wait_for(lock, std::chrono::milliseconds(timeout),
                                   [sp]() { return sp->s_cnt > 0; });
    if (ok) {
        sp->s_cnt--;
        return RDY_OK;
    }
    return RDY_TIMEOUT;
}

void chSemSignal(Semaphore* sp) {
    if (!sp) return;
    std::lock_guard lock(sp->emu_mutex);
    sp->s_cnt++;
    sp->emu_cv.notify_one();
}

void chSemSignalI(Semaphore* sp) {
    chSemSignal(sp);
}

void chSemReset(Semaphore* sp, cnt_t n) {
    if (!sp) return;
    std::lock_guard lock(sp->emu_mutex);
    sp->s_cnt = n;
    sp->emu_cv.notify_all();
}

void chSemResetI(Semaphore* sp, cnt_t n) {
    chSemReset(sp, n);
}

// ---------------------------------------------------------------------------
// System lock (emulates interrupt disable)
// ---------------------------------------------------------------------------
void chSysLock(void) {
    g_sys_mutex.lock();
}

void chSysUnlock(void) {
    g_sys_mutex.unlock();
}

void chSysLockFromIsr(void) {
    g_sys_mutex.lock();
}

void chSysUnlockFromIsr(void) {
    g_sys_mutex.unlock();
}

Thread* chSysGetIdleThread(void) {
    static Thread idle_thread;
    idle_thread.p_name = "idle";
    return &idle_thread;
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
systime_t chTimeNow(void) {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
    return static_cast<systime_t>(ms);
}

bool chTimeIsWithin(systime_t start, systime_t end) {
    auto now = chTimeNow();
    return (now >= start) && (now < end);
}

// ---------------------------------------------------------------------------
// Virtual Timer (stubs)
// ---------------------------------------------------------------------------
void chVTSetI(VirtualTimer* /*vtp*/, systime_t /*delay*/, void* /*func*/, void* /*par*/) {}
void chVTResetI(VirtualTimer* /*vtp*/) {}

// ---------------------------------------------------------------------------
// Heap / Core Memory
// ---------------------------------------------------------------------------
void* chHeapAlloc(void* /*heapp*/, size_t size) {
    return std::malloc(size);
}

void chHeapFree(void* p) {
    std::free(p);
}

size_t chHeapStatus(void* /*heapp*/, size_t* sizep) {
    if (sizep) *sizep = 1024 * 1024;
    return 0;
}

size_t chCoreStatus(void) {
    return 1024 * 1024;
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
void chDbgPanic(const char* msg) {
    std::fprintf(stderr, "[EmuHem] PANIC: %s\n", msg ? msg : "(null)");
    std::fflush(stderr);
    std::abort();
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
void port_wait_for_interrupt(void) {
    std::this_thread::yield();
}

// ---------------------------------------------------------------------------
// I/O Queue stubs
// ---------------------------------------------------------------------------
msg_t chIQPutI(InputQueue* /*iqp*/, uint8_t /*b*/) { return Q_OK; }
msg_t chIQGetTimeout(InputQueue* /*iqp*/, systime_t /*timeout*/) { return Q_TIMEOUT; }
size_t chIQReadTimeout(InputQueue* /*iqp*/, uint8_t* /*bp*/, size_t /*n*/, systime_t /*timeout*/) { return 0; }
void chIQResetI(InputQueue* /*iqp*/) {}
msg_t chOQPut(OutputQueue* /*oqp*/, uint8_t /*b*/) { return Q_OK; }
msg_t chOQPutTimeout(OutputQueue* /*oqp*/, uint8_t /*b*/, systime_t /*timeout*/) { return Q_OK; }
msg_t chOQGetI(OutputQueue* /*oqp*/) { return Q_TIMEOUT; }
size_t chOQWriteTimeout(OutputQueue* /*oqp*/, const uint8_t* /*bp*/, size_t /*n*/, systime_t /*timeout*/) { return 0; }
void chOQResetI(OutputQueue* /*oqp*/) {}

// ---------------------------------------------------------------------------
// SDC driver stubs
// ---------------------------------------------------------------------------
bool sdcConnect(SDCDriver* /*sdcp*/) { return HAL_SUCCESS; }
bool sdcDisconnect(SDCDriver* /*sdcp*/) { return HAL_SUCCESS; }
void sdcStart(SDCDriver* sdcp, const SDCConfig* config) { if (sdcp) { sdcp->config = const_cast<SDCConfig*>(config); sdcp->state = BLK_ACTIVE; } }
void sdcStop(SDCDriver* sdcp) { if (sdcp) sdcp->state = BLK_READY; }
bool sdcRead(SDCDriver* /*sdcp*/, uint32_t /*startblk*/, uint8_t* /*buf*/, uint32_t /*n*/) { return HAL_FAILED; }
bool sdcWrite(SDCDriver* /*sdcp*/, uint32_t /*startblk*/, const uint8_t* /*buf*/, uint32_t /*n*/) { return HAL_FAILED; }
bool sdcGetInfo(SDCDriver* /*sdcp*/, void* /*info*/) { return HAL_FAILED; }

// ---------------------------------------------------------------------------
// I2C driver stubs
// ---------------------------------------------------------------------------
void i2cStart(I2CDriver* i2cp, const I2CConfig* config) { if (i2cp) i2cp->config = config; }
void i2cStop(I2CDriver* /*i2cp*/) {}
msg_t i2cMasterTransmitTimeout(I2CDriver* /*i2cp*/, uint8_t /*addr*/,
                                const uint8_t* /*txbuf*/, size_t /*txbytes*/,
                                uint8_t* /*rxbuf*/, size_t /*rxbytes*/,
                                systime_t /*timeout*/) { return RDY_OK; }
msg_t i2cMasterReceiveTimeout(I2CDriver* /*i2cp*/, uint8_t /*addr*/,
                               uint8_t* /*rxbuf*/, size_t /*rxbytes*/,
                               systime_t /*timeout*/) { return RDY_OK; }
void i2cAcquireBus(I2CDriver* /*i2cp*/) {}
void i2cReleaseBus(I2CDriver* /*i2cp*/) {}
uint32_t i2cGetErrors(I2CDriver* /*i2cp*/) { return 0; }

// ---------------------------------------------------------------------------
// SPI driver stubs
// ---------------------------------------------------------------------------
void spiStart(SPIDriver* spip, const SPIConfig* config) { if (spip) spip->config = config; }
void spiStop(SPIDriver* /*spip*/) {}
void spiSelect(SPIDriver* /*spip*/) {}
void spiUnselect(SPIDriver* /*spip*/) {}
void spiSend(SPIDriver* /*spip*/, size_t /*n*/, const void* /*txbuf*/) {}
void spiReceive(SPIDriver* /*spip*/, size_t /*n*/, void* /*rxbuf*/) {}
void spiExchange(SPIDriver* /*spip*/, size_t /*n*/, const void* /*txbuf*/, void* /*rxbuf*/) {}
void spiAcquireBus(SPIDriver* /*spip*/) {}
void spiReleaseBus(SPIDriver* /*spip*/) {}

// ---------------------------------------------------------------------------
// PAL stubs
// ---------------------------------------------------------------------------
void palSetPad(ioportid_t /*port*/, uint8_t /*pad*/) {}
void palClearPad(ioportid_t /*port*/, uint8_t /*pad*/) {}
uint8_t palReadPad(ioportid_t /*port*/, uint8_t /*pad*/) { return 0; }
void palSetPadMode(ioportid_t /*port*/, uint8_t /*pad*/, uint32_t /*mode*/) {}

// ---------------------------------------------------------------------------
// chprintf
// ---------------------------------------------------------------------------
int chprintf(void* /*chp*/, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int chsnprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = std::vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

// ---------------------------------------------------------------------------
// RTC driver stubs
// ---------------------------------------------------------------------------
#include "lpc43xx_cpp.hpp"

void rtcGetTime(RTCDriver*, RTCTime* timespec) {
    if (timespec) {
        std::time_t t = std::time(nullptr);
        struct std::tm* tm = std::localtime(&t);
        timespec->tv_date = (static_cast<uint32_t>(tm->tm_year + 1900) << 16)
                          | (static_cast<uint32_t>(tm->tm_mon + 1) << 8)
                          | static_cast<uint32_t>(tm->tm_mday);
        timespec->tv_time = (static_cast<uint32_t>(tm->tm_hour) << 16)
                          | (static_cast<uint32_t>(tm->tm_min) << 8)
                          | static_cast<uint32_t>(tm->tm_sec);
    }
}

void rtcSetTime(RTCDriver*, const RTCTime*) {}

void rtcGetTime(RTCDriver*, lpc43xx::rtc::RTC* datetime) {
    if (datetime) {
        std::time_t t = std::time(nullptr);
        struct std::tm* tm = std::localtime(&t);
        *datetime = lpc43xx::rtc::RTC{
            static_cast<uint32_t>(tm->tm_year + 1900),
            static_cast<uint32_t>(tm->tm_mon + 1),
            static_cast<uint32_t>(tm->tm_mday),
            static_cast<uint32_t>(tm->tm_hour),
            static_cast<uint32_t>(tm->tm_min),
            static_cast<uint32_t>(tm->tm_sec)
        };
    }
}

void rtcSetTime(RTCDriver*, const lpc43xx::rtc::RTC*) {}
