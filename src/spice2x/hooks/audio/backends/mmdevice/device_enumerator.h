#pragma once

#include <initguid.h>
#include <mmdeviceapi.h>

#include "util/logging.h"

struct WrappedIMMDeviceEnumerator : IMMDeviceEnumerator {
    explicit WrappedIMMDeviceEnumerator(IMMDeviceEnumerator *orig) : pReal(orig) {
    }

    WrappedIMMDeviceEnumerator(const WrappedIMMDeviceEnumerator &) = delete;
    WrappedIMMDeviceEnumerator &operator=(const WrappedIMMDeviceEnumerator &) = delete;

    virtual ~WrappedIMMDeviceEnumerator() = default;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IMMDeviceEnumerator
    virtual HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow dataFlow, DWORD dwStateMask, IMMDeviceCollection **ppDevices) override;
    virtual HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow dataFlow, ERole role, IMMDevice **ppEndpoint) override;
    virtual HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR pwstrId, IMMDevice **ppDevice) override;
    virtual HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IMMNotificationClient *pClient) override;
    virtual HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IMMNotificationClient *pClient) override;
#pragma endregion

private:
    IMMDeviceEnumerator *const pReal;
};
