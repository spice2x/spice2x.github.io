#pragma once

#include <initguid.h>
#include <mmdeviceapi.h>

#include "util/logging.h"

// {7CC2A363-D96F-4BE2-B6CF-2A44AADA424B}
static const GUID IID_WrappedIMMDevice = {
    0x7cc2a363, 0xd96f, 0x4be2, { 0xb6, 0xcf, 0x2a, 0x44, 0xaa, 0xda, 0x42, 0x4b }
};

struct WrappedIMMDevice : IMMDevice {
    explicit WrappedIMMDevice(IMMDevice *orig) : pReal(orig) {
    }

    WrappedIMMDevice(const WrappedIMMDevice &) = delete;
    WrappedIMMDevice &operator=(const WrappedIMMDevice &) = delete;

    virtual ~WrappedIMMDevice() = default;

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

    IMMDevice *const pReal;
};
