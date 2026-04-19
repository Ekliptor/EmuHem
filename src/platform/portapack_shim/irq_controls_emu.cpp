// EmuHem irq_controls emulation
// Provides button/encoder/touch input from SDL keyboard events.

#include "irq_controls.hpp"
#include <atomic>

static std::atomic<uint8_t> emu_switches_raw{0};
static std::atomic<uint32_t> emu_encoder_position{0};

// Called from SDL event thread
void emu_set_switches(uint8_t mask) {
    emu_switches_raw.store(mask, std::memory_order_relaxed);
}

void emu_adjust_encoder(int delta) {
    emu_encoder_position.fetch_add(delta, std::memory_order_relaxed);
}

// Firmware API implementations (signatures must match irq_controls.hpp exactly)
void controls_init() {}

SwitchesState get_switches_state() {
    SwitchesState s{};
    uint8_t raw = emu_switches_raw.load(std::memory_order_relaxed);
    for (int i = 0; i < 6 && i < static_cast<int>(s.size()); i++) {
        s[i] = (raw >> i) & 1;
    }
    return s;
}

EncoderPosition get_encoder_position() {
    return static_cast<EncoderPosition>(emu_encoder_position.load(std::memory_order_relaxed));
}

touch::Frame get_touch_frame() {
    return {};
}

bool switch_is_long_pressed(Switch) {
    return false;
}

uint8_t swizzled_switches() {
    return emu_switches_raw.load(std::memory_order_relaxed);
}

SwitchesState get_switches_repeat_config() { return {}; }
void set_switches_repeat_config(SwitchesState) {}

SwitchesState get_switches_long_press_config() { return {}; }
void set_switches_long_press_config(SwitchesState) {}

namespace control {
namespace debug {
uint8_t switches() { return 0; }
void inject_switch(uint8_t) {}
} // namespace debug
} // namespace control
