#include <initguid.h>

#include "fake_backend.h"

#include <dinput.h>

#include "util/logging.h"

#include "fake_device.h"

HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::QueryInterface(
        REFIID riid,
        void **ppvObj)
{
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_IDirectInput8A ||
        riid == IID_IDirectInput8W)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE FakeIDirectInput8W::AddRef() {
    return ++this->ref_count;
}
ULONG STDMETHODCALLTYPE FakeIDirectInput8W::Release() {
    ULONG refs = --this->ref_count;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::CreateDevice(
        REFGUID rguid,
        LPDIRECTINPUTDEVICE8W *lplpDirectInputDevice,
        LPUNKNOWN pUnkOuter)
{
    log_misc("input::dinput8", "IDirectInput8::CreateDevice hook hit");

    if (lplpDirectInputDevice == nullptr) {
        return DIERR_INVALIDPARAM;
    }

    if (rguid == GUID_SysKeyboard ||
        rguid == GUID_SysMouse)
    {
        log_misc("input::dinput8", "returning stub device");

        *lplpDirectInputDevice = new FakeIDirectInputDevice8W();

        return DI_OK;
    }

    return DIERR_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::EnumDevices(
        DWORD dwDevType,
        LPDIENUMDEVICESCALLBACKW lpCallback,
        LPVOID pvRef,
        DWORD dwFlags)
{
    log_misc("input::dinput8", "IDirectInput8::EnumDevices hook hit");

    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::GetDeviceStatus(REFGUID rguidInstance) {
    return DIERR_GENERIC;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::RunControlPanel(HWND hwndOwner, DWORD dwFlags) {
    return DIERR_GENERIC;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::Initialize(HINSTANCE hinst, DWORD dwVersion) {
    log_misc("input::dinput8", "IDirectInput8::Initialize({}, 0x{:x})",
            fmt::ptr(hinst),
            dwVersion);

    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::FindDevice(
        REFGUID rguid,
        LPCWSTR pszName,
        LPGUID pguidInstance)
{
    return DIERR_GENERIC;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::EnumDevicesBySemantics(
        LPCWSTR ptszUserName,
        LPDIACTIONFORMATW lpdiActionFormat,
        LPDIENUMDEVICESBYSEMANTICSCBW lpCallback,
        LPVOID pvRef,
        DWORD dwFlags)
{
    return DIERR_GENERIC;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInput8W::ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK lpdiCallback,
        LPDICONFIGUREDEVICESPARAMSW lpdiCDParams,
        DWORD dwFlags,
        LPVOID pvRefData)
{
    return DIERR_GENERIC;
}
