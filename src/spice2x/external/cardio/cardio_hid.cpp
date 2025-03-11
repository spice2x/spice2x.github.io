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

#include "cardio_hid.h"
#include "util/logging.h"
#include "util/utils.h"

extern "C" {
#include <hidclass.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
}

#include <windows.h>
#include <devguid.h>
#include <setupapi.h>


#define DEFAULT_ALLOCATED_CONTEXTS 2
#define CARD_READER_USAGE_PAGE 0xffca

// GUID_DEVCLASS_HIDCLASS
static GUID hidclass_guid = {0x745a17a0, 0x74d3, 0x11d0, {0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda}};

// globals
CRITICAL_SECTION CARDIO_HID_CRIT_SECTION;
struct cardio_hid_device *CARDIO_HID_CONTEXTS = NULL;
size_t CARDIO_HID_CONTEXTS_LENGTH = 0;

void hid_ctx_init(cardio_hid_device *ctx) {
    ctx->dev_path = NULL;
    ctx->dev_handle = INVALID_HANDLE_VALUE;
    ctx->initialized = FALSE;
    ctx->io_pending = FALSE;
    ctx->read_size = 0;
    ctx->pp_data = NULL;
    ctx->collection = NULL;
    ctx->collection_length = 0;

    memset(&ctx->read_state, 0, sizeof(OVERLAPPED));
    memset(&ctx->report_buffer, 0, sizeof(ctx->report_buffer));
    memset(&ctx->usage_value, 0, sizeof(ctx->usage_value));
    memset(&ctx->caps, 0, sizeof(HIDP_CAPS));
}

void hid_ctx_free(struct cardio_hid_device *ctx) {
    if (ctx->dev_path != NULL) {
        HeapFree(GetProcessHeap(), 0, ctx->dev_path);
        ctx->dev_path = NULL;
    }

    if (ctx->dev_handle != INVALID_HANDLE_VALUE) {
        CancelIo(ctx->dev_handle);
        CloseHandle(ctx->dev_handle);
        ctx->dev_handle = INVALID_HANDLE_VALUE;
    }

    if (ctx->pp_data != NULL) {
        HidD_FreePreparsedData(ctx->pp_data);
        ctx->pp_data = NULL;
    }

    if (ctx->collection != NULL) {
        HeapFree(GetProcessHeap(), 0, ctx->collection);
        ctx->collection = NULL;
    }
}

void hid_ctx_reset(struct cardio_hid_device *ctx) {
    ctx->initialized = FALSE;
    ctx->io_pending = FALSE;
    ctx->read_size = 0;
    ctx->collection_length = 0;

    hid_ctx_free(ctx);

    memset(&ctx->read_state, 0, sizeof(OVERLAPPED));
    memset(&ctx->report_buffer, 0, sizeof(ctx->report_buffer));
    memset(&ctx->usage_value, 0, sizeof(ctx->usage_value));
    memset(&ctx->caps, 0, sizeof(HIDP_CAPS));
}

BOOL cardio_hid_init() {
    size_t i, contexts_size;

    InitializeCriticalSectionAndSpinCount(&CARDIO_HID_CRIT_SECTION, 0x00000400);

    contexts_size = DEFAULT_ALLOCATED_CONTEXTS * sizeof(struct cardio_hid_device);
    CARDIO_HID_CONTEXTS = (struct cardio_hid_device *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, contexts_size);
    if (CARDIO_HID_CONTEXTS == NULL) {
        return FALSE;
    }

    CARDIO_HID_CONTEXTS_LENGTH = DEFAULT_ALLOCATED_CONTEXTS;

    for (i = 0; i < CARDIO_HID_CONTEXTS_LENGTH; i++) {
        hid_ctx_init(&CARDIO_HID_CONTEXTS[i]);
    }

    return TRUE;
}

void cardio_hid_close() {
    size_t i;

    if (CARDIO_HID_CONTEXTS_LENGTH > 0) {
        for (i = 0; i < CARDIO_HID_CONTEXTS_LENGTH; i++) {
            hid_ctx_free(&CARDIO_HID_CONTEXTS[i]);
        }

        HeapFree(GetProcessHeap(), 0, CARDIO_HID_CONTEXTS);
        CARDIO_HID_CONTEXTS = NULL;
        CARDIO_HID_CONTEXTS_LENGTH = 0;

        DeleteCriticalSection(&CARDIO_HID_CRIT_SECTION);
    }
}

BOOL cardio_hid_add_device(LPCWSTR device_path) {
    BOOL res = FALSE;
    size_t i;

    EnterCriticalSection(&CARDIO_HID_CRIT_SECTION);

    for (i = 0; i < CARDIO_HID_CONTEXTS_LENGTH; i++) {
        if (!CARDIO_HID_CONTEXTS[i].initialized) {
            res = cardio_hid_scan_device(&CARDIO_HID_CONTEXTS[i], device_path);
            break;
        }
    }

    LeaveCriticalSection(&CARDIO_HID_CRIT_SECTION);

    return res;
}

