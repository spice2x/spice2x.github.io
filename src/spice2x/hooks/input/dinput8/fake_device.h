#pragma once

#include <atomic>

#include <dinput.h>

struct FakeIDirectInputDevice8W : IDirectInputDevice8W {
    explicit FakeIDirectInputDevice8W() {
    }

    FakeIDirectInputDevice8W(const FakeIDirectInputDevice8W &) = delete;
    FakeIDirectInputDevice8W &operator=(const FakeIDirectInputDevice8W &) = delete;

    virtual ~FakeIDirectInputDevice8W() = default;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) noexcept override;
    virtual ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    virtual ULONG STDMETHODCALLTYPE Release() noexcept override;
#pragma endregion

#pragma region IDirectInputDeviceW
    virtual HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS lpDIDevCaps) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE Acquire() noexcept override;
    virtual HRESULT STDMETHODCALLTYPE Unacquire() noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT lpdf) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE hEvent) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hWnd, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW pdidi) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) noexcept override;
#pragma endregion

#pragma region IDirectInputDevice2W
    virtual HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW pdei, REFGUID rguid) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD pdwOut) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE Poll() noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) noexcept override;
#pragma endregion

#pragma region IDirectInputDevice7W
    virtual HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags) noexcept override;
#pragma endregion

#pragma region IDirectInputDevice8W
    virtual HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW lpdiDevImageInfoHeader) noexcept override;
#pragma endregion

private:
    std::atomic<ULONG> ref_count = 1;
};
