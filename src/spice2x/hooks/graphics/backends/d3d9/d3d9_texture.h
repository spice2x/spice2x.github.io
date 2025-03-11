#pragma once

#include <d3d9.h>

#include "util/logging.h"

// {22E9B203-6506-4BC5-B304-A48F3001630F}
static const GUID IID_WrappedIDirect3DTexture9 = {
    0x22e9b203, 0x6506, 0x4bc5, { 0xb3, 0x04, 0xa4, 0x8f, 0x30, 0x01, 0x63, 0x0f }
};

struct WrappedIDirect3DTexture9 : IDirect3DTexture9 {
    explicit WrappedIDirect3DTexture9(IDirect3DDevice9 *dev, IDirect3DTexture9 *orig) : pDev(dev), pReal(orig) {
        log_misc("graphics::d3d9::texture", "Creating texture wrapper around {} => {}", fmt::ptr(orig), fmt::ptr(this));
    }

    WrappedIDirect3DTexture9(const WrappedIDirect3DTexture9 &) = delete;
    WrappedIDirect3DTexture9 &operator=(const WrappedIDirect3DTexture9 &) = delete;

    virtual ~WrappedIDirect3DTexture9() = default;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef(void) override;
    virtual ULONG STDMETHODCALLTYPE Release(void) override;
#pragma endregion

#pragma region IDirect3DResource9
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, const void *pData, DWORD SizeOfData, DWORD Flags) override;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData, DWORD* pSizeOfData) override;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
    virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
    virtual DWORD STDMETHODCALLTYPE GetPriority(void) override;
    virtual void STDMETHODCALLTYPE PreLoad(void) override;
    virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType(void) override;
#pragma endregion

#pragma region IDirect3DBaseTexture9
    virtual DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override;
    virtual DWORD STDMETHODCALLTYPE GetLOD(void) override;
    virtual DWORD STDMETHODCALLTYPE GetLevelCount(void) override;
    virtual HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override;
    virtual D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType(void) override;
    virtual void STDMETHODCALLTYPE GenerateMipSubLevels(void) override;
#pragma endregion

#pragma region IDirect3DTexture9
    virtual HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    virtual HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) override;
    virtual HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;
    virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *pDirtyRect) override;
#pragma endregion

    IDirect3DDevice9 *const pDev;
    IDirect3DTexture9 *const pReal;
};
