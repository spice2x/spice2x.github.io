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
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IDirectInput8W
    virtual HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8W *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) override;
    virtual HRESULT STDMETHODCALLTYPE EnumDevices(DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguidInstance) override;
    virtual HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion) override;
    virtual HRESULT STDMETHODCALLTYPE FindDevice(REFGUID rguid, LPCWSTR pszName, LPGUID pguidInstance) override;
    virtual HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCWSTR ptszUserName, LPDIACTIONFORMATW lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBW lpCallback, LPVOID pvRef, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSW lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) override;
#pragma endregion

private:
    std::atomic<ULONG> ref_count = 1;
};
