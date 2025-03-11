#include "d3d9_fake_swapchain.h"

#include <cassert>
#include <mutex>

#include "util/logging.h"

#if 1
#define WRAP_VERBOSE log_misc("graphics::d3d9", "FakeIDirect3DSwapChain9::{}", __FUNCTION__)
#define WRAP_VERBOSE_FMT(format, ...) log_misc("graphics::d3d9", format, __VA_ARGS__)
#else
#define WRAP_VERBOSE
#define WRAP_VERBOSE_FMT(format, ...)
#endif

// IDirect3DSwapChain9
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_IUnknown ||
        riid == IID_IDirect3DSwapChain9 ||
        riid == IID_IDirect3DSwapChain9Ex)
    {
#pragma region Update to IDirect3DSwapChain9Ex interface
        if (!is_d3d9ex && riid == IID_IDirect3DSwapChain9Ex) {
            is_d3d9ex = true;
        }
#pragma endregion

        AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE FakeIDirect3DSwapChain9::AddRef(void) {
    return ++this->ref_cnt;
}
ULONG STDMETHODCALLTYPE FakeIDirect3DSwapChain9::Release(void) {
    ULONG refs = --this->ref_cnt;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::Present(const RECT *pSourceRect, const RECT *pDestRect,
        HWND hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "FakeIDirect3DSwapChain9::Present");
    });

    return D3DERR_INVALIDCALL;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetFrontBufferData(IDirect3DSurface9 *pDestSurface) {
    WRAP_VERBOSE;
    return D3DERR_INVALIDCALL;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type,
        IDirect3DSurface9 **ppBackBuffer)
{
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "FakeIDirect3DSwapChain9::GetBackBuffer");
    });

    if (iBackBuffer >= render_targets.size() || Type != D3DBACKBUFFER_TYPE_MONO || !ppBackBuffer) {
        return D3DERR_INVALIDCALL;
    }

    auto &render_target = render_targets[iBackBuffer];

    render_target->AddRef();
    *ppBackBuffer = render_target;

    return D3D_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) {
    WRAP_VERBOSE;
    return D3DERR_INVALIDCALL;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetDisplayMode(D3DDISPLAYMODE *pMode) {
    WRAP_VERBOSE;
    return D3DERR_INVALIDCALL;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetDevice(IDirect3DDevice9 **ppDevice) {
    WRAP_VERBOSE;

    if (ppDevice == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    pDev->AddRef();
    *ppDevice = pDev;

    return D3D_OK;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetPresentParameters(
        D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    WRAP_VERBOSE;
    return D3DERR_INVALIDCALL;
}

// IDirect3DSwapChain9Ex
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetLastPresentCount(UINT *pLastPresentCount) {
    assert(is_d3d9ex);
    return D3DERR_INVALIDCALL;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetPresentStats(D3DPRESENTSTATS *pPresentationStatistics) {
    assert(is_d3d9ex);
    return D3DERR_INVALIDCALL;
}
HRESULT STDMETHODCALLTYPE FakeIDirect3DSwapChain9::GetDisplayModeEx(D3DDISPLAYMODEEX *pMode,
        D3DDISPLAYROTATION *pRotation)
{
    WRAP_VERBOSE;

    assert(is_d3d9ex);
    return D3DERR_INVALIDCALL;
}
