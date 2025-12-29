#include "device.h"

#include <mutex>

#include <audioclient.h>
#include <endpointvolume.h>

#include "hooks/audio/audio_private.h"
#include "hooks/audio/backends/mmdevice/audio_endpoint_volume.h"
#include "hooks/audio/backends/wasapi/audio_client.h"
#include "util/utils.h"

#define PRINT_FAILED_RESULT(name, ret) \
    do { \
        if (AUDIO_LOG_HRESULT) { \
            log_warning("audio::mmdevice", "{} failed, hr={}", name, FMT_HRESULT(ret)); \
        } \
    } while (0)

#define CHECK_RESULT(x) \
    do { \
        HRESULT __ret = (x); \
        if (FAILED(__ret)) { \
            PRINT_FAILED_RESULT(__FUNCTION__, __ret); \
        } \
        return __ret; \
    } while (0)

#ifdef _MSC_VER
DEFINE_GUID(IID_IMMDevice,
        0xd666063f, 0x1587, 0x4e43,
        0x81, 0xf1, 0xb9, 0x48, 0xe8, 0x07, 0x36, 0x3f);
#endif

HRESULT STDMETHODCALLTYPE WrappedIMMDevice::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIMMDevice ||
        riid == IID_IMMDevice)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}
ULONG STDMETHODCALLTYPE WrappedIMMDevice::AddRef() {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIMMDevice::Release() {

    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        log_misc("audio::mmdevice", "WrappedIMMDevice::Release");
        delete this;
    }

    return refs;
}

// IMMDevice
HRESULT STDMETHODCALLTYPE WrappedIMMDevice::Activate(
    REFIID iid,
    DWORD dwClsCtx,
    PROPVARIANT *pActivationParams,
    void **ppInterface)
{
    log_misc("audio::mmdevice", "WrappedIMMDevice::Activate {}", guid2s(iid));

    // call original
    HRESULT ret = pReal->Activate(iid, dwClsCtx, pActivationParams, ppInterface);

    // check for failure
    if (FAILED(ret)) {
        PRINT_FAILED_RESULT("IMMDevice::Activate", ret);
        return ret;
    }

    if (iid == IID_IAudioClient) {

        // prevent initialization recursion when using some ASIO backends that proxy to DirectSound, WASAPI, or WDM
        // like ASIO4All or FlexASIO
        if (!hooks::audio::INITIALIZE_LOCK.try_lock()) {
            log_warning("audio::mmdevice", "ignoring wrap request while backend is initializing, possible recursion");
            return ret;
        }
        std::lock_guard initialize_guard(hooks::audio::INITIALIZE_LOCK, std::adopt_lock);

        auto client = reinterpret_cast<IAudioClient *>(*ppInterface);

        // release old audio client if initialized
        if (hooks::audio::CLIENT) {
            hooks::audio::CLIENT->Release();
        }

        /*
        ret = wrap_audio_client(pReal, dwClsCtx, pActivationParams, &client);
        if (FAILED(ret)) {
            return ret;
        }
        */
        client = wrap_audio_client(client);

        *ppInterface = client;

        // persist the audio client
        hooks::audio::CLIENT = client;
        hooks::audio::CLIENT->AddRef();

    } else if (iid == __uuidof(IAudioEndpointVolume) && hooks::audio::VOLUME_HOOK_ENABLED) {
        *ppInterface = new WrappedIAudioEndpointVolume(reinterpret_cast<IAudioEndpointVolume *>(*ppInterface));
    }

    return ret;
}
HRESULT STDMETHODCALLTYPE WrappedIMMDevice::OpenPropertyStore(DWORD stgmAccess, IPropertyStore **ppProperties) {
    CHECK_RESULT(pReal->OpenPropertyStore(stgmAccess, ppProperties));
}
HRESULT STDMETHODCALLTYPE WrappedIMMDevice::GetId(LPWSTR *ppstrId) {
    CHECK_RESULT(pReal->GetId(ppstrId));
}
HRESULT STDMETHODCALLTYPE WrappedIMMDevice::GetState(DWORD *pdwState) {
    CHECK_RESULT(pReal->GetState(pdwState));
}
