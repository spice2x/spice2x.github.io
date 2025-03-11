#include "dsound_backend.h"

#include <dsound.h>
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"

static void *DIRECT_SOUND_CREATE8_ADR = nullptr;
static char DIRECT_SOUND_CREATE8_CONTENTS[16];
static decltype(DirectSoundCreate8) *DirectSoundCreate8_orig = nullptr;

static HRESULT WINAPI DirectSoundCreate8_hook(LPCGUID lpGUID, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN pUnkOuter) {
    log_misc("audio::dsound", "DirectSoundCreate8 hook hit");

    // remove hook
    if (DIRECT_SOUND_CREATE8_ADR) {
        detour::inline_restore(DIRECT_SOUND_CREATE8_ADR, DIRECT_SOUND_CREATE8_CONTENTS);

        if (DirectSoundCreate8_orig == nullptr) {
            DirectSoundCreate8_orig = reinterpret_cast<decltype(DirectSoundCreate8) *>(DIRECT_SOUND_CREATE8_ADR);
        }
    }

    HRESULT result = DirectSoundCreate8_orig(lpGUID, ppDS8, pUnkOuter);
    if (result != DS_OK) {
        log_warning("audio::dsound", "failed to create DirectSound interface");

        return result;
    }

    *ppDS8 = new WrappedIDirectSound8(*ppDS8);

    // add hook
    if (DIRECT_SOUND_CREATE8_ADR) {
        detour::inline_noprotect((void *) DirectSoundCreate8_hook, DIRECT_SOUND_CREATE8_ADR);
    }

    return result;
}

#pragma region WrappedIDirectSound8
__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::QueryInterface(const IID &riid, void **ppvObject) {
    return pReal->QueryInterface(riid, ppvObject);
}

__declspec (nothrow) ULONG WINAPI WrappedIDirectSound8::AddRef() {
    return pReal->AddRef();
}

