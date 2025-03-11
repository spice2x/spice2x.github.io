#include <initguid.h>

#include "fake_device.h"

HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::QueryInterface(
        REFIID riid,
        void **ppvObj)
{
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_IDirectInputDeviceA ||
        riid == IID_IDirectInputDeviceW ||
        riid == IID_IDirectInputDevice2A ||
        riid == IID_IDirectInputDevice2W ||
        riid == IID_IDirectInputDevice7A ||
        riid == IID_IDirectInputDevice7W ||
        riid == IID_IDirectInputDevice8A ||
        riid == IID_IDirectInputDevice8W)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE FakeIDirectInputDevice8W::AddRef() {
    return ++this->ref_count;
}
ULONG STDMETHODCALLTYPE FakeIDirectInputDevice8W::Release() {
    ULONG refs = --this->ref_count;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetCapabilities(LPDIDEVCAPS lpDIDevCaps) {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::EnumObjects(
        LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback,
        LPVOID pvRef,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetProperty(
        REFGUID rguidProp,
        LPDIPROPHEADER pdiph)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SetProperty(
        REFGUID rguidProp,
        LPCDIPROPHEADER pdiph)
{
    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::Acquire() {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::Unacquire() {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetDeviceState(
        DWORD cbData,
        LPVOID lpvData)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetDeviceData(
        DWORD cbObjectData,
        LPDIDEVICEOBJECTDATA rgdod,
        LPDWORD pdwInOut,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SetDataFormat(LPCDIDATAFORMAT lpdf) {
    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SetEventNotification(HANDLE hEvent) {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SetCooperativeLevel(
        HWND hWnd,
        DWORD dwFlags)
{
    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetObjectInfo(
        LPDIDEVICEOBJECTINSTANCEW pdidoi,
        DWORD dwObj,
        DWORD dwHow)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetDeviceInfo(
        LPDIDEVICEINSTANCEW pdidi)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::RunControlPanel(
        HWND hwndOwner,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::Initialize(
        HINSTANCE hinst,
        DWORD dwVersion,
        REFGUID rguid)
{
    return DIERR_INVALIDPARAM;
}

HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::CreateEffect(
        REFGUID rguid,
        LPCDIEFFECT lpeff,
        LPDIRECTINPUTEFFECT *ppdeff,
        LPUNKNOWN punkOuter)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::EnumEffects(
        LPDIENUMEFFECTSCALLBACKW lpCallback,
        LPVOID pvRef,
        DWORD dwEffType)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetEffectInfo(
        LPDIEFFECTINFOW pdei,
        REFGUID rguid)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetForceFeedbackState(LPDWORD pdwOut) {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SendForceFeedbackCommand(DWORD dwFlags) {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::EnumCreatedEffectObjects(
        LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback,
        LPVOID pvRef,
        DWORD fl)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::Escape(LPDIEFFESCAPE pesc) {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::Poll() {
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SendDeviceData(
        DWORD cbObjectData,
        LPCDIDEVICEOBJECTDATA rgdod,
        LPDWORD pdwInOut,
        DWORD fl)
{
    return DIERR_INVALIDPARAM;
}

HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::EnumEffectsInFile(
        LPCWSTR lpszFileName,
        LPDIENUMEFFECTSINFILECALLBACK pec,
        LPVOID pvRef,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::WriteEffectToFile(
        LPCWSTR lpszFileName,
        DWORD dwEntries,
        LPDIFILEEFFECT rgDiFileEft,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}

HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::BuildActionMap(
        LPDIACTIONFORMATW lpdiaf,
        LPCWSTR lpszUserName,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::SetActionMap(
        LPDIACTIONFORMATW lpdiaf,
        LPCWSTR lpszUserName,
        DWORD dwFlags)
{
    return DIERR_INVALIDPARAM;
}
HRESULT STDMETHODCALLTYPE FakeIDirectInputDevice8W::GetImageInfo(
        LPDIDEVICEIMAGEINFOHEADERW lpdiDevImageInfoHeader)
{
    return DIERR_INVALIDPARAM;
}
