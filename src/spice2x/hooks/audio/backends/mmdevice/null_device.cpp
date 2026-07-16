#include "null_device.h"

#include <atomic>
#include <cstring>

#include <audioclient.h>

#include "hooks/audio/audio.h"
#include "hooks/audio/audio_private.h"
#include "hooks/audio/backends/wasapi/dummy_audio_client.h"
#include "util/logging.h"
#include "util/utils.h"

#include "null_discard_backend.h"

// friendly name reported by the synthetic device. must contain "Realtek" so the
// gitadora arena device search matches it.
static const wchar_t NULL_DEVICE_FRIENDLY_NAME[] = L"Realtek High Definition Audio";

// arbitrary identifier reported by the synthetic device.
static const wchar_t NULL_DEVICE_ID[] = L"{spice2x-null-render-device}";

// PKEY_Device_FriendlyName, hardcoded to avoid pulling in functiondiscoverykeys_devpkey.h
static const PROPERTYKEY PKEY_DEVICE_FRIENDLY_NAME_LOCAL = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
    14
};

bool null_render_device_enabled() {
    return hooks::audio::INJECT_FAKE_REALTEK_AUDIO;
}

// duplicate a wide string into CoTaskMem so the caller can free it with
// CoTaskMemFree / PropVariantClear as the COM API contract requires.
static LPWSTR co_task_wcsdup(const wchar_t *src) {
    const size_t bytes = (wcslen(src) + 1) * sizeof(wchar_t);
    auto *dst = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
    if (dst != nullptr) {
        memcpy(dst, src, bytes);
    }
    return dst;
}

namespace {

    // minimal IPropertyStore that only answers PKEY_Device_FriendlyName.
    struct NullPropertyStore : IPropertyStore {
        std::atomic<ULONG> ref_cnt = 1;

        virtual ~NullPropertyStore() = default;

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override {
            if (ppvObj == nullptr) {
                return E_POINTER;
            }
            if (riid == __uuidof(IUnknown) || riid == __uuidof(IPropertyStore)) {
                this->AddRef();
                *ppvObj = this;
                return S_OK;
            }
            *ppvObj = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++this->ref_cnt;
        }
        ULONG STDMETHODCALLTYPE Release() override {
            const ULONG refs = --this->ref_cnt;
            if (refs == 0) {
                delete this;
            }
            return refs;
        }
        HRESULT STDMETHODCALLTYPE GetCount(DWORD *cProps) override {
            if (cProps == nullptr) {
                return E_POINTER;
            }
            *cProps = 1;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetAt(DWORD iProp, PROPERTYKEY *pkey) override {
            if (pkey == nullptr) {
                return E_POINTER;
            }
            if (iProp != 0) {
                return E_INVALIDARG;
            }
            *pkey = PKEY_DEVICE_FRIENDLY_NAME_LOCAL;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY key, PROPVARIANT *pv) override {
            if (pv == nullptr) {
                return E_POINTER;
            }
            PropVariantInit(pv);
            if (key.fmtid == PKEY_DEVICE_FRIENDLY_NAME_LOCAL.fmtid
                    && key.pid == PKEY_DEVICE_FRIENDLY_NAME_LOCAL.pid) {
                pv->pwszVal = co_task_wcsdup(NULL_DEVICE_FRIENDLY_NAME);
                if (pv->pwszVal == nullptr) {
                    return E_OUTOFMEMORY;
                }
                pv->vt = VT_LPWSTR;
            }
            // unknown keys are returned as VT_EMPTY / S_OK
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY, REFPROPVARIANT) override {
            return STG_E_ACCESSDENIED;
        }
        HRESULT STDMETHODCALLTYPE Commit() override {
            return S_OK;
        }
    };
}

#pragma region IUnknown
HRESULT STDMETHODCALLTYPE NullMMDevice::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMDevice)) {
        this->AddRef();
        *ppvObj = this;
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE NullMMDevice::AddRef() {
    return ++this->ref_cnt;
}
ULONG STDMETHODCALLTYPE NullMMDevice::Release() {
    const ULONG refs = --this->ref_cnt;
    if (refs == 0) {
        delete this;
    }
    return refs;
}
#pragma endregion

#pragma region IMMDevice
HRESULT STDMETHODCALLTYPE NullMMDevice::Activate(
    REFIID iid,
    DWORD,
    PROPVARIANT *,
    void **ppInterface)
{
    if (ppInterface == nullptr) {
        return E_POINTER;
    }
    *ppInterface = nullptr;

    log_info("audio::null", "NullMMDevice::Activate {}", guid2s(iid));

    if (iid == IID_IAudioClient) {
        auto *client = static_cast<IAudioClient *>(new DummyIAudioClient(new NullDiscardBackend()));
        *ppInterface = client;

        return S_OK;
    }

    return E_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE NullMMDevice::OpenPropertyStore(DWORD, IPropertyStore **ppProperties) {
    if (ppProperties == nullptr) {
        return E_POINTER;
    }
    *ppProperties = new NullPropertyStore();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE NullMMDevice::GetId(LPWSTR *ppstrId) {
    if (ppstrId == nullptr) {
        return E_POINTER;
    }
    *ppstrId = co_task_wcsdup(NULL_DEVICE_ID);
    return *ppstrId != nullptr ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE NullMMDevice::GetState(DWORD *pdwState) {
    if (pdwState == nullptr) {
        return E_POINTER;
    }
    *pdwState = DEVICE_STATE_ACTIVE;
    return S_OK;
}
#pragma endregion