__declspec (nothrow) ULONG WINAPI WrappedIDirectSound8::Release() {
    return pReal->Release();
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::CreateSoundBuffer(
        LPCDSBUFFERDESC lpcDSBufferDesc,
        LPLPDIRECTSOUNDBUFFER lplpDirectSoundBuffer,
        IUnknown *pUnkOuter)
{
    HRESULT result = pReal->CreateSoundBuffer(lpcDSBufferDesc, lplpDirectSoundBuffer, pUnkOuter);
    if (result != DS_OK) {
        log_warning("audio::dsound", "failed to create sound buffer");

        return result;
    }

    *lplpDirectSoundBuffer = new WrappedIDirectSoundBuffer(*lplpDirectSoundBuffer);

    return result;
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::GetCaps(LPDSCAPS lpDSCaps) {
    return pReal->GetCaps(lpDSCaps);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::DuplicateSoundBuffer(
        LPDIRECTSOUNDBUFFER lpDsbOriginal,
        LPLPDIRECTSOUNDBUFFER lplpDsbDuplicate)
{
    return pReal->DuplicateSoundBuffer(lpDsbOriginal, lplpDsbDuplicate);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::SetCooperativeLevel(HWND hwnd, DWORD dwLevel) {
    return pReal->SetCooperativeLevel(hwnd, dwLevel);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::Compact() {
    return pReal->Compact();
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::GetSpeakerConfig(LPDWORD lpdwSpeakerConfig) {
    return pReal->GetSpeakerConfig(lpdwSpeakerConfig);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::SetSpeakerConfig(DWORD dwSpeakerConfig) {
    return pReal->SetSpeakerConfig(dwSpeakerConfig);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::Initialize(LPCGUID lpcGuid) {
    return pReal->Initialize(lpcGuid);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSound8::VerifyCertification(LPDWORD pdwCertified) {
    return pReal->VerifyCertification(pdwCertified);
}
#pragma endregion

#pragma region WrappedIDirectSoundBuffer
__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::QueryInterface(const IID &riid, void **ppvObject) {
    return pReal->QueryInterface(riid, ppvObject);
}

__declspec (nothrow) ULONG WINAPI WrappedIDirectSoundBuffer::AddRef() {
    return pReal->AddRef();
}

__declspec (nothrow) ULONG WINAPI WrappedIDirectSoundBuffer::Release() {
    return pReal->Release();
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps) {
    return pReal->GetCaps(lpDSBufferCaps);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetCurrentPosition(
        LPDWORD lpdwCurrentPlayCursor,
        LPDWORD lpdwCurrentWriteCursor)
{
    return pReal->GetCurrentPosition(lpdwCurrentPlayCursor, lpdwCurrentWriteCursor);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetFormat(
        LPWAVEFORMATEX lpwfxFormat,
        DWORD dwSizeAllocated,
        LPDWORD lpdwSizeWritten)
{
    return pReal->GetFormat(lpwfxFormat, dwSizeAllocated, lpdwSizeWritten);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetVolume(LPLONG lplVolume) {
    return pReal->GetVolume(lplVolume);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetPan(LPLONG lplpan) {
    return pReal->GetPan(lplpan);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetFrequency(LPDWORD lpdwFrequency) {
    return pReal->GetFrequency(lpdwFrequency);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::GetStatus(LPDWORD lpdwStatus) {
    return pReal->GetStatus(lpdwStatus);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::Initialize(
        LPDIRECTSOUND lpDirectSound,
        LPCDSBUFFERDESC lpcDSBufferDesc)
{
    return pReal->Initialize(lpDirectSound, lpcDSBufferDesc);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::Lock(
        DWORD dwOffset,
        DWORD dwBytes,
        LPVOID *ppvAudioPtr1,
        LPDWORD pdwAudioBytes1,
        LPVOID *ppvAudioPtr2,
        LPDWORD pdwAudioBytes2,
        DWORD dwFlags)
{
    return pReal->Lock(dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1, ppvAudioPtr2, pdwAudioBytes2, dwFlags);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::Play(
        DWORD dwReserved1,
        DWORD dwReserved2,
        DWORD dwFlags)
{
    return pReal->Play(dwReserved1, dwReserved2, dwFlags);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::SetCurrentPosition(DWORD dwNewPosition) {
    return pReal->SetCurrentPosition(dwNewPosition);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::SetFormat(LPCWAVEFORMATEX lpcfxFormat) {
    HRESULT result = pReal->SetFormat(lpcfxFormat);
    // for KBR, MBR
    if (result == DSERR_ALLOCATED) {
        log_info("audio::dsound", "WrappedIDirectSoundBuffer::SetFormat returned DSERR_ALLOCATED");
        return DS_OK;
    }

    return result;
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::SetVolume(LONG lVolume) {
    return pReal->SetVolume(lVolume);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::SetPan(LONG lPan) {
    return pReal->SetPan(lPan);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::SetFrequency(DWORD dwFrequency) {
    return pReal->SetFrequency(dwFrequency);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::Stop() {
    return pReal->Stop();
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::Unlock(
        LPVOID pvAudioPtr1,
        DWORD dwAudioBytes1,
        LPVOID pvAudioPtr2,
        DWORD dwAudioPtr2)
{
    return pReal->Unlock(pvAudioPtr1, dwAudioBytes1, pvAudioPtr2, dwAudioPtr2);
}

__declspec (nothrow) HRESULT WINAPI WrappedIDirectSoundBuffer::Restore() {
    return pReal->Restore();
}
#pragma endregion

void audio_dsound_init() {
    log_info("audio::dsound", "initializing");

    // dsound inline hooks
    HMODULE dsound = libutils::try_module("dsound.dll");
    if (!dsound) {
        log_info("audio::dsound", "skipping inline hooks");
    } else {
        DIRECT_SOUND_CREATE8_ADR = (void *) libutils::get_proc(dsound, "DirectSoundCreate8");
        detour::inline_preserve(reinterpret_cast<void *>(DirectSoundCreate8_hook), DIRECT_SOUND_CREATE8_ADR, DIRECT_SOUND_CREATE8_CONTENTS);
    }

    DirectSoundCreate8_orig = detour::iat_try("DirectSoundCreate8", DirectSoundCreate8_hook);
}