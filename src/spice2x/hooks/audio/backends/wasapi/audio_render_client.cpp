#include "audio_render_client.h"

#include "audio_client.h"
#include "wasapi_private.h"

const char CLASS_NAME[] = "WrappedIAudioRenderClient";

HRESULT STDMETHODCALLTYPE WrappedIAudioRenderClient::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIAudioRenderClient ||
        riid == IID_IAudioRenderClient)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}
ULONG STDMETHODCALLTYPE WrappedIAudioRenderClient::AddRef() {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIAudioRenderClient::Release() {

    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioRenderClient
HRESULT STDMETHODCALLTYPE WrappedIAudioRenderClient::GetBuffer(UINT32 NumFramesRequested, BYTE **ppData) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioRenderClient::GetBuffer");
    });

    if (this->client->backend) {
        SAFE_CALL("AudioBackend", "on_get_buffer", this->client->backend->on_get_buffer(
                NumFramesRequested,
                ppData));

        return S_OK;
    }

    // call original
    HRESULT ret = pReal->GetBuffer(NumFramesRequested, ppData);

    // store buffer reference
    if (SUCCEEDED(ret)) {
        this->audio_buffer = *ppData;
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioRenderClient::ReleaseBuffer(UINT32 NumFramesWritten, DWORD dwFlags) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioRenderClient::ReleaseBuffer");
    });

    if (this->client->backend) {
        SAFE_CALL("AudioBackend", "on_release_buffer", this->client->backend->on_release_buffer(
                NumFramesWritten,
                dwFlags));

        return S_OK;
    }

    // fix for audio pop effect
    if (this->buffers_to_mute > 0 && this->client->frame_size > 0) {

        // zero out = mute
        memset(this->audio_buffer, 0, NumFramesWritten * this->client->frame_size);

        this->buffers_to_mute--;
    }

    CHECK_RESULT(pReal->ReleaseBuffer(NumFramesWritten, dwFlags));
}
