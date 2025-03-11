#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

void audio_dsound_init();

struct WrappedIDirectSound8 : IDirectSound8 {
    explicit WrappedIDirectSound8(IDirectSound8 *orig) : pReal(orig) {}

    WrappedIDirectSound8(const WrappedIDirectSound8 &) = delete;
    WrappedIDirectSound8 &operator=(const WrappedIDirectSound8 &) = delete;

    virtual ~WrappedIDirectSound8() = default;

#pragma region IUnknown
    __declspec (nothrow) HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override;
    __declspec (nothrow) ULONG WINAPI AddRef() override;
    __declspec (nothrow) ULONG WINAPI Release() override;
#pragma endregion

#pragma region IDirectSound8 methods
    __declspec (nothrow) HRESULT WINAPI CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc, LPLPDIRECTSOUNDBUFFER lplpDirectSoundBuffer, IUnknown *pUnkOuter) override;
    __declspec (nothrow) HRESULT WINAPI GetCaps(LPDSCAPS lpDSCaps) override;
    __declspec (nothrow) HRESULT WINAPI DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDsbOriginal, LPLPDIRECTSOUNDBUFFER lplpDsbDuplicate) override;
    __declspec (nothrow) HRESULT WINAPI SetCooperativeLevel(HWND hwnd, DWORD dwLevel) override;
    __declspec (nothrow) HRESULT WINAPI Compact() override;
    __declspec (nothrow) HRESULT WINAPI GetSpeakerConfig(LPDWORD lpdwSpeakerConfig) override;
    __declspec (nothrow) HRESULT WINAPI SetSpeakerConfig(DWORD dwSpeakerConfig) override;
    __declspec (nothrow) HRESULT WINAPI Initialize(LPCGUID lpcGuid) override;
    __declspec (nothrow) HRESULT WINAPI VerifyCertification(LPDWORD pdwCertified) override;
#pragma endregion
    
private:
    IDirectSound8 *pReal;
};

struct WrappedIDirectSoundBuffer : IDirectSoundBuffer {
    explicit WrappedIDirectSoundBuffer(IDirectSoundBuffer *orig) : pReal(orig) {}

    WrappedIDirectSoundBuffer(const WrappedIDirectSound8 &) = delete;
    WrappedIDirectSoundBuffer &operator=(const WrappedIDirectSoundBuffer &) = delete;

    virtual ~WrappedIDirectSoundBuffer() = default;

#pragma region IUnknown
    __declspec (nothrow) HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override;
    __declspec (nothrow) ULONG WINAPI AddRef() override;
    __declspec (nothrow) ULONG WINAPI Release() override;
#pragma endregion

#pragma region IDirectSoundBuffer methods
    __declspec (nothrow) HRESULT WINAPI GetCaps(LPDSBCAPS lpDSBufferCaps) override;
    __declspec (nothrow) HRESULT WINAPI GetCurrentPosition(LPDWORD lpdwCurrentPlayCursor, LPDWORD lpdwCurrentWriteCursor) override;
    __declspec (nothrow) HRESULT WINAPI GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated, LPDWORD lpdwSizeWritten) override;
    __declspec (nothrow) HRESULT WINAPI GetVolume(LPLONG lplVolume) override;
    __declspec (nothrow) HRESULT WINAPI GetPan(LPLONG lplpan) override;
    __declspec (nothrow) HRESULT WINAPI GetFrequency(LPDWORD lpdwFrequency) override;
    __declspec (nothrow) HRESULT WINAPI GetStatus(LPDWORD lpdwStatus) override;
    __declspec (nothrow) HRESULT WINAPI Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc) override;
    __declspec (nothrow) HRESULT WINAPI Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1, LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags) override;
    __declspec (nothrow) HRESULT WINAPI Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags) override;
    __declspec (nothrow) HRESULT WINAPI SetCurrentPosition(DWORD dwNewPosition) override;
    __declspec (nothrow) HRESULT WINAPI SetFormat(LPCWAVEFORMATEX lpcfxFormat) override;
    __declspec (nothrow) HRESULT WINAPI SetVolume(LONG lVolume) override;
    __declspec (nothrow) HRESULT WINAPI SetPan(LONG lPan) override;
    __declspec (nothrow) HRESULT WINAPI SetFrequency(DWORD dwFrequency) override;
    __declspec (nothrow) HRESULT WINAPI Stop() override;
    __declspec (nothrow) HRESULT WINAPI Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioPtr2) override;
    __declspec (nothrow) HRESULT WINAPI Restore() override;
#pragma endregion

private:
    IDirectSoundBuffer *pReal;
};
