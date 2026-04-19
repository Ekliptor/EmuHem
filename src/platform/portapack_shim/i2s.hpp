// EmuHem I2S shim
// Replaces firmware/common/i2s.hpp which has heavy LPC43xx hardware dependencies.
// Only provides the tx_mute() call used by baseband_thread.cpp.

#ifndef __I2S_H__
#define __I2S_H__

#include <cstdint>

namespace lpc43xx {
namespace i2s {

class i2s0 {
   public:
    static void configure(...) {}
    static void shutdown() {}
    static void rx_start() {}
    static void rx_stop() {}
    static void tx_start() {}
    static void tx_stop() {}
    static void tx_mute() {}
    static void tx_unmute() {}
};

using i2s1 = i2s0;

}  // namespace i2s
}  // namespace lpc43xx

#endif  // __I2S_H__