BOOL cardio_hid_remove_device(LPCWSTR device_path) {
    BOOL res = FALSE;
    size_t i;

    EnterCriticalSection(&CARDIO_HID_CRIT_SECTION);

    for (i = 0; i < CARDIO_HID_CONTEXTS_LENGTH; i++) {
        // The device paths in `hid_scan` are partially lower-case, so perform a
        // case-insensitive comparison here
        if (CARDIO_HID_CONTEXTS[i].initialized && (_wcsicmp(device_path, CARDIO_HID_CONTEXTS[i].dev_path) == 0)) {
            hid_ctx_reset(&CARDIO_HID_CONTEXTS[i]);
            res = TRUE;
            break;
        }
    }

    LeaveCriticalSection(&CARDIO_HID_CRIT_SECTION);

    return res;
}

/*
 * Scan HID device to see if it is a HID reader
 */
BOOL cardio_hid_scan_device(struct cardio_hid_device *ctx, LPCWSTR device_path) {
    NTSTATUS res;

    size_t dev_path_size = (wcslen(device_path) + 1) * sizeof(WCHAR);
    ctx->dev_path = (LPWSTR) HeapAlloc(GetProcessHeap(), 0, dev_path_size);
    if (ctx->dev_path == NULL) {
        return FALSE;
    }

    memcpy(ctx->dev_path, device_path, dev_path_size);
    ctx->dev_path[dev_path_size - 1] = '\0';
    ctx->dev_handle = CreateFileW(
            ctx->dev_path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL);
    if (ctx->dev_handle == INVALID_HANDLE_VALUE) {
        log_info("cardio", "could not open device: {}", ws2s(std::wstring(device_path)));
        HeapFree(GetProcessHeap(), 0, ctx->dev_path);
        ctx->dev_path = NULL;
        ctx->dev_handle = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    if (!HidD_GetPreparsedData(ctx->dev_handle, &ctx->pp_data)) {
        goto end;
    }

    res = HidP_GetCaps(ctx->pp_data, &ctx->caps);
    if (res != HIDP_STATUS_SUCCESS) {
        goto end;
    }

    // 0xffca is the card reader usage page ID
    if (ctx->caps.UsagePage != CARD_READER_USAGE_PAGE) {
        goto end;
    } else if (ctx->caps.NumberInputValueCaps == 0) {
        goto end;
    }
    ctx->collection_length = ctx->caps.NumberInputValueCaps;
    ctx->collection = (HIDP_VALUE_CAPS *) HeapAlloc(GetProcessHeap(), 0,
                                                    ctx->collection_length * sizeof(HIDP_VALUE_CAPS));
    if (ctx->collection == NULL) {
        goto end;
    }
    res = HidP_GetValueCaps(
            HidP_Input,
            ctx->collection,
            &ctx->collection_length,
            ctx->pp_data);
    if (res != HIDP_STATUS_SUCCESS) {
        goto end;
    }

    log_info("cardio", "detected reader: {}", ws2s(std::wstring(device_path)));
    ctx->initialized = TRUE;
    return TRUE;

    end:
    hid_ctx_reset(ctx);
    return FALSE;
}

/*
 * Checks all devices registered with the HIDClass GUID. If the usage page of
 * the device is 0xffca, then a compatible card reader was found.
 *
 * Usage 0x41 => ISO_15693
 * Usage 0x42 => ISO_18092 (FeliCa)
 */
BOOL cardio_hid_scan() {
    BOOL res = TRUE;
    SP_DEVINFO_DATA devinfo_data;
    SP_DEVICE_INTERFACE_DATA device_interface_data;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
    HDEVINFO device_info_set;
    GUID hid_guid;
    DWORD device_index = 0;
    DWORD dwSize = 0;
    size_t hid_devices = 0;

    // get GUID
    HidD_GetHidGuid(&hid_guid);

    // HID collection opening needs `DIGCF_DEVICEINTERFACE` and ignore
    // disconnected devices
    device_info_set = SetupDiGetClassDevs(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE) {
        res = FALSE;
        goto end;
    }

    memset(&devinfo_data, 0, sizeof(SP_DEVINFO_DATA));
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
    device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // `SetupDiEnumDeviceInterfaces` must come before `SetupDiEnumDeviceInfo`
    // else `SetupDiEnumDeviceInterfaces` will fail with error 259
    while (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &hid_guid, device_index, &device_interface_data)) {

        // Get the required size
        if (SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &dwSize, NULL)) {
            goto cont;
        }

        device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *) HeapAlloc(GetProcessHeap(), 0, dwSize);
        if (device_interface_detail_data == NULL) {
            goto cont;
        }

        device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data,
                                              dwSize, NULL, NULL)) {
            goto cont;
        }

        if (!SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data)) {
            goto cont;
        }

        if (!IsEqualGUID(hidclass_guid, devinfo_data.ClassGuid)) {
            goto cont;
        }

        EnterCriticalSection(&CARDIO_HID_CRIT_SECTION);

        if (hid_devices == CARDIO_HID_CONTEXTS_LENGTH) {
            CARDIO_HID_CONTEXTS_LENGTH++;

            CARDIO_HID_CONTEXTS = (struct cardio_hid_device *) HeapReAlloc(
                    GetProcessHeap(),
                    HEAP_ZERO_MEMORY,
                    CARDIO_HID_CONTEXTS,
                    CARDIO_HID_CONTEXTS_LENGTH * sizeof(struct cardio_hid_device)
            );
            if (CARDIO_HID_CONTEXTS == NULL) {
                LeaveCriticalSection(&CARDIO_HID_CRIT_SECTION);
                HeapFree(GetProcessHeap(), 0, device_interface_detail_data);
                res = FALSE;
                goto end;
            }

            hid_ctx_init(&CARDIO_HID_CONTEXTS[hid_devices]);
        }

        if (cardio_hid_scan_device(&CARDIO_HID_CONTEXTS[hid_devices], device_interface_detail_data->DevicePath)) {
            hid_devices++;
        }

        LeaveCriticalSection(&CARDIO_HID_CRIT_SECTION);

        cont:
        if (device_interface_detail_data) {
            HeapFree(GetProcessHeap(), 0, device_interface_detail_data);
            device_interface_detail_data = NULL;
        }

        device_index++;
    }

    end:
    if (device_info_set != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(device_info_set);
    }

    return res;
}

