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
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IDirectInputDeviceW
    virtual HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS lpDIDevCaps) override;
    virtual HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph) override;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph) override;
    virtual HRESULT STDMETHODCALLTYPE Acquire() override;
    virtual HRESULT STDMETHODCALLTYPE Unacquire() override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData) override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT lpdf) override;
    virtual HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE hEvent) override;
    virtual HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hWnd, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow) override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW pdidi) override;
    virtual HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) override;
#pragma endregion

#pragma region IDirectInputDevice2W
    virtual HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) override;
    virtual HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType) override;
    virtual HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW pdei, REFGUID rguid) override;
    virtual HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD pdwOut) override;
    virtual HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) override;
    virtual HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc) override;
    virtual HRESULT STDMETHODCALLTYPE Poll() override;
    virtual HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) override;
#pragma endregion

#pragma region IDirectInputDevice7W
    virtual HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags) override;
#pragma endregion

#pragma region IDirectInputDevice8W
    virtual HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags) override;
    virtual HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW lpdiDevImageInfoHeader) override;
#pragma endregion

private:
    std::atomic<ULONG> ref_count = 1;
};
