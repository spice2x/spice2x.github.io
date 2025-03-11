#pragma once

#include "external/fmt/include/fmt/format.h"

#define ENUM_VARIANT(value) case (value): return #value

#define FLAGS_START(VAR) \
    if ((VAR) == 0) { \
        return "0x0"; \
    } \
    \
    bool first = true; \
    std::string result

#define FLAG(VAR, value) \
    do { \
        if ((VAR) & (value)) { \
            if (!first) { \
                result += " | "; \
            } \
            first = false; \
            result += (#value); \
        } \
    } while (0)

#define FLAGS_END(VAR) \
    if (result.empty()) { \
        result = fmt::format("0x{:08x}", (VAR)); \
    } \
    \
    return result
