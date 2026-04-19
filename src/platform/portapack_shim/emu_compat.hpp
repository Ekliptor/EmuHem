// EmuHem compatibility header
// Force-included before all translation units to fix 64-bit / Clang issues
// in firmware headers that can't be modified.

#ifndef __EMU_COMPAT_H__
#define __EMU_COMPAT_H__

// Fix: firmware's constexpr pow() collides with std::pow from <cmath> on desktop.
// The firmware defines pow() in utility.hpp as a constexpr template, but <cmath>
// provides a non-constexpr std::pow that wins overload resolution.
// We ensure the firmware's pow is used for integer args by providing a wrapper.
#include <cmath>

namespace emu_compat {
template<typename T>
inline constexpr T pow(const T base, unsigned const exponent) {
    return (exponent == 0) ? 1 : (base * pow(base, exponent - 1));
}
}

// Override the global pow for integer arguments so constexpr contexts work
using emu_compat::pow;

#endif // __EMU_COMPAT_H__
