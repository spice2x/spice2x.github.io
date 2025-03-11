#include "dummy_audio_clock.h"

#include "hooks/audio/backends/wasapi/dummy_audio_client.h"

#include "wasapi_private.h"

const char CLASS_NAME[] = "DummyIAudioClock";

// IUnknown
HRESULT STDMETHODCALLTYPE DummyIAudioClock::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_DummyIAudioClock ||
        riid == IID_IAudioClock)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE DummyIAudioClock::AddRef() {
    return ++this->ref_cnt;
}
ULONG STDMETHODCALLTYPE DummyIAudioClock::Release() {
    ULONG refs = --this->ref_cnt;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioClock
HRESULT STDMETHODCALLTYPE DummyIAudioClock::GetFrequency(UINT64 *pu64Frequency) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "DummyIAudioClock::GetFrequency");
    });

    if (!pu64Frequency) {
        return E_POINTER;
    }

    *pu64Frequency = static_cast<UINT64>(this->backend->format().Format.nSamplesPerSec);

    return S_OK;
}
HRESULT STDMETHODCALLTYPE DummyIAudioClock::GetPosition(
    UINT64 *pu64Position,
    UINT64 *pu64QPCPosition)
{
    CHECK_RESULT(E_NOTIMPL);
}
HRESULT STDMETHODCALLTYPE DummyIAudioClock::GetCharacteristics(DWORD *pdwCharacteristics) {
    CHECK_RESULT(E_NOTIMPL);
}
