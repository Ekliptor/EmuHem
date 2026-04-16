// EmuHem platform detection stub

#include "platform_detect.h"

static board_id_t g_board_id = BOARD_ID_HACKRF1_OG;
static board_rev_t g_board_rev = BOARD_REV_HACKRF1_R6;

void detect_hardware_platform(void) {
    g_board_id = BOARD_ID_HACKRF1_OG;
    g_board_rev = BOARD_REV_HACKRF1_R6;
}

board_id_t detected_platform(void) {
    return g_board_id;
}

board_rev_t detected_revision(void) {
    return g_board_rev;
}

uint32_t supported_platform(void) {
    return PLATFORM_HACKRF1_OG;
}
