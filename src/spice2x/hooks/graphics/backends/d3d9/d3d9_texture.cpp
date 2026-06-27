#include "d3d9_texture.h"

#include "hooks/graphics/graphics.h"
#include "util/logging.h"
#include "util/utils.h"

#if 0
#define WRAP_DEBUG log_misc("graphics::d3d9::texture", "{}", __FUNCTION__)
#define WRAP_DEBUG_FMT(format, ...) log_misc("graphics::d3d9::texture", format, __VA_ARGS__)
#else
#define WRAP_DEBUG do {} while (0)
#define WRAP_DEBUG_FMT(format, ...) do {} while (0)
#endif

#define CHECK_RESULT(x) \
    HRESULT ret = (x); \
    if (GRAPHICS_LOG_HRESULT && FAILED(ret)) [[unlikely]] { \
        log_warning("graphics::d3d9::texture", "{} failed, hr={}", __FUNCTION__, FMT_HRESULT(ret)); \
    } \
    return ret;

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::QueryInterface(REFIID riid, void **ppvObj) {
#ifndef __GNUC__

    // fast path without incrementing the reference count for texture updates
    if (riid == IID_WrappedIDirect3DTexture9) {
        return S_OK;
    }
#endif

    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (/* riid == IID_IUnknown || */
        riid == IID_IDirect3DResource9 ||
        riid == IID_IDirect3DBaseTexture9 ||
        riid == IID_IDirect3DTexture9)
    {
        this->AddRef();
        *ppvObj = this;

        log_info("graphics::d3d9::texture", "WrappedIDirect3DTexture9::QueryInterface({})", guid2s(riid));

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DTexture9::AddRef(void) {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIDirect3DTexture9::Release(void) {
    ULONG refs = (pReal != nullptr) ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IDirect3DResource9 methods
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetDevice(IDirect3DDevice9 **ppDevice) {
    CHECK_RESULT(pReal->GetDevice(ppDevice));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetPrivateData(REFGUID refguid, const void *pData, DWORD SizeOfData,
        DWORD Flags)
{
    CHECK_RESULT(pReal->SetPrivateData(refguid, pData, SizeOfData, Flags));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetPrivateData(REFGUID refguid, void *pData, DWORD* pSizeOfData) {
    CHECK_RESULT(pReal->GetPrivateData(refguid, pData, pSizeOfData));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::FreePrivateData(REFGUID refguid) {
    CHECK_RESULT(pReal->FreePrivateData(refguid));
}
DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetPriority(DWORD PriorityNew) {
    return pReal->SetPriority(PriorityNew);
}
DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetPriority(void) {
    return pReal->GetPriority();
}
void STDMETHODCALLTYPE WrappedIDirect3DTexture9::PreLoad(void) {
    return pReal->PreLoad();
}
D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetType(void) {
    return pReal->GetType();
}

// IDirect3DBaseTexture9 methods
DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetLOD(DWORD LODNew) {
    return pReal->SetLOD(LODNew);
}
DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetLOD(void) {
    return pReal->GetLOD();
}
DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetLevelCount(void) {
    return pReal->GetLevelCount();
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) {
    CHECK_RESULT(pReal->SetAutoGenFilterType(FilterType));
}
D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetAutoGenFilterType(void) {
    return pReal->GetAutoGenFilterType();
}
void STDMETHODCALLTYPE WrappedIDirect3DTexture9::GenerateMipSubLevels(void) {
    return pReal->GenerateMipSubLevels();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) {
    CHECK_RESULT(pReal->GetLevelDesc(Level, pDesc));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) {
    CHECK_RESULT(pReal->GetSurfaceLevel(Level, ppSurfaceLevel));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) {
    WRAP_DEBUG_FMT("LockRect({}, {}, {}, 0x{:x})", Level, fmt::ptr(pLockedRect), fmt::ptr(pRect), Flags);
    CHECK_RESULT(pReal->LockRect(Level, pLockedRect, pRect, Flags));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::UnlockRect(UINT Level) {
    CHECK_RESULT(pReal->UnlockRect(Level));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::AddDirtyRect(const RECT *pDirtyRect) {
    CHECK_RESULT(pReal->AddDirtyRect(pDirtyRect));
}
