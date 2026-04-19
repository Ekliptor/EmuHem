// EmuHem rtc_time stubs
// Provides real-time clock from host system.

#include "rtc_time.hpp"

#include <ctime>

using namespace lpc43xx;

namespace rtc_time {

Signal<> signal_tick_second{};

static rtc::RTC host_time_to_rtc() {
    std::time_t t = std::time(nullptr);
    struct std::tm* tm = std::localtime(&t);
    return rtc::RTC{
        static_cast<uint32_t>(tm->tm_year + 1900),
        static_cast<uint32_t>(tm->tm_mon + 1),
        static_cast<uint32_t>(tm->tm_mday),
        static_cast<uint32_t>(tm->tm_hour),
        static_cast<uint32_t>(tm->tm_min),
        static_cast<uint32_t>(tm->tm_sec)
    };
}

void on_tick_second() {
    signal_tick_second.emit();
}

void set(rtc::RTC&) {}

rtc::RTC now() {
    return host_time_to_rtc();
}

rtc::RTC now(rtc::RTC& out_datetime) {
    out_datetime = host_time_to_rtc();
    return out_datetime;
}

void dst_init() {}
rtc::RTC dst_adjust_returned_time(rtc::RTC& datetime) { return datetime; }
bool dst_check_date_range(uint16_t, uint16_t, uint16_t) { return false; }
void dst_update_date_range(uint16_t, uint16_t) {}
bool dst_test_date_range(uint16_t, uint16_t, portapack::persistent_memory::dst_config_t) { return false; }
uint8_t days_per_month(uint16_t, uint8_t month) {
    static const uint8_t dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return (month >= 1 && month <= 12) ? dm[month - 1] : 30;
}
uint8_t current_day_of_week() { return 0; }
uint8_t day_of_week(uint16_t, uint8_t, uint8_t) { return 0; }
bool leap_year(uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }
uint16_t day_of_year(uint16_t, uint8_t, uint8_t) { return 1; }
uint16_t day_of_year_of_nth_weekday(uint16_t, uint8_t, uint8_t, uint8_t) { return 1; }

bool isLeap(int year) { return leap_year(static_cast<uint16_t>(year)); }
time_t rtcToUnixUTC(const rtc::RTC&) { return std::time(nullptr); }

} // namespace rtc_time
