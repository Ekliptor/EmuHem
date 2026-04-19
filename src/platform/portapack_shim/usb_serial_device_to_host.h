// EmuHem usb_serial_device_to_host.h stub
// Provides types and function declarations for USB serial (all no-ops).

#ifndef __USB_SERIAL_DEVICE_TO_HOST_H__
#define __USB_SERIAL_DEVICE_TO_HOST_H__

#include "ch.h"

#define USB_BULK_BUFFER_SIZE 64
#ifndef USBSERIAL_BUFFERS_SIZE
#define USBSERIAL_BUFFERS_SIZE 128
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct SerialUSBDriverVMT {
    void* _base_methods;
};

typedef struct SerialUSBDriver {
    const struct SerialUSBDriverVMT* vmt;
    InputQueue iqueue;
    OutputQueue oqueue;
    uint8_t ib[USBSERIAL_BUFFERS_SIZE];
    uint8_t ob[USBSERIAL_BUFFERS_SIZE];
} SerialUSBDriver;

extern SerialUSBDriver SUSBD1;

void init_serial_usb_driver(SerialUSBDriver* sdp);
size_t fillOBuffer(OutputQueue* oqp, const uint8_t* bp, size_t n);

#ifdef __cplusplus
}
#endif

#endif // __USB_SERIAL_DEVICE_TO_HOST_H__
