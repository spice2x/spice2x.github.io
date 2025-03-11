#pragma once

#include <initguid.h>
#include <audioclient.h>

struct WrappedIAudioClient;

// {1CB6ABEE-1181-4FF7-8449-1CA18C2109E3}
static const GUID IID_WrappedIAudioRenderClient = {
    0x1cb6abee, 0x1181, 0x4ff7, { 0x84, 0x49, 0x1c, 0xa1, 0x8c, 0x21, 0x09, 0xe3 }
};

struct WrappedIAudioRenderClient : IAudioRenderClient {
    explicit WrappedIAudioRenderClient(WrappedIAudioClient *client, IAudioRenderClient *orig) : pReal(orig), client(client) {
    }

    WrappedIAudioRenderClient(const WrappedIAudioRenderClient &) = delete;
    WrappedIAudioRenderClient &operator=(const WrappedIAudioRenderClient &) = delete;

    virtual ~WrappedIAudioRenderClient() = default;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAudioClient
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT32 NumFramesRequested, BYTE **ppData) override;
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 NumFramesWritten, DWORD dwFlags) override;
#pragma endregion

    IAudioRenderClient *const pReal;
    WrappedIAudioClient *const client;

    int buffers_to_mute = 16;
    BYTE *audio_buffer = nullptr;
};
