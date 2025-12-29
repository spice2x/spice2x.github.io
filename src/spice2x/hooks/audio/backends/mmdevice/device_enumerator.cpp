#include "device_enumerator.h"
#include "device_collection.h"
#include "device.h"
#include "util/utils.h"

#ifdef _MSC_VER
DEFINE_GUID(IID_IMMDeviceEnumerator,
        0xa95664d2, 0x9614, 0x4f35,
        0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
#endif

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_IMMDeviceEnumerator) {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}
ULONG STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::AddRef() {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::Release() {

    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::EnumAudioEndpoints(
    EDataFlow dataFlow,
    DWORD dwStateMask,
    IMMDeviceCollection **ppDevices)
{
    const auto hr = pReal->EnumAudioEndpoints(dataFlow, dwStateMask, ppDevices);
    if (SUCCEEDED(hr) && (ppDevices != nullptr) && (*ppDevices != nullptr)) {
        *ppDevices = new WrappedIMMDeviceCollection(*ppDevices);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::GetDefaultAudioEndpoint(
    EDataFlow dataFlow,
    ERole role,
    IMMDevice **ppEndpoint)
{
    // call orignal
    HRESULT ret = this->pReal->GetDefaultAudioEndpoint(dataFlow, role, ppEndpoint);

    // check for failure
    if (FAILED(ret)) {
        log_warning("audio", "IMMDeviceEnumerator::GetDefaultAudioEndpoint failed, hr={}", FMT_HRESULT(ret));
        return ret;
    }

    // dump friendly name of audio endpoint
    // https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-properties?redirectedfrom=MSDN
    {
        IPropertyStore *pProps = nullptr;
        HRESULT hr;

        hr = (*ppEndpoint)->OpenPropertyStore(STGM_READ, &pProps);
        if (SUCCEEDED(hr)) {
            PROPVARIANT varName;

            // instead of including giant windows headers, hardcoding the GUID here
            PROPERTYKEY key;
            GUID IDevice_FriendlyName = { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } };
            key.pid = 14;
            key.fmtid = IDevice_FriendlyName;

            // get friendly name
            PropVariantInit(&varName);
            hr = pProps->GetValue(key, &varName);
            if (SUCCEEDED(hr) && varName.vt != VT_EMPTY) {
                log_info("audio", "IMMDeviceEnumerator::GetDefaultAudioEndpoint: using {}", ws2s(varName.pwszVal));
            }

            // cleanup
            PropVariantClear(&varName);
        }
        if (pProps) {
            pProps->Release();
            pProps = nullptr;
        }
    }

    // wrap interface
    *ppEndpoint = new WrappedIMMDevice(*ppEndpoint);

    // return original result
    return ret;
}
HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::GetDevice(
    LPCWSTR pwstrId,
    IMMDevice **ppDevice)
{
    return pReal->GetDevice(pwstrId, ppDevice);
}
HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::RegisterEndpointNotificationCallback(
    IMMNotificationClient *pClient)
{
    return pReal->RegisterEndpointNotificationCallback(pClient);
}
HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::UnregisterEndpointNotificationCallback(
    IMMNotificationClient *pClient)
{
    return pReal->UnregisterEndpointNotificationCallback(pClient);
}
