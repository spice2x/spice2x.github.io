#pragma once

#include <initguid.h>
#include <mmdeviceapi.h>

struct WrappedIMMDeviceCollection : IMMDeviceCollection {
    WrappedIMMDeviceCollection(IMMDeviceCollection *orig, EDataFlow dataFlow)
        : pReal(orig), data_flow(dataFlow) {
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
    // whether the synthetic fake Realtek render device should be appended to this collection
    bool should_inject_fake_realtek() const;

    IMMDeviceCollection *const pReal;
    const EDataFlow data_flow;
};
