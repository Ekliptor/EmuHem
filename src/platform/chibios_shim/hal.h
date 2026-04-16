// EmuHem HAL Shim -- Stubs for ChibiOS HAL layer
// Provides LPC43xx peripheral register stubs and HAL driver types.

#ifndef _HAL_H_
#define _HAL_H_

#include "ch.h"
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// LPC43xx GPIO peripheral stub
// ---------------------------------------------------------------------------
struct LPC_GPIO_T {
    uint8_t DIR[8]{};
    uint8_t _pad1[0x80 - 8]{};
    uint32_t MASK[8]{};
    uint32_t PIN[8]{};
    uint32_t MPIN[8]{};
    uint32_t SET[8]{};
    uint32_t CLR[8]{};
    uint32_t NOT[8]{};
};

// ---------------------------------------------------------------------------
// LPC43xx CGU (Clock Generation Unit) stub
// ---------------------------------------------------------------------------
struct LPC_CGU_T {
    uint32_t FREQ_MON{0};
    uint32_t XTAL_OSC_CTRL{0};
    uint32_t PLL0USB_STAT{0};
    uint32_t PLL0USB_CTRL{0};
    uint32_t PLL0USB_MDIV{0};
    uint32_t PLL0USB_NP_DIV{0};
    uint32_t PLL0AUDIO_STAT{0};
    uint32_t PLL0AUDIO_CTRL{0};
    uint32_t PLL0AUDIO_MDIV{0};
    uint32_t PLL0AUDIO_NP_DIV{0};
    uint32_t PLL0AUDIO_FRAC{0};
    uint32_t PLL1_STAT{0};
    uint32_t PLL1_CTRL{0};
    uint32_t IDIVA_CTRL{0};
    uint32_t IDIVB_CTRL{0};
    uint32_t IDIVC_CTRL{0};
    uint32_t IDIVD_CTRL{0};
    uint32_t IDIVE_CTRL{0};
    uint32_t BASE_SAFE_CLK{0};
    uint32_t BASE_USB0_CLK{0};
    uint32_t BASE_PERIPH_CLK{0};
    uint32_t BASE_USB1_CLK{0};
    uint32_t BASE_M4_CLK{0};
    uint32_t BASE_SPIFI_CLK{0};
    uint32_t BASE_SPI_CLK{0};
    uint32_t BASE_PHY_RX_CLK{0};
    uint32_t BASE_PHY_TX_CLK{0};
    uint32_t BASE_APB1_CLK{0};
    uint32_t BASE_APB3_CLK{0};
    uint32_t BASE_LCD_CLK{0};
    uint32_t BASE_VADC_CLK{0};
    uint32_t BASE_SDIO_CLK{0};
    uint32_t BASE_SSP0_CLK{0};
    uint32_t BASE_SSP1_CLK{0};
    uint32_t BASE_UART0_CLK{0};
    uint32_t BASE_UART1_CLK{0};
    uint32_t BASE_UART2_CLK{0};
    uint32_t BASE_UART3_CLK{0};
    uint32_t BASE_OUT_CLK{0};
    uint32_t BASE_AUDIO_CLK{0};
    uint32_t BASE_CGU_OUT0_CLK{0};
    uint32_t BASE_CGU_OUT1_CLK{0};
};

// ---------------------------------------------------------------------------
// LPC43xx CREG stub
// ---------------------------------------------------------------------------
struct LPC_CREG_T {
    uint32_t _pad[64]{};
    uint32_t M4MEMMAP{0};
    uint32_t _pad2[5]{};
    uint32_t CREG5{0};
    uint32_t DMAMUX{0};
    uint32_t _pad3[2]{};
    uint32_t CREG6{0};
    uint32_t M4TXEVENT{0};
    uint32_t _pad4[48]{};
    uint32_t M0APPTXEVENT{0};
    uint32_t M0SUBMEMMAP{0};
    uint32_t M0SUBTXEVENT{0};
};

// ---------------------------------------------------------------------------
// LPC43xx RGU (Reset Generation Unit) stub
// ---------------------------------------------------------------------------
struct LPC_RGU_T {
    uint32_t RESET_CTRL[2]{};
    uint32_t _pad[2]{};
    uint32_t RESET_STATUS[4]{};
    uint32_t RESET_ACTIVE_STATUS[2]{};
};

