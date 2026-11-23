/*
 * mock_libusb_internal.h -- Shared state for the libusb mock.
 */

#ifndef TR4MPASS_MOCK_LIBUSB_INTERNAL_H
#define TR4MPASS_MOCK_LIBUSB_INTERNAL_H

#include "mock_internal.h"

#include <stdint.h>
#include <stddef.h>

#include <libusb.h>

struct libusb_context {
    uint32_t magic;
};

struct libusb_device {
    uint16_t vid;
    uint16_t pid;
    uint8_t  bus;
    uint8_t  addr;
};

struct libusb_device_handle {
    struct libusb_device *dev;
    int                   claimed;
};

#define CTX_MAGIC 0x5553425Bu /* "USB[" */

#define MOCK_CTRL_SLOTS 16

typedef struct {
    int    in_use;
    int    consumed;
    int    return_code;
    void  *data;
    size_t data_len;
} mock_libusb_ctrl_stage_t;

extern mock_libusb_ctrl_stage_t g_libusb_ctrl_stages[MOCK_CTRL_SLOTS];
extern int       g_libusb_list_count;
extern uint16_t  g_libusb_list_vid;
extern uint16_t  g_libusb_list_pid;

#endif /* TR4MPASS_MOCK_LIBUSB_INTERNAL_H */
