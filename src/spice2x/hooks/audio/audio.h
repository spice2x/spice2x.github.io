#pragma once

#include <cstdint>
#include <optional>
#include <string>

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

    // surround-to-stereo downmix algorithm
    enum class DownmixAlgorithm {
        FrontOnly, // keep only the front channels
        RearOnly,  // keep only the rear/back channels
        SideOnly,  // keep only the side channels
        AC4,       // AC-4 stereo downmix coefficients (ETSI TS 103 190-1)
        Normalize, // all channels equally loud, LFE dropped
    };

    extern bool ENABLED;
    extern bool VOLUME_HOOK_ENABLED;
    extern std::optional<DownmixAlgorithm> DOWNMIX_ALGORITHM;
    extern float VOLUME_BOOST;

    // target sample rate the hooked output is resampled to, if set
    extern std::optional<uint32_t> RESAMPLE_RATE;

    // minimum WASAPI exclusive buffer duration (milliseconds), if set. enlarges the device buffer
    // to avoid underrun crackle on endpoints that cannot service a tiny buffer in time.
    extern std::optional<uint32_t> EXCLUSIVE_BUFFER_MS;

    // when true, WASAPI compatibility mode is active: exclusive-mode streams are redirected to
    // shared mode and natively-shared streams gain the engine's format converter. the Windows audio
    // engine performs any required sample-rate / channel / bit-depth conversion, so the game's
    // format is passed through unchanged and other applications can play audio simultaneously.
    extern bool WASAPI_COMPATIBILITY_MODE;
    extern bool USE_DUMMY;
    extern WAVEFORMATEXTENSIBLE FORMAT;
    extern std::optional<Backend> BACKEND;

    // when true, a synthetic "Realtek" render endpoint is injected into device enumeration that
    // discards all audio. used by gitadora arena, whose device search crashes when no render
    // endpoint reports a "Realtek" friendly name.
    extern bool INJECT_FAKE_REALTEK_AUDIO;
    extern std::optional<size_t> ASIO_DRIVER_ID;
    extern std::string ASIO_DRIVER_NAME;
    extern bool ASIO_FORCE_UNLOAD_ON_STOP;
    extern bool LOW_LATENCY_SHARED_WASAPI;

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
