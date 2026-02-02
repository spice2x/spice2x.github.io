#include <mutex>

#include "device_enumerator.h"
#include "device_collection.h"
#include "device.h"
#include "util/utils.h"
#include "hooks/audio/audio.h"

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

IMMDevice *WrappedIMMDeviceEnumerator::get_default_device() {
    IMMDeviceCollection *ppDevices = nullptr;
    IMMDevice *pDevice = nullptr;

    const auto hr = pReal->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &ppDevices);
    if (FAILED(hr)) {
        return nullptr;
    }

    // get count
    UINT count = 0;
    ppDevices->GetCount(&count);
    if (FAILED(hr) || count == 0) {
        ppDevices->Release();
        return nullptr;
    }

    log_info(
        "audio",
        "looking for audio output device that matches `{}` (-defaultaudio)",
        hooks::audio::DEFAULT_IMM_DEVICE_ID.value());

    // first, iterate through devices and match on intance ID
    for (UINT i = 0; i < count; i++) {
        if (FAILED(ppDevices->Item(i, &pDevice))) {
            continue;
        }

        const std::string device_id = get_device_id(pDevice);
        if (device_id.find(hooks::audio::DEFAULT_IMM_DEVICE_ID.value()) != std::string::npos) {
            log_info(
                "audio",
                "found matching device for -defaultaudio option from ID match: [{}] {}",
                i,
                device_id);
            break;
        }

        // not matched, clean up
        pDevice->Release();
        pDevice = nullptr;
    }

    // second, iterate again to match on friendly name 
    if (pDevice == nullptr) {
        for (UINT i = 0; i < count; i++) {
            if (FAILED(ppDevices->Item(i, &pDevice))) {
                continue;
            }

            const std::string friendly_name = get_device_friendly_name(pDevice);
            if (friendly_name.find(hooks::audio::DEFAULT_IMM_DEVICE_ID.value()) != std::string::npos) {
                log_info(
                    "audio",
                    "found matching device for -defaultaudio option from friendly name match: [{}] {}",
                    i,
                    friendly_name);
                break;
            }

            // not matched, clean up
            pDevice->Release();
            pDevice = nullptr;
        }
    }

    if (pDevice == nullptr) {
        log_fatal(
            "audio",
            "could not find any device matches this ID `{}`; check -defaultaudio option and try again",
            hooks::audio::DEFAULT_IMM_DEVICE_ID.value());
    }

    ppDevices->Release();
    return pDevice;
}

void WrappedIMMDeviceEnumerator::dump_devices() {
    IMMDeviceCollection *ppDevices = nullptr;
    
    const auto hr = pReal->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &ppDevices);
    if (FAILED(hr)) {
        return;
    }

    UINT count = 0;
    ppDevices->GetCount(&count);
    if (FAILED(hr) || count == 0) {
        ppDevices->Release();
        return;
    }
    log_info("audio", "found {} active audio devices from EnumAudioEndpoints", count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice *pDevice;
        if (SUCCEEDED(ppDevices->Item(i, &pDevice))) {
            log_info("audio", "    device [{}]:", i);
            log_info("audio", "        device ID: {}", get_device_id(pDevice));
            log_info("audio", "        friendly name: {}", get_device_friendly_name(pDevice));
            pDevice->Release();
        }
    }

    ppDevices->Release();
    return;
}

std::string WrappedIMMDeviceEnumerator::get_device_id(IMMDevice *pDevice) {
    LPWSTR id = nullptr;
    if (SUCCEEDED(pDevice->GetId(&id))) {
        std::string result = ws2s(std::wstring(id));
        CoTaskMemFree(id);
        return result;
    }
    return "";
}

std::string WrappedIMMDeviceEnumerator::get_device_friendly_name(IMMDevice *pDevice) {
    // dump friendly name of audio endpoint
    // https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-properties?redirectedfrom=MSDN
    IPropertyStore *pProps = nullptr;
    HRESULT hr;

    hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (SUCCEEDED(hr)) {
        PROPVARIANT varName;
        PROPERTYKEY key;

        // from functiondiscoverykeys_devpkey.h
        // instead of including giant windows headers, hardcoding the GUID here
        // get friendly name
        GUID IDevice_FriendlyName = { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }; // 14
        key.pid = 14;
        key.fmtid = IDevice_FriendlyName;
        PropVariantInit(&varName);
        hr = pProps->GetValue(key, &varName);
        if (SUCCEEDED(hr) && varName.vt != VT_EMPTY) {
            std::string result = ws2s(varName.pwszVal);
            PropVariantClear(&varName);
            pProps->Release();
            return result;
        }
        PropVariantClear(&varName);
    }
    if (pProps) {
        pProps->Release();
    }
    return "";
}

HRESULT STDMETHODCALLTYPE WrappedIMMDeviceEnumerator::GetDefaultAudioEndpoint(
    EDataFlow dataFlow,
    ERole role,
    IMMDevice **ppEndpoint)
{

    if (dataFlow == eRender && (role == eMultimedia || role == eConsole)) {
        static std::once_flag printed;
        std::call_once(printed, [&]() {
            dump_devices();
        });

        // user override
        if (hooks::audio::DEFAULT_IMM_DEVICE_ID.has_value()) {
            auto device = get_default_device();
            if (device != nullptr) {
                // wrap interface
                device = new WrappedIMMDevice(device);
                log_info(
                    "audio",
                    "IMMDeviceEnumerator::GetDefaultAudioEndpoint is returning user-preferred device: {} / {}",
                    get_device_id(device),
                    get_device_friendly_name(device));
                *ppEndpoint = device;
                return S_OK;
            }
            // if not found, fallthrough to normal behavior and call
            // the real GetDefaultAudioEndpoint below
        }
    }

    // call orignal
    HRESULT ret = this->pReal->GetDefaultAudioEndpoint(dataFlow, role, ppEndpoint);

    // check for failure
    if (FAILED(ret)) {
        log_warning("audio", "IMMDeviceEnumerator::GetDefaultAudioEndpoint failed, hr={}", FMT_HRESULT(ret));
        return ret;
    }

    log_info(
        "audio",
        "IMMDeviceEnumerator::GetDefaultAudioEndpoint is returning system default device: {} / {}",
        get_device_id(*ppEndpoint),
        get_device_friendly_name(*ppEndpoint));

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
