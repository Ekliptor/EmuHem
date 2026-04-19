// EmuHem HAL peripheral instances
// Global stub structs for LPC43xx peripherals.

#include "hal.h"

LPC_GPIO_T lpc_gpio{};
LPC_CGU_T lpc_cgu{};
LPC_CREG_T lpc_creg{};
LPC_RGU_T lpc_rgu{};
LPC_SGPIO_T lpc_sgpio{};
LPC_GPDMA_T lpc_gpdma{};
LPC_SCU_T lpc_scu{};
LPC_SSP_T lpc_ssp0{};
LPC_SSP_T lpc_ssp1{};
LPC_TIMER_T lpc_timer0{};
LPC_TIMER_T lpc_timer1{};
LPC_TIMER_T lpc_timer2{};
LPC_TIMER_T lpc_timer3{};
LPC_ADC_T lpc_adc0{};
LPC_ADC_T lpc_adc1{};
LPC_RTC_T lpc_rtc{};
LPC_WWDT_T lpc_wwdt{};
LPC_I2S_T lpc_i2s0{};

// ChibiOS driver instances
RTCDriver RTCD1{};
