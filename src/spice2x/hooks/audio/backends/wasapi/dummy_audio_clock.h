#pragma once

#include <atomic>

#include <initguid.h>
#include <audiopolicy.h>

struct AudioBackend;

// {8AE52B4A-ACC4-420C-9169-BA8AF07A251F}
static const GUID IID_DummyIAudioClock = {
    0x8ae52b4a, 0xacc4, 0x420c, { 0x91, 0x69, 0xba, 0x8a, 0xf0, 0x7a, 0x25, 0x1f }
};

struct DummyIAudioClock : IAudioClock {
    explicit DummyIAudioClock(AudioBackend *backend) : backend(backend) {
    }

    DummyIAudioClock(const DummyIAudioClock &) = delete;
    DummyIAudioClock &operator=(const DummyIAudioClock &) = delete;

    virtual ~DummyIAudioClock() = default;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAudioClock
    HRESULT STDMETHODCALLTYPE GetFrequency(UINT64 *pu64Frequency) override;
    HRESULT STDMETHODCALLTYPE GetPosition(UINT64 *pu64Position, UINT64 *pu64QPCPosition) override;
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD *pdwCharacteristics) override;
#pragma endregion

    AudioBackend *const backend;

    std::atomic<ULONG> ref_cnt = 1;
};
