#pragma once

#include <initguid.h>
#include <mmdeviceapi.h>

struct WrappedIMMDeviceCollection : IMMDeviceCollection {
    explicit WrappedIMMDeviceCollection(IMMDeviceCollection *orig) : pReal(orig) {
    }

    WrappedIMMDeviceCollection(const WrappedIMMDeviceCollection &) = delete;
    WrappedIMMDeviceCollection &operator=(const WrappedIMMDeviceCollection &) = delete;

    virtual ~WrappedIMMDeviceCollection() = default;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IMMDeviceCollection
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT *pcDevices) override;
    virtual HRESULT STDMETHODCALLTYPE Item(UINT nDevice, IMMDevice **ppDevice) override;
#pragma endregion

private:
    IMMDeviceCollection *const pReal;
};
