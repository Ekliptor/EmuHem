// EmuHem memory_map.hpp shim
// Provides region_t type and memory map constants.
// On real hardware these are fixed addresses; in the emulator they're
// arbitrary values used only for struct sizing and identification.

#ifndef __MEMORY_MAP_H__
#define __MEMORY_MAP_H__

#include <cstddef>
#include <cstdint>

#include "utility.hpp"

// Constants that the real firmware gets from LPC43xx headers
#ifndef LPC_BACKUP_REG_BASE
#define LPC_BACKUP_REG_BASE 0x40041000
#endif
#ifndef LPC_SPIFI_DATA_BASE
#define LPC_SPIFI_DATA_BASE 0x14000000
#endif
#ifndef LPC_SPIFI_DATA_CACHED_BASE
#define LPC_SPIFI_DATA_CACHED_BASE 0x80000000
#endif
#ifndef FLASH_SIZE_MB
#define FLASH_SIZE_MB 1
#endif

namespace portapack {
namespace memory {

struct region_t {
    constexpr region_t(const uint32_t base, const size_t size)
        : base_{base}, size_{size} {}

    constexpr uint32_t base() const { return base_; }
    constexpr uint32_t end() const { return base_ + size_; }
    constexpr size_t size() const { return size_; }

private:
    const uint32_t base_;
    const size_t size_;
};

namespace map {

constexpr region_t local_sram_0{0x10000000, 96_KiB};
constexpr region_t local_sram_1{0x10080000, 40_KiB};

constexpr region_t ahb_ram_0{0x20000000, 32_KiB};
constexpr region_t ahb_ram_1{0x20008000, 16_KiB};
constexpr region_t ahb_ram_2{0x2000c000, 16_KiB};

constexpr region_t backup_ram{LPC_BACKUP_REG_BASE, 256};

constexpr region_t spifi_uncached{LPC_SPIFI_DATA_BASE, FLASH_SIZE_MB * 1024 * 1024};
constexpr region_t spifi_cached{LPC_SPIFI_DATA_CACHED_BASE, spifi_uncached.size()};

constexpr region_t m4_code{local_sram_1.base(), 32_KiB};
constexpr region_t shared_memory{m4_code.end(), 8_KiB};

constexpr region_t m4_code_hackrf = local_sram_0;

} // namespace map
} // namespace memory
} // namespace portapack

#endif // __MEMORY_MAP_H__
