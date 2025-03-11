/**
 * MIT-License
 * Copyright (c) 2018 by Felix
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Modified version.
 */

#ifndef SPICETOOLS_CARDIO_HID_H
#define SPICETOOLS_CARDIO_HID_H

#include <windows.h>
#include <stdint.h>

extern "C" {
#include <hidsdi.h>
};

extern CRITICAL_SECTION CARDIO_HID_CRIT_SECTION;
extern struct cardio_hid_device *CARDIO_HID_CONTEXTS;
extern size_t CARDIO_HID_CONTEXTS_LENGTH;

struct cardio_hid_device {
    LPWSTR dev_path;
    HANDLE dev_handle;
    OVERLAPPED read_state;
    BOOL initialized;
    BOOL io_pending;

    BYTE report_buffer[128];
    unsigned char usage_value[128];
    DWORD read_size;

    PHIDP_PREPARSED_DATA pp_data;
    HIDP_CAPS caps;
    PHIDP_VALUE_CAPS collection;
    USHORT collection_length;
};

typedef enum cardio_poll_value {
    HID_POLL_ERROR = 0,
    HID_POLL_CARD_NOT_READY = 1,
    HID_POLL_CARD_READY = 2,
} cardio_hid_poll_value_t;

typedef enum cardio_hid_card_type {
    HID_CARD_NONE = 0,
    HID_CARD_ISO_15693 = 0x41,
    HID_CARD_ISO_18092 = 0x42,
} cardio_hid_card_type_t;

BOOL cardio_hid_init();

void cardio_hid_close();

BOOL cardio_hid_add_device(LPCWSTR device_path);

BOOL cardio_hid_remove_device(LPCWSTR device_path);

BOOL cardio_hid_scan_device(struct cardio_hid_device *ctx, LPCWSTR device_path);

BOOL cardio_hid_scan();

cardio_hid_poll_value_t cardio_hid_device_poll(struct cardio_hid_device *ctx);

cardio_hid_card_type cardio_hid_device_read(struct cardio_hid_device *ctx);

#endif //SPICETOOLS_CARDIO_HID_H
