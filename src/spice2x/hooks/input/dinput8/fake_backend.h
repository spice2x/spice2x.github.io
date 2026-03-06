#pragma once

#include <atomic>

#include <dinput.h>

struct FakeIDirectInput8W : IDirectInput8W {
    explicit FakeIDirectInput8W() {
    }

    FakeIDirectInput8W(const FakeIDirectInput8W &) = delete;
    FakeIDirectInput8W &operator=(const FakeIDirectInput8W &) = delete;

    virtual ~FakeIDirectInput8W() = default;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) noexcept override;
    virtual ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    virtual ULONG STDMETHODCALLTYPE Release() noexcept override;
#pragma endregion

#pragma region IDirectInput8W
    virtual HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8W *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE EnumDevices(DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguidInstance) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE FindDevice(REFGUID rguid, LPCWSTR pszName, LPGUID pguidInstance) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCWSTR ptszUserName, LPDIACTIONFORMATW lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBW lpCallback, LPVOID pvRef, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSW lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) noexcept override;
#pragma endregion

private:
    std::atomic<ULONG> ref_count = 1;
};
