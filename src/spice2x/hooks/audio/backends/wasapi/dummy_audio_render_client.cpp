#include "dummy_audio_render_client.h"

#include "dummy_audio_client.h"
#include "wasapi_private.h"

const char CLASS_NAME[] = "DummyIAudioRenderClient";

HRESULT STDMETHODCALLTYPE DummyIAudioRenderClient::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_DummyIAudioRenderClient ||
        riid == IID_IAudioRenderClient)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE DummyIAudioRenderClient::AddRef() {
    return ++this->ref_cnt;
}
ULONG STDMETHODCALLTYPE DummyIAudioRenderClient::Release() {
    ULONG refs = --this->ref_cnt;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioRenderClient
HRESULT STDMETHODCALLTYPE DummyIAudioRenderClient::GetBuffer(UINT32 NumFramesRequested, BYTE **ppData) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioRenderClient::GetBuffer");
    });

    CHECK_RESULT(this->client->backend->on_get_buffer(
            NumFramesRequested,
            ppData));
}
HRESULT STDMETHODCALLTYPE DummyIAudioRenderClient::ReleaseBuffer(UINT32 NumFramesWritten, DWORD dwFlags) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioRenderClient::ReleaseBuffer");
    });

    CHECK_RESULT(this->client->backend->on_release_buffer(
            NumFramesWritten,
            dwFlags));
}