cardio_hid_poll_value_t cardio_hid_device_poll(struct cardio_hid_device *ctx) {
    DWORD error = 0;

    if (!ctx->initialized) {
        return HID_POLL_ERROR;
    }

    if (ctx->io_pending) {
        // Do this if inside to not have more `ReadFile` overlapped I/O requests.
        // If there are more calls to `ReadFile` than `GetOverlappedResult` then
        // eventually the working set quota will run out triggering error 1426
        // (ERROR_WORKING_SET_QUOTA).
        if (HasOverlappedIoCompleted(&ctx->read_state)) {
            ctx->io_pending = FALSE;

            if (!GetOverlappedResult(ctx->dev_handle, &ctx->read_state, &ctx->read_size, FALSE)) {
                return HID_POLL_ERROR;
            }

            memset(&ctx->read_state, 0, sizeof(OVERLAPPED));

            return HID_POLL_CARD_READY;
        }
    } else {
        if (!ReadFile(
                ctx->dev_handle,
                &ctx->report_buffer,
                sizeof(ctx->report_buffer),
                &ctx->read_size,
                &ctx->read_state)) {
            error = GetLastError();

            if (error == ERROR_IO_PENDING) {
                ctx->io_pending = TRUE;
            } else {
                return HID_POLL_ERROR;
            }
        } else {
            // The read completed right away
            return HID_POLL_CARD_READY;
        }
    }

    return HID_POLL_CARD_NOT_READY;
}

cardio_hid_card_type cardio_hid_device_read(struct cardio_hid_device *hid_ctx) {

    // check if not initialized
    if (!hid_ctx->initialized) {
        return HID_CARD_NONE;
    }

    // check if IO is pending
    if (hid_ctx->io_pending) {
        return HID_CARD_NONE;
    }

    // check if nothing was read
    if (hid_ctx->read_size == 0) {
        return HID_CARD_NONE;
    }

    // iterate collections
    for (int i = 0; i < hid_ctx->collection_length; i++) {
        HIDP_VALUE_CAPS *item = &hid_ctx->collection[i];

        // get usages
        NTSTATUS res = HidP_GetUsageValueArray(
                HidP_Input,
                CARD_READER_USAGE_PAGE,
                0, // LinkCollection
                item->NotRange.Usage,
                (PCHAR) &hid_ctx->usage_value,
                sizeof(hid_ctx->usage_value),
                hid_ctx->pp_data,
                (PCHAR) &hid_ctx->report_buffer,
                hid_ctx->read_size);

        // loop through the collection to find the entry that handles this report ID
        if (res == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
            continue;
        }

        // check if failed
        if (res != HIDP_STATUS_SUCCESS) {
            return HID_CARD_NONE;
        }

        // return card type
        return (cardio_hid_card_type) item->NotRange.Usage;
    }

    return HID_CARD_NONE;
}
