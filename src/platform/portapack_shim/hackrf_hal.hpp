// EmuHem hackrf_hal.hpp shim
// Provides clock frequency constants and I2C address needed by portapack.cpp

#ifndef __HACKRF_HAL_H__
#define __HACKRF_HAL_H__

#include <cstdint>
#include <cstddef>

namespace hackrf {
namespace one {

using ClockFrequency = uint32_t;

constexpr ClockFrequency si5351_xtal_f = 25000000U;
constexpr ClockFrequency si5351_clkin_f = 10000000U;
constexpr ClockFrequency base_m4_clk_f = 200000000U;
constexpr ClockFrequency base_m0_clk_f = 200000000U;
constexpr ClockFrequency ssp1_pclk_f = 200000000U;
constexpr ClockFrequency max5864_spi_f = 20000000U;
constexpr ClockFrequency max283x_spi_f = 20000000U;
constexpr ClockFrequency rffc5072_reference_f = 50000000U;
constexpr ClockFrequency max283x_reference_f = 40000000U;
constexpr ClockFrequency mcu_clkin_og_f = 40000000U;
constexpr ClockFrequency mcu_clkin_r9_f = 10000000U;

// Si5351 clock output indices (OG variant)
constexpr size_t clock_generator_output_og_codec = 0;
constexpr size_t clock_generator_output_og_cpld = 1;
constexpr size_t clock_generator_output_og_sgpio = 2;
constexpr size_t clock_generator_output_og_clkout = 3;
constexpr size_t clock_generator_output_og_first_if = 4;
constexpr size_t clock_generator_output_og_second_if = 5;
constexpr size_t clock_generator_output_og_mcu_clkin = 7;

// Si5351 clock output indices (R9 variant)
constexpr size_t clock_generator_output_r9_if = 0;
constexpr size_t clock_generator_output_r9_sgpio = 1;
constexpr size_t clock_generator_output_r9_clkout = 2;
constexpr size_t clock_generator_output_r9_mcu_clkin = 2;

constexpr uint8_t si5351_i2c_address = 0x60;

} // namespace one
} // namespace hackrf

// ADC stub types
namespace adc {
template <uint32_t BASE_ADDR>
struct ADC {
    static void clock_enable() {}
    static void clock_disable() {}
};
} // namespace adc

#define LPC_ADC0_BASE 0
#define LPC_ADC1_BASE 1

using adc0 = adc::ADC<LPC_ADC0_BASE>;
using adc1 = adc::ADC<LPC_ADC1_BASE>;

#endif // __HACKRF_HAL_H__
