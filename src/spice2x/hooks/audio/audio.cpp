#include "audio.h"

#include <vector>

#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include "hooks/audio/backends/mmdevice/device_enumerator.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/memutils.h"

#include "audio_private.h"

#ifdef _MSC_VER
DEFINE_GUID(CLSID_MMDeviceEnumerator,
    0xBCDE0395, 0xE52F, 0x467C,
    0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
#endif

// {ef985e71-d5c7-42d4-ba4d-2d073e2e96f4}
DEFINE_GUID(CLSID_DirectSoundI3DL2ReverbDMO,
    0xef985e71, 0xd5c7, 0x42d4,
    0xba, 0x4d, 0x2d, 0x07, 0x3e, 0x2e, 0x96, 0xf4);

// {87fc0268-9a55-4360-95aa-004a1d9de26c}
DEFINE_GUID(CLSID_DirectSoundWavesReverbDMO,
    0x87fc0268, 0x9a55, 0x4360,
    0x95, 0xaa, 0x00, 0x4a, 0x1d, 0x9d, 0xe2, 0x6c);

// function pointers
static decltype(CoCreateInstance) *CoCreateInstance_orig = nullptr;

namespace hooks::audio {

    // public globals
    bool ENABLED = true;
    bool VOLUME_HOOK_ENABLED = true;
    bool USE_DUMMY = false;
    WAVEFORMATEXTENSIBLE FORMAT {};
    std::optional<Backend> BACKEND = std::nullopt;
    size_t ASIO_DRIVER_ID = 0;
    bool ASIO_FORCE_UNLOAD_ON_STOP = false;

    // private globals
    IAudioClient *CLIENT = nullptr;
    std::mutex INITIALIZE_LOCK; // for asio
}

static HRESULT STDAPICALLTYPE CoCreateInstance_hook(
    REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID *ppv)
{

    HRESULT ret;

    // call original
    ret = CoCreateInstance_orig(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (FAILED(ret)) {
        if (IsEqualCLSID(rclsid, CLSID_MMDeviceEnumerator)) {
            log_warning("audio", "CoCreateInstance failed for CLSID_MMDeviceEnumerator, hr={}", FMT_HRESULT(ret));
        }
        
        // Windows 11 KB5052093 issue - reverb DMO went missing from dsdmo.dll (fails with 0x80040154)
        if (IsEqualCLSID(rclsid, CLSID_DirectSoundI3DL2ReverbDMO) && ret == (HRESULT)0x80040154) {
            log_warning(
                "audio",
                "CoCreateInstance for CLSID_DirectSoundI3DL2ReverbDMO failed with 0x80040154, swap with CLSID_DirectSoundWavesReverbDMO "
                "(workaround for Windows 11 KB5052093 issue); REVERB EX replaced with REVERB");
            ret = CoCreateInstance_orig(CLSID_DirectSoundWavesReverbDMO, pUnkOuter, dwClsContext, riid, ppv);
            if (FAILED(ret)) {
                log_warning("audio", "CoCreateInstance(CLSID_DirectSoundWavesReverbDMO...) failed, hr={}", FMT_HRESULT(ret));
            }
        }

        return ret;
    }

    // check if this is the audio device enumerator
    if (IsEqualCLSID(rclsid, CLSID_MMDeviceEnumerator)) {

        // wrap object
        auto mmde = reinterpret_cast<IMMDeviceEnumerator **>(ppv);
        *mmde = new WrappedIMMDeviceEnumerator(*mmde);
    }

    // return original result
    return ret;
}

namespace hooks::audio {
    void init() {
        if (!ENABLED) {
            return;
        }

        log_info("audio", "initializing");
        init_low_latency();

        // general hooks
        CoCreateInstance_orig = detour::iat_try("CoCreateInstance", CoCreateInstance_hook);
    }

    void stop() {
        log_info("audio", "stopping");
        if (CLIENT) {
            CLIENT->Stop();
            CLIENT->Release();
            CLIENT = nullptr;
        }
        stop_low_latency();
    }
}
