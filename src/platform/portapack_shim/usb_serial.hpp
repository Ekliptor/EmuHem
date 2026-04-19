// EmuHem usb_serial.hpp stub
// Stub for portapack::USBSerial class.

#ifndef __USB_SERIAL_H__
#define __USB_SERIAL_H__

#include "ch.h"

class EventDispatcher;

namespace portapack {

class USBSerial {
public:
    void initialize() {}
    void dispatch() {}
    void dispatch_transfer() {}
    void on_channel_opened() {}
    void on_channel_closed() {}
    void setEventDispatcher(EventDispatcher*) {}
    bool serial_connected() const { return false; }

private:
    bool connected{false};
    bool shell_created{false};
    EventDispatcher* _eventDispatcher{nullptr};
};

} // namespace portapack

#endif // __USB_SERIAL_H__
