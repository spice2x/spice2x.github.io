#pragma once

#include <stdint.h>
#include <endpointvolume.h>

struct WrappedIAudioEndpointVolume : IAudioEndpointVolume {
    explicit WrappedIAudioEndpointVolume(IAudioEndpointVolume *orig) : pReal(orig) {}

    WrappedIAudioEndpointVolume(const WrappedIAudioEndpointVolume &) = delete;
    WrappedIAudioEndpointVolume &operator=(const WrappedIAudioEndpointVolume &) = delete;

    virtual ~WrappedIAudioEndpointVolume() = default;

#pragma region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IAudioEndpointVolume
    HRESULT STDMETHODCALLTYPE RegisterControlChangeNotify(IAudioEndpointVolumeCallback *pNotify) override;
    HRESULT STDMETHODCALLTYPE UnregisterControlChangeNotify(IAudioEndpointVolumeCallback *pNotify) override;
    HRESULT STDMETHODCALLTYPE GetChannelCount(uint32_t *pnChannelCount) override;
    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevel(float fLevelDB, LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevelScalar(float fLevel, LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevel(float *fLevelDB) override;
    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevelScalar(float *fLevel) override;
    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevel(uint32_t nChannel, float fLevelDB, LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevelScalar(uint32_t nChannel, float fLevel, LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevel(uint32_t nChannel, float *fLevelDB) override;
    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevelScalar(uint32_t nChannel, float *fLevel) override;
    HRESULT STDMETHODCALLTYPE SetMute(WINBOOL bMute, LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE GetMute(WINBOOL *bMute) override;
    HRESULT STDMETHODCALLTYPE GetVolumeStepInfo(uint32_t *pnStep, uint32_t *pnStepCount) override;
    HRESULT STDMETHODCALLTYPE VolumeStepUp(LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE VolumeStepDown(LPCGUID pguidEventContext) override;
    HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD *pdwHardwareSupportMask) override;
    HRESULT STDMETHODCALLTYPE GetVolumeRange(float *pflVolumeMindB, float *pflVolumeMaxdB, float *pflVolumeIncrementdB) override;
#pragma endregion

private:
    IAudioEndpointVolume *const pReal;
};