// ---------------------------------------------------------------------------
// LPC43xx SGPIO stub
// ---------------------------------------------------------------------------
struct LPC_SGPIO_T {
    uint32_t OUT_MUX_CFG[16]{};
    uint32_t SGPIO_MUX_CFG[16]{};
    uint32_t SLICE_MUX_CFG[16]{};
    uint32_t REG[16]{};
    uint32_t REG_SS[16]{};
    uint32_t PRESET[16]{};
    uint32_t COUNT[16]{};
    uint32_t POS[16]{};
    uint32_t MASK_A{0};
    uint32_t MASK_H{0};
    uint32_t MASK_I{0};
    uint32_t MASK_D{0};
    uint32_t MASK_P{0};
    uint32_t GPIO_INREG{0};
    uint32_t GPIO_OUTREG{0};
    uint32_t GPIO_OENREG{0};
    uint32_t CTRL_ENABLE{0};
    uint32_t CTRL_DISABLE{0};
    uint32_t _pad[8]{};
    uint32_t CLR_EN_0{0};
    uint32_t SET_EN_0{0};
    uint32_t ENABLE_0{0};
    uint32_t STATUS_0{0};
    uint32_t CTR_STATUS_0{0};
    uint32_t SET_STATUS_0{0};
    uint32_t CLR_EN_1{0};
    uint32_t SET_EN_1{0};
    uint32_t ENABLE_1{0};
    uint32_t STATUS_1{0};
    uint32_t CTR_STATUS_1{0};
    uint32_t SET_STATUS_1{0};
    uint32_t CLR_EN_2{0};
    uint32_t SET_EN_2{0};
    uint32_t ENABLE_2{0};
    uint32_t STATUS_2{0};
    uint32_t CTR_STATUS_2{0};
    uint32_t SET_STATUS_2{0};
    uint32_t CLR_EN_3{0};
    uint32_t SET_EN_3{0};
    uint32_t ENABLE_3{0};
    uint32_t STATUS_3{0};
    uint32_t CTR_STATUS_3{0};
    uint32_t SET_STATUS_3{0};
};

// ---------------------------------------------------------------------------
// LPC43xx GPDMA stub
// ---------------------------------------------------------------------------
struct LPC_GPDMA_T {
    uint32_t INTSTAT{0};
    uint32_t INTTCSTAT{0};
    uint32_t INTTCCLEAR{0};
    uint32_t INTERRSTAT{0};
    uint32_t INTERRCLR{0};
    uint32_t RAWINTTCSTAT{0};
    uint32_t RAWINTERRSTAT{0};
    uint32_t ENBLDCHNS{0};
    uint32_t SOFTBREQ{0};
    uint32_t SOFTSREQ{0};
    uint32_t SOFTLBREQ{0};
    uint32_t SOFTLSREQ{0};
    uint32_t CONFIG{0};
    uint32_t SYNC{0};
};

// ---------------------------------------------------------------------------
// LPC43xx SCU (System Control Unit) stub
// ---------------------------------------------------------------------------
struct LPC_SCU_T {
    uint32_t SFSP[16][32]{};
    uint32_t _pad[256]{};
    uint32_t SFSCLK[4]{};
    uint32_t _pad2[28]{};
    uint32_t EMCDELAYCLK{0};
    uint32_t _pad3[63]{};
    uint32_t PINTSEL[2]{};
};

// ---------------------------------------------------------------------------
// LPC43xx SSP stub (SPI peripheral)
// ---------------------------------------------------------------------------
struct LPC_SSP_T {
    uint32_t CR0{0};
    uint32_t CR1{0};
    uint32_t DR{0};
    uint32_t SR{0};
    uint32_t CPSR{0};
    uint32_t IMSC{0};
    uint32_t RIS{0};
    uint32_t MIS{0};
    uint32_t ICR{0};
    uint32_t DMACR{0};
};

// ---------------------------------------------------------------------------
// LPC43xx Timer stub
// ---------------------------------------------------------------------------
struct LPC_TIMER_T {
    uint32_t IR{0};
    uint32_t TCR{0};
    uint32_t TC{0};
    uint32_t PR{0};
    uint32_t PC{0};
    uint32_t MCR{0};
    uint32_t MR[4]{};
    uint32_t CCR{0};
    uint32_t CR[4]{};
    uint32_t EMR{0};
    uint32_t _pad[12]{};
    uint32_t CTCR{0};
};

