#pragma once

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <windows.h>

#include "external/fmt/include/fmt/format.h"
#include "external/fmt/include/fmt/compile.h"

#include "launcher/launcher.h"
#include "launcher/logger.h"
#include "launcher/shutdown.h"

// string conversion helper for logging purposes
template<typename T>
static inline std::string to_string(T value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

template<typename T>
static inline std::string to_hex(T val, size_t width = sizeof(T) * 2) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(width) << std::hex << (val | 0);
    return ss.str();
}

// util
std::string_view log_get_datetime();
std::string_view log_get_datetime(std::time_t ts);

struct fmt_log {
    std::time_t ts;
    const std::string_view level;
    const std::string_view module;
};

template<>
struct fmt::formatter<fmt_log> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const fmt_log &v, FormatContext &ctx) {
        return format_to(ctx.out(), FMT_COMPILE("{} {}:{}: "), log_get_datetime(v.ts), v.level, v.module);
    }
};

struct fmt_hresult {
    HRESULT result;
};
#define FMT_HRESULT(result) fmt_hresult { result }

template<>
struct fmt::formatter<fmt_hresult> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const fmt_hresult &v, FormatContext &ctx) {
        return format_to(ctx.out(), FMT_COMPILE("0x{:08x}"), static_cast<unsigned>(v.result));
    }
};

// misc log
#define LOG_FORMAT(level, module, fmt_str, ...) fmt::format(FMT_COMPILE("{}" fmt_str "\n"), \
    fmt_log { std::time(nullptr), level, module }, ## __VA_ARGS__)
#define log_misc(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#define log_info(module, format_str, ...) logger::push( \
    LOG_FORMAT("I", module, format_str, ## __VA_ARGS__), logger::Style::DEFAULT)
#define log_warning(module, format_str, ...) logger::push( \
    LOG_FORMAT("W", module, format_str, ## __VA_ARGS__), logger::Style::YELLOW)
#define log_fatal(module, format_str, ...) { \
    logger::push(LOG_FORMAT("F", module, format_str, ## __VA_ARGS__), logger::Style::RED); \
    logger::push(LOG_FORMAT("F", "spice", "encountered a fatal error, you can close the window or press ctrl + c"), logger::Style::RED); \
    launcher::stop_subsystems(); \
    Sleep(10000); \
    launcher::kill(); \
    std::terminate(); \
} ((void) 0 )
