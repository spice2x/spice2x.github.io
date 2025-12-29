#include "device_collection.h"
#include "device.h"
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

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceCollection::GetCount(UINT *pcDevices) {
    return pReal->GetCount(pcDevices);
}

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceCollection::Item(UINT nDevice, IMMDevice **ppDevice) {
    log_info("audio", "WrappedIMMDeviceCollection::Item[{}]", nDevice);

    // call original
    const auto hr = pReal->Item(nDevice, ppDevice);

    // wrap interface
    *ppDevice = new WrappedIMMDevice(*ppDevice);
    return hr;
}