// ---------------------------------------------------------------------------
// LPC43xx ADC stub
// ---------------------------------------------------------------------------
struct LPC_ADC_T {
    uint32_t CR{0};
    uint32_t GDR{0};
    uint32_t _pad{0};
    uint32_t INTEN{0};
    uint32_t DR[8]{};
    uint32_t STAT{0};
};

// ---------------------------------------------------------------------------
// LPC43xx RTC stub
// ---------------------------------------------------------------------------
struct LPC_RTC_T {
    uint32_t ILR{0};
    uint32_t _pad1{0};
    uint32_t CCR{0};
    uint32_t CIIR{0};
    uint32_t AMR{0};
    uint32_t CTIME0{0};
    uint32_t CTIME1{0};
    uint32_t CTIME2{0};
    uint32_t SEC{0};
    uint32_t MIN{0};
    uint32_t HOUR{0};
    uint32_t DOM{0};
    uint32_t DOW{0};
    uint32_t DOY{0};
    uint32_t MONTH{0};
    uint32_t YEAR{0};
    uint32_t CALIBRATION{0};
    uint32_t GPREG[5]{};
    uint32_t RTC_AUX{0};
    uint32_t RTC_AUXEN{0};
    uint32_t ASEC{0};
    uint32_t AMIN{0};
    uint32_t AHOUR{0};
    uint32_t ADOM{0};
    uint32_t ADOW{0};
    uint32_t ADOY{0};
    uint32_t AMON{0};
    uint32_t AYEAR{0};
};

// ---------------------------------------------------------------------------
// LPC43xx WWDT stub
// ---------------------------------------------------------------------------
struct LPC_WWDT_T {
    uint32_t MOD{0};
    uint32_t TC{0};
    uint32_t FEED{0};
    uint32_t TV{0};
    uint32_t _pad{0};
    uint32_t WARNINT{0};
    uint32_t WINDOW{0};
};

// ---------------------------------------------------------------------------
// Global peripheral instances
// ---------------------------------------------------------------------------
extern LPC_GPIO_T lpc_gpio;
extern LPC_CGU_T lpc_cgu;
extern LPC_CREG_T lpc_creg;
extern LPC_RGU_T lpc_rgu;
extern LPC_SGPIO_T lpc_sgpio;
extern LPC_GPDMA_T lpc_gpdma;
extern LPC_SCU_T lpc_scu;
extern LPC_SSP_T lpc_ssp0;
extern LPC_SSP_T lpc_ssp1;
extern LPC_TIMER_T lpc_timer0;
extern LPC_TIMER_T lpc_timer1;
extern LPC_TIMER_T lpc_timer2;
extern LPC_TIMER_T lpc_timer3;
extern LPC_ADC_T lpc_adc0;
extern LPC_ADC_T lpc_adc1;
extern LPC_RTC_T lpc_rtc;
extern LPC_WWDT_T lpc_wwdt;

#define LPC_GPIO    (&lpc_gpio)
#define LPC_CGU     (&lpc_cgu)
#define LPC_CREG    (&lpc_creg)
#define LPC_RGU     (&lpc_rgu)
#define LPC_SGPIO   (&lpc_sgpio)
#define LPC_GPDMA   (&lpc_gpdma)
#define LPC_SCU     (&lpc_scu)
#define LPC_SSP0    (&lpc_ssp0)
#define LPC_SSP1    (&lpc_ssp1)
#define LPC_TIMER0  (&lpc_timer0)
#define LPC_TIMER1  (&lpc_timer1)
#define LPC_TIMER2  (&lpc_timer2)
#define LPC_TIMER3  (&lpc_timer3)
#define LPC_ADC0    (&lpc_adc0)
#define LPC_ADC1    (&lpc_adc1)
#define LPC_RTC     (&lpc_rtc)
#define LPC_WWDT    (&lpc_wwdt)

// ---------------------------------------------------------------------------
// HAL system clock (no-op in emulation)
// ---------------------------------------------------------------------------
static inline void halLPCSetSystemClock(void) {}
static inline void halInit(void) {}

#ifdef __cplusplus
}
#endif

#endif /* _HAL_H_ */
