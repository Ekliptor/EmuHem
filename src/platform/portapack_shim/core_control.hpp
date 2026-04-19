// EmuHem core_control.hpp shim
// Stubs for M4 core control (processor loading/reset).

#ifndef __CORE_CONTROL_H__
#define __CORE_CONTROL_H__

#include "memory_map.hpp"
#include "spi_image.hpp"

void m4_init(const portapack::spi_flash::image_tag_t image_tag,
             const portapack::memory::region_t to,
             const bool full_reset);
void m4_init_prepared(const uint32_t m4_code, const bool full_reset);
void m4_request_shutdown();
void m0_halt();

#endif // __CORE_CONTROL_H__
