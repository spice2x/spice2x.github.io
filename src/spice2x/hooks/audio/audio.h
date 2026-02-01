#pragma once

#include <string>
#include <mutex>
#include <optional>

#include <windows.h>
#include <mmreg.h>
#ifndef _MSC_VER
#include <ks.h>
#include <ksmedia.h>
#endif

namespace hooks::audio {
    enum class Backend {
        Asio,
        WaveOut,
    };

    extern bool ENABLED;
    extern bool VOLUME_HOOK_ENABLED;
    extern bool USE_DUMMY;
    extern WAVEFORMATEXTENSIBLE FORMAT;
    extern std::optional<Backend> BACKEND;
    extern size_t ASIO_DRIVER_ID;
    extern bool ASIO_FORCE_UNLOAD_ON_STOP;
    extern bool LOW_LATENCY_SHARED_WASAPI;

    extern std::optional<std::string> DEFAULT_IMM_DEVICE_ID;
    extern std::mutex DEFAULT_IMM_DEVICE_MUTEX;
    extern void *DEFAULT_IMM_DEVICE;
    
    void init();
    void stop();

    inline std::optional<Backend> name_to_backend(const char *value) {
        if (_stricmp(value, "asio") == 0) {
            return Backend::Asio;
        } else if (_stricmp(value, "waveout") == 0) {
            return Backend::WaveOut;
        }

        return std::nullopt;
    }
}
