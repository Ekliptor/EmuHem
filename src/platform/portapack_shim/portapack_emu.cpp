// EmuHem portapack global object definitions
// Provides all extern objects declared in portapack.hpp.

#include "portapack.hpp"
#include "portapack_persistent_memory.hpp"
#include "hackrf_hal.hpp"

#include <cstdio>

namespace portapack {

const char* init_error = nullptr;

IO io{};
lcd::ILI9341 display{};

I2C i2c0{&I2CD0};
SPI ssp1{&SPID2};

USBSerial usb_serial{};

si5351::Si5351 clock_generator{i2c0, hackrf::one::si5351_i2c_address};
ClockManager clock_manager{i2c0, clock_generator};

ReceiverModel receiver_model{};
TransmitterModel transmitter_model{};

uint32_t bl_tick_counter{0};
bool antenna_bias{false};
uint16_t touch_threshold{0};

TemperatureLogger temperature_logger{};

bool async_tx_enabled{false};

static portapack::BacklightOnOff backlight_instance;

void set_antenna_bias(const bool v) { antenna_bias = v; }
bool get_antenna_bias() { return antenna_bias; }

Backlight* backlight() { return &backlight_instance; }

void setEventDispatcherToUSBSerial(EventDispatcher*) {}

init_status_t init() {
    std::fprintf(stdout, "[EmuHem] portapack::init()\n");

    io.init();
    display.init();

    persistent_memory::cache::init();

    return init_status_t::INIT_SUCCESS;
}

void shutdown(const bool) {
    std::fprintf(stdout, "[EmuHem] portapack::shutdown()\n");
}

} // namespace portapack
