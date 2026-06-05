#pragma once

#include <atomic>

#include <mmdeviceapi.h>

// returns true when a synthetic render endpoint should be injected into device
// enumeration. games like gitadora arena search the render endpoint list for a
// device whose friendly name contains "Realtek" and crash with a null pointer
// dereference when no match exists. presenting a fake match that routes to the
// null audio backend lets the search succeed while discarding the audio.
bool null_render_device_enabled();

// fake IMMDevice that reports a "Realtek" friendly name and activates straight
// into the null audio backend, never touching real hardware.
struct NullMMDevice : IMMDevice {
    NullMMDevice() = default;

    NullMMDevice(const NullMMDevice &) = delete;
    NullMMDevice &operator=(const NullMMDevice &) = delete;

    virtual ~NullMMDevice() = default;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IMMDevice
    HRESULT STDMETHODCALLTYPE Activate(REFIID iid, DWORD dwClsCtx, PROPVARIANT *pActivationParams, void **ppInterface) override;
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD stgmAccess, IPropertyStore **ppProperties) override;
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR *ppstrId) override;
    HRESULT STDMETHODCALLTYPE GetState(DWORD *pdwState) override;
#pragma endregion

private:
    std::atomic<ULONG> ref_cnt = 1;
};
