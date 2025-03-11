#pragma once

#include <atomic>

#include <initguid.h>
#include <audioclient.h>

struct DummyIAudioClient;

// {453BF965-DDA4-4234-8846-F02BA5E874B7}
static const GUID IID_DummyIAudioRenderClient = {
    0x453bf965, 0xdda4, 0x4234, { 0x88, 0x46, 0xf0, 0x2b, 0xa5, 0xe8, 0x74, 0xb7 }
};

struct DummyIAudioRenderClient : IAudioRenderClient {
    explicit DummyIAudioRenderClient(DummyIAudioClient *client) : client(client) {
    }

    DummyIAudioRenderClient(const DummyIAudioRenderClient &) = delete;
    DummyIAudioRenderClient &operator=(const DummyIAudioRenderClient &) = delete;

    virtual ~DummyIAudioRenderClient() = default;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAudioClient
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT32 NumFramesRequested, BYTE **ppData) override;
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 NumFramesWritten, DWORD dwFlags) override;
#pragma endregion

    std::atomic<ULONG> ref_cnt = 1;

    DummyIAudioClient *const client;
};
