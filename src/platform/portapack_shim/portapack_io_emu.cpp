// EmuHem portapack::IO implementation -- framebuffer-backed LCD emulation
// Intercepts ILI9341 commands and writes RGB565 pixels to a memory framebuffer.

#include "portapack_io.hpp"

#include <cstdio>
#include <cstring>

namespace portapack {

DeviceType device_type = DEV_PORTAPACK;

uint16_t IO::framebuffer_[FB_WIDTH * FB_HEIGHT] = {};
std::atomic<bool> IO::fb_dirty_{false};

uint16_t* IO::get_framebuffer() { return framebuffer_; }
bool IO::is_dirty() { return fb_dirty_.load(std::memory_order_relaxed); }
void IO::clear_dirty() { fb_dirty_.store(false, std::memory_order_relaxed); }

void IO::init() {
    std::memset(framebuffer_, 0, sizeof(framebuffer_));
    fb_dirty_.store(true, std::memory_order_relaxed);
}

void IO::lcd_backlight(bool) {}
void IO::lcd_reset_state(bool) {}
void IO::audio_reset_state(bool) {}
void IO::reference_oscillator(bool) {}

void IO::lcd_command(uint8_t cmd) {
    current_command_ = cmd;
    cmd_data_index_ = 0;

    switch (cmd) {
        case 0x2C: // RAMWR -- Memory Write
            lcd_state_ = LcdState::RAMWR;
            write_x_ = col_start_;
            write_y_ = page_start_;
            break;
        case 0x2E: // RAMRD -- Memory Read
            lcd_state_ = LcdState::RAMRD;
            write_x_ = col_start_;
            write_y_ = page_start_;
            break;
        case 0x2A: // CASET -- Column Address Set (expects 4 data bytes)
        case 0x2B: // PASET -- Page Address Set (expects 4 data bytes)
            lcd_state_ = LcdState::IDLE;
            break;
        default:
            lcd_state_ = LcdState::IDLE;
            break;
    }
}

void IO::lcd_write_data(uint32_t value) {
    uint16_t v = static_cast<uint16_t>(value & 0xFFFF);

    if (lcd_state_ == LcdState::RAMWR) {
        write_pixel_to_fb(v);
        advance_cursor();
        return;
    }

    // Handle CASET/PASET data bytes
    if (current_command_ == 0x2A || current_command_ == 0x2B) {
        // Data arrives as 16-bit words: start_hi|start_lo, end_hi|end_lo
        // But the firmware calls lcd_write_data with 8-bit values via
        // lcd_data_write_command_and_data which sends individual bytes.
        // However, lcd_ili9341.cpp uses lcd_start_ram_write which calls
        // io.lcd_data_write_command_and_data(0x2A, {sh, sl, eh, el})
        // where each element is a byte written via lcd_write_data().
        // So we collect 4 bytes.
        if (cmd_data_index_ < 4) {
            cmd_data_buf_[cmd_data_index_++] = static_cast<uint8_t>(v & 0xFF);
        }
        if (cmd_data_index_ == 4) {
            uint16_t start = (static_cast<uint16_t>(cmd_data_buf_[0]) << 8) | cmd_data_buf_[1];
            uint16_t end = (static_cast<uint16_t>(cmd_data_buf_[2]) << 8) | cmd_data_buf_[3];
            if (current_command_ == 0x2A) {
                col_start_ = start;
                col_end_ = end;
            } else {
                page_start_ = start;
                page_end_ = end;
            }
        }
    }
}

uint32_t IO::lcd_read_data() {
    if (lcd_state_ == LcdState::RAMRD) {
        if (write_y_ < FB_HEIGHT && write_x_ < FB_WIDTH) {
            uint16_t pixel = framebuffer_[write_y_ * FB_WIDTH + write_x_];
            advance_cursor();
            return pixel;
        }
    }
    return 0;
}

void IO::write_pixel_to_fb(uint16_t color) {
    if (write_x_ < FB_WIDTH && write_y_ < FB_HEIGHT) {
        framebuffer_[write_y_ * FB_WIDTH + write_x_] = color;
        fb_dirty_.store(true, std::memory_order_relaxed);
    }
}

void IO::advance_cursor() {
    write_x_++;
    if (write_x_ > col_end_) {
        write_x_ = col_start_;
        write_y_++;
        if (write_y_ > page_end_) {
            write_y_ = page_start_;
        }
    }
}

// -- Public interface methods that delegate to the private protocol --

void IO::lcd_data_write_command_and_data(
    uint_fast8_t command,
    const uint8_t* data,
    size_t data_count) {
    lcd_command(command);
    for (size_t i = 0; i < data_count; i++) {
        lcd_write_data(data[i]);
    }
}

void IO::lcd_data_write_command_and_data(
    uint_fast8_t command,
    const std::initializer_list<uint8_t>& data) {
    lcd_command(command);
    for (const auto d : data) {
        lcd_write_data(d);
    }
}

void IO::lcd_data_read_command_and_data(
    uint_fast8_t command,
    uint16_t* data,
    size_t data_count) {
    lcd_command(command);
    for (size_t i = 0; i < data_count; i++) {
        data[i] = static_cast<uint16_t>(lcd_read_data());
    }
}

uint32_t IO::lcd_read_data_raw() {
    return lcd_read_data();
}

void IO::lcd_write_word(uint32_t w) {
    lcd_write_data(w);
}

void IO::lcd_write_words(const uint16_t* w, size_t n) {
    for (size_t i = 0; i < n; i++) {
        lcd_write_data(w[i]);
    }
}

void IO::lcd_write_pixel(ui::Color pixel) {
    uint16_t v = pixel.v;
    if (dark_cover_enabled) {
        v = DARKENED_PIXEL(v, brightness);
    }
    lcd_write_data(v);
}

uint32_t IO::lcd_read_word() {
    return lcd_read_data();
}

void IO::lcd_write_pixels(ui::Color pixel, size_t n) {
    uint16_t v = pixel.v;
    if (dark_cover_enabled) {
        v = DARKENED_PIXEL(v, brightness);
    }
    while (n--) {
        lcd_write_data(v);
    }
}

void IO::lcd_write_pixels_unrolled8(ui::Color pixel, size_t n) {
    uint16_t v = pixel.v;
    if (dark_cover_enabled) {
        v = DARKENED_PIXEL(v, brightness);
    }
    n >>= 3;
    while (n--) {
        lcd_write_data(v);
        lcd_write_data(v);
        lcd_write_data(v);
        lcd_write_data(v);
        lcd_write_data(v);
        lcd_write_data(v);
        lcd_write_data(v);
        lcd_write_data(v);
    }
}

void IO::lcd_write_pixels(const ui::Color* pixels, size_t n) {
    for (size_t i = 0; i < n; i++) {
        lcd_write_pixel(pixels[i]);
    }
}

void IO::lcd_read_bytes(uint8_t* byte, size_t byte_count) {
    for (size_t i = 0; i < byte_count; i++) {
        uint32_t val = lcd_read_data();
        byte[i] = static_cast<uint8_t>(val & 0xFF);
    }
}

uint32_t IO::io_read() {
    // Returns switch state. Bits set by SDL input thread.
    // TODO: Wire to emu_switches atomic in irq_controls_emu.cpp
    return 0;
}

uint32_t IO::io_update(TouchPinsConfig) {
    return 0;
}

uint32_t IO::lcd_te() { return 0; }
uint32_t IO::dfu_read() { return 0; }

void IO::update_cached_values() {
    // In the real firmware this reads persistent memory settings.
    // For now use defaults.
}

bool IO::get_is_normally_black() { return lcd_normally_black; }
bool IO::get_dark_cover() { return dark_cover_enabled; }
uint8_t IO::get_brightness() { return brightness; }

} // namespace portapack
