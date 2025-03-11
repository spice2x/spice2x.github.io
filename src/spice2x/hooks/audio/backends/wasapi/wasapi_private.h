#pragma once

#include <windows.h>

#include "hooks/audio/audio_private.h"
#include "util/logging.h"

#define PRINT_FAILED_RESULT(class_name, func_name, ret) \
    if (AUDIO_LOG_HRESULT) { \
        log_warning("audio::wasapi", "{}::{} failed, hr={}", class_name, func_name, FMT_HRESULT(ret)); \
    }

#define SAFE_CALL(class_name, func_name, x) \
    do { \
        HRESULT __hr = (x); \
        if (FAILED(__hr)) { \
            PRINT_FAILED_RESULT(class_name, func_name, __hr); \
            return __hr; \
        } \
    } while (0)

#define CHECK_RESULT(x) \
    do { \
        HRESULT __ret = (x); \
        if (FAILED(__ret)) { \
            PRINT_FAILED_RESULT(CLASS_NAME, __func__, __ret); \
        } \
        return __ret; \
    } while (0)
