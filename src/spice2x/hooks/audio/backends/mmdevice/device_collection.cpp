#include "device_collection.h"
#include "device.h"
#include "null_device.h"
#include "util/utils.h"
#include "util/logging.h"

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceCollection::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }
    if (riid == __uuidof(IMMDeviceCollection)) {
        this->AddRef();
        *ppvObj = this;
        return S_OK;
    }
    return pReal->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIMMDeviceCollection::AddRef() {
    return pReal->AddRef();
}

ULONG STDMETHODCALLTYPE WrappedIMMDeviceCollection::Release() {
    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;
    if (refs == 0) {
        delete this;
    }
    return refs;
}

bool WrappedIMMDeviceCollection::should_inject_fake_realtek() const {
    return null_render_device_enabled()
        && (data_flow == eRender || data_flow == eAll);
}

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceCollection::GetCount(UINT *pcDevices) {
    // when active, hide all real devices and present only the synthetic one
    if (should_inject_fake_realtek()) {
        if (pcDevices == nullptr) {
            return E_POINTER;
        }
        *pcDevices = 1;
        return S_OK;
    }

    return pReal->GetCount(pcDevices);
}

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceCollection::Item(UINT nDevice, IMMDevice **ppDevice) {
    if (ppDevice == nullptr) {
        return E_POINTER;
    }

    // when active, the only device in the collection is the synthetic fake Realtek
    // render device; all real devices are hidden
    if (should_inject_fake_realtek()) {
        if (nDevice != 0) {
            return E_INVALIDARG;
        }
        log_info("audio", "WrappedIMMDeviceCollection::Item[{}] -> synthetic fake Realtek render device", nDevice);
        *ppDevice = new NullMMDevice();
        return S_OK;
    }

    log_info("audio", "WrappedIMMDeviceCollection::Item[{}]", nDevice);

    // call original
    const auto hr = pReal->Item(nDevice, ppDevice);

    // wrap interface
    if (SUCCEEDED(hr) && *ppDevice != nullptr) {
        *ppDevice = new WrappedIMMDevice(*ppDevice);
    }
    return hr;
}