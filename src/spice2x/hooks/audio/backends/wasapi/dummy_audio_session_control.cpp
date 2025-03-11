#include "dummy_audio_session_control.h"

#include <algorithm>

#include "dummy_audio_client.h"
#include "wasapi_private.h"

#if 1
#define WRAP_DEBUG log_misc("audio::wasapi", "{}::{}", CLASS_NAME, __func__)
#else
#define WRAP_DEBUG do {} while (0)
#endif

const char CLASS_NAME[] = "DummyIAudioSessionControl";

// IUnknown
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_DummyIAudioSessionControl ||
        riid == IID_IAudioSessionControl)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE DummyIAudioSessionControl::AddRef() {
    return ++this->ref_cnt;
}
ULONG STDMETHODCALLTYPE DummyIAudioSessionControl::Release() {
    ULONG refs = --this->ref_cnt;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioSessionControl
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::GetState(AudioSessionState *pRetVal) {
    CHECK_RESULT(E_NOTIMPL);
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::GetDisplayName(LPWSTR *pRetVal) {
    WRAP_DEBUG;

    if (!pRetVal) {
        return E_POINTER;
    }

    auto length = this->display_name.length();
    auto value = reinterpret_cast<LPWSTR>(CoTaskMemAlloc(length + 1));

    if (!value) {
        CHECK_RESULT(E_OUTOFMEMORY);
    }

    memcpy(value, this->display_name.c_str(), length);
    value[length] = L'\0';

    *pRetVal = value;

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::SetDisplayName(
    LPCWSTR Value,
    LPCGUID EventContext)
{
    WRAP_DEBUG;

    if (!Value) {
        return E_POINTER;
    }

    this->display_name = std::wstring(Value);

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::GetIconPath(LPWSTR *pRetVal) {
    WRAP_DEBUG;

    if (!pRetVal) {
        return E_POINTER;
    }

    auto length = this->icon_path.length();
    auto value = reinterpret_cast<LPWSTR>(CoTaskMemAlloc(length + 1));

    if (!value) {
        CHECK_RESULT(E_OUTOFMEMORY);
    }

    memcpy(value, this->icon_path.c_str(), length);
    value[length] = L'\0';

    *pRetVal = value;

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::SetIconPath(
    LPCWSTR Value,
    LPCGUID EventContext)
{
    WRAP_DEBUG;

    if (!Value) {
        return E_POINTER;
    }

    this->icon_path = std::wstring(Value);

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::GetGroupingParam(GUID *pRetVal) {
    WRAP_DEBUG;

    if (!pRetVal) {
        return E_POINTER;
    }

    memcpy(pRetVal, &this->grouping_param, sizeof(this->grouping_param));

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::SetGroupingParam(
    LPCGUID Override,
    LPCGUID EventContext)
{
    WRAP_DEBUG;

    if (!Override) {
        return E_POINTER;
    }

    memcpy(&this->grouping_param, Override, sizeof(this->grouping_param));

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::RegisterAudioSessionNotification(
    IAudioSessionEvents *NewNotifications)
{
    WRAP_DEBUG;

    if (!NewNotifications) {
        return E_POINTER;
    }

    this->client->session_notification_handlers.emplace_back(NewNotifications);

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioSessionControl::UnregisterAudioSessionNotification(
    IAudioSessionEvents *NewNotifications)
{
    WRAP_DEBUG;

    if (!NewNotifications) {
        return E_POINTER;
    }

    this->client->session_notification_handlers.erase(
        std::remove(
            this->client->session_notification_handlers.begin(),
            this->client->session_notification_handlers.end(),
            NewNotifications
        ),
        this->client->session_notification_handlers.end());

    return S_OK;
}
