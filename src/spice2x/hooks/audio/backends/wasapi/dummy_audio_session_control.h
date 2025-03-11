#pragma once

#include <atomic>
#include <string>

#include <initguid.h>
#include <audiopolicy.h>

struct DummyIAudioClient;

// {5412A875-C82F-451F-B29A-0E18DB1CDFA2}
static const GUID IID_DummyIAudioSessionControl = {
    0x5412a875, 0xc82f, 0x451f, { 0xb2, 0x9a, 0x0e, 0x18, 0xdb, 0x1c, 0xdf, 0xa2 }
};

struct DummyIAudioSessionControl : IAudioSessionControl {
    explicit DummyIAudioSessionControl(DummyIAudioClient *client) : client(client) {
    }

    DummyIAudioSessionControl(const DummyIAudioSessionControl &) = delete;
    DummyIAudioSessionControl &operator=(const DummyIAudioSessionControl &) = delete;

    virtual ~DummyIAudioSessionControl() = default;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAudioSessionControl
    HRESULT STDMETHODCALLTYPE GetState(AudioSessionState *pRetVal) override;
    HRESULT STDMETHODCALLTYPE GetDisplayName(LPWSTR *pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetDisplayName(LPCWSTR Value, LPCGUID EventContext) override;
    HRESULT STDMETHODCALLTYPE GetIconPath(LPWSTR *pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetIconPath(LPCWSTR Value, LPCGUID EventContext) override;
    HRESULT STDMETHODCALLTYPE GetGroupingParam(GUID *pRetVal) override;
    HRESULT STDMETHODCALLTYPE SetGroupingParam(LPCGUID Override, LPCGUID EventContext) override;
    HRESULT STDMETHODCALLTYPE RegisterAudioSessionNotification(IAudioSessionEvents *NewNotifications) override;
    HRESULT STDMETHODCALLTYPE UnregisterAudioSessionNotification(IAudioSessionEvents *NewNotifications) override;
#pragma endregion

    DummyIAudioClient *const client;

    std::atomic<ULONG> ref_cnt = 1;

    std::wstring display_name = L"Dummy Audio Device";
    std::wstring icon_path = L"";
    GUID grouping_param = GUID_NULL;
};