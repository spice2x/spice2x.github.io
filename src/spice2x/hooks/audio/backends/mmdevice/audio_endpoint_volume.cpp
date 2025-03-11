#include "audio_endpoint_volume.h"
#include "util/logging.h"

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == __uuidof(IAudioEndpointVolume)) {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIAudioEndpointVolume::AddRef() {
    return pReal->AddRef();
}

ULONG STDMETHODCALLTYPE WrappedIAudioEndpointVolume::Release() {

    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::SetMasterVolumeLevelScalar(float fLevel, LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::SetMasterVolumeLevelScalar called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::RegisterControlChangeNotify(IAudioEndpointVolumeCallback *pNotify) {
    return pReal->RegisterControlChangeNotify(pNotify);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::UnregisterControlChangeNotify(IAudioEndpointVolumeCallback *pNotify) {
    return pReal->UnregisterControlChangeNotify(pNotify);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetChannelCount(uint32_t *pnChannelCount) {
    return pReal->GetChannelCount(pnChannelCount);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::SetMasterVolumeLevel(float fLevelDB, LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::SetMasterVolumeLevel called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetMasterVolumeLevel(float *fLevelDB) {
    return pReal->GetMasterVolumeLevel(fLevelDB);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetMasterVolumeLevelScalar(float *fLevel) {
    return pReal->GetMasterVolumeLevelScalar(fLevel);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::SetChannelVolumeLevel(uint32_t nChannel, float fLevelDB, LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::SetChannelVolumeLevel called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::SetChannelVolumeLevelScalar(uint32_t nChannel, float fLevel, LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::SetChannelVolumeLevelScalar called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetChannelVolumeLevel(uint32_t nChannel, float *fLevelDB) {
    return pReal->GetChannelVolumeLevel(nChannel, fLevelDB);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetChannelVolumeLevelScalar(uint32_t nChannel, float *fLevel) {
    return pReal->GetChannelVolumeLevelScalar(nChannel, fLevel);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::SetMute(WINBOOL bMute, LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::SetMute called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetMute(WINBOOL *bMute) {
    return pReal->GetMute(bMute);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetVolumeStepInfo(uint32_t *pnStep, uint32_t *pnStepCount) {
    return pReal->GetVolumeStepInfo(pnStep, pnStepCount);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::VolumeStepUp(LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::VolumeStepUp called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::VolumeStepDown(LPCGUID pguidEventContext) {
    log_misc("audio", "WrappedIAudioEndpointVolume::VolumeStepDown called; ignoring volume change");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::QueryHardwareSupport(DWORD *pdwHardwareSupportMask) {
    return pReal->QueryHardwareSupport(pdwHardwareSupportMask);
}

HRESULT STDMETHODCALLTYPE WrappedIAudioEndpointVolume::GetVolumeRange(float *pflVolumeMindB, float *pflVolumeMaxdB, float *pflVolumeIncrementdB) {
    return pReal->GetVolumeRange(pflVolumeMindB, pflVolumeMaxdB, pflVolumeIncrementdB);
}
