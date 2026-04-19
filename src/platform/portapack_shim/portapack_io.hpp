// EmuHem portapack::IO shim -- framebuffer-backed LCD emulation
// Replaces firmware/common/portapack_io.hpp.
// Intercepts ILI9341 commands (CASET/PASET/RAMWR/RAMRD) and writes
// RGB565 pixels to a uint16_t[240*320] framebuffer instead of GPIO.

#ifndef __PORTAPACK_IO_H__
#define __PORTAPACK_IO_H__

#include <cstdint>
#include <cstddef>
#include <array>
#include <atomic>
#include <initializer_list>

#include "platform.hpp"
#include "ui.hpp"

// Darkened pixel bit mask (copied from original portapack_io.hpp)
static const uint16_t darken_mask[4] = {
    0b1111111111111111,
    0b0111101111101111,
    0b0011100111100111,
    0b0001100011100011
};

#define DARKENED_PIXEL(pixel, shift) ((pixel >> shift) & darken_mask[shift])
#define UNDARKENED_PIXEL(pixel, shift) (pixel << shift)

namespace portapack {

enum DeviceType {
    DEV_PORTAPACK,
    DEV_PORTARF
};
extern DeviceType device_type;

class IO {
public:
    enum class TouchPinsConfig : uint8_t {
        XN_BIT = (1 << 0),
        XP_BIT = (1 << 1),
        YN_BIT = (1 << 2),
        YP_BIT = (1 << 3),
        XN_OE = (1 << 4),
        XP_OE = (1 << 5),
        YN_OE = (1 << 6),
        YP_OE = (1 << 7),
        XN_IN = (1 << 0),
        XN_OUT_1 = (1 << 4) | (1 << 0),
        XN_OUT_0 = (1 << 4),
        XP_IN = (1 << 1),
        XP_OUT_1 = (1 << 5) | (1 << 1),
        XP_OUT_0 = (1 << 5),
        YN_IN = (1 << 2),
        YN_OUT_1 = (1 << 6) | (1 << 2),
        YN_OUT_0 = (1 << 6),
        YP_IN = (1 << 3),
        YP_OUT_1 = (1 << 7) | (1 << 3),
        YP_OUT_0 = (1 << 7),
        Float = (1 << 1) | (1 << 0) | (1 << 3) | (1 << 2),
        WaitTouch = (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2),
        SensePressure = (1 << 1) | (1 << 4) | (1 << 7) | (1 << 3) | (1 << 2),
        SenseX = (1 << 5) | (1 << 1) | (1 << 4) | (1 << 3) | (1 << 2),
        SenseY = (1 << 1) | (1 << 0) | (1 << 7) | (1 << 3) | (1 << 6),
    };

    IO() = default;

    // Original constructor signature (ignored in emulator, but needed for compatibility)
    template <typename... Args>
    constexpr IO(Args&&...) {}

    void init();

    void lcd_backlight(bool value);
    void lcd_reset_state(bool active);
    void audio_reset_state(bool active);
    void reference_oscillator(bool enable);

    void lcd_data_write_command_and_data(
        uint_fast8_t command,
        const uint8_t* data,
        size_t data_count);

    void lcd_data_write_command_and_data(
        uint_fast8_t command,
        const std::initializer_list<uint8_t>& data);

    void lcd_data_read_command_and_data(
        uint_fast8_t command,
        uint16_t* data,
        size_t data_count);

    uint32_t lcd_read_data_raw();

    void lcd_write_word(uint32_t w);
    void lcd_write_words(const uint16_t* w, size_t n);
    void lcd_write_pixel(ui::Color pixel);
    uint32_t lcd_read_word();
    void lcd_write_pixels(ui::Color pixel, size_t n);
    void lcd_write_pixels_unrolled8(ui::Color pixel, size_t n);
    void lcd_write_pixels(const ui::Color* pixels, size_t n);
    void lcd_read_bytes(uint8_t* byte, size_t byte_count);

    uint32_t io_read();
    uint32_t io_update(TouchPinsConfig write_value);
    uint32_t lcd_te();
    uint32_t dfu_read();

    void update_cached_values();
    bool get_is_normally_black();
    bool get_dark_cover();
    uint8_t get_brightness();

    bool lcd_normally_black = false;
    bool dark_cover_enabled = false;
    uint8_t brightness = 0;

    // Emulator-specific framebuffer access
    static uint16_t* get_framebuffer();
    static bool is_dirty();
    static void clear_dirty();

private:
    // ILI9341 state machine
    enum class LcdState { IDLE, RAMWR, RAMRD };

    LcdState lcd_state_ = LcdState::IDLE;
    uint8_t current_command_ = 0;
    uint8_t cmd_data_index_ = 0;
    uint8_t cmd_data_buf_[4] = {};

    uint16_t col_start_ = 0;
    uint16_t col_end_ = 239;
    uint16_t page_start_ = 0;
    uint16_t page_end_ = 319;
    uint16_t write_x_ = 0;
    uint16_t write_y_ = 0;

    static constexpr int FB_WIDTH = 240;
    static constexpr int FB_HEIGHT = 320;
    static uint16_t framebuffer_[FB_WIDTH * FB_HEIGHT];
    static std::atomic<bool> fb_dirty_;

    void lcd_command(uint8_t cmd);
    void lcd_write_data(uint32_t value);
    uint32_t lcd_read_data();
    void write_pixel_to_fb(uint16_t color);
    void advance_cursor();
};

} // namespace portapack

#endif // __PORTAPACK_IO_H__
