#include "d3d9_swapchain.h"

#include <cassert>
#include <mutex>

#include "avs/game.h"
#include "hooks/graphics/graphics.h"

#include "d3d9_backend.h"
#include "d3d9_device.h"

// std::min
#ifdef min
#undef min
#endif

#define CHECK_RESULT(x) \
    HRESULT ret = (x); \
    if (GRAPHICS_LOG_HRESULT && FAILED(ret)) [[unlikely]] { \
        log_warning("graphics::d3d9", "{} failed, hr={}", __FUNCTION__, FMT_HRESULT(ret)); \
    } \
    return ret

// inert unless this is the SMALL head and it is being scaled onto a differently sized monitor
static bool scales_small_head(const WrappedIDirect3DSwapChain9 *chain) {
    return chain->native_group_head == WrappedIDirect3DSwapChain9::NativeGroupHead::Small
            && chain->pDev->gfdm_small_head.scaled();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if ((riid == IID_IUnknown && pDev->is_gfdm_two_head_exclusive()) ||
        riid == IID_IDirect3DSwapChain9 ||
        riid == IID_IDirect3DSwapChain9Ex)
    {
#pragma region Update to IDirect3DSwapChain9Ex interface
        if (!is_d3d9ex && riid == IID_IDirect3DSwapChain9Ex) {
            IDirect3DSwapChain9Ex *swapchainex = nullptr;

            if (FAILED(pReal->QueryInterface(IID_PPV_ARGS(&swapchainex)))) {
                return E_NOINTERFACE;
            }

            pReal->Release();
            pReal = swapchainex;
            is_d3d9ex = true;
        }
#pragma endregion

        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::AddRef(void) {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::Release(void) {
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    // Metal Gear Arcade expects the swap chain to only have one reference. The parent
    // `WrappedIDirect3DDevice9` holds a strong reference to this swap chain which means
    // the reference count will be above one. Workaround this by returning a maximum of one.
    if (avs::game::is_model("I36")) {
        return std::min(refs, 1lu);
    }

    return refs;
}

/*
 * IDirect3DSwapChain9
 */

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::Present(const RECT *pSourceRect, const RECT *pDestRect,
        HWND hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "WrappedIDirect3DSwapChain9::Present");
    });

    if (should_run_hooks) {
        graphics_d3d9_on_present(pDev->hFocusWindow, pDev->pReal, pDev);
    }

    if (scales_small_head(this)) {
        pDev->gfdm_small_head.compose(pDev->pReal);
    }

    HRESULT result = pReal->Present(
            pSourceRect,
            pDestRect,
            hDestWindowOverride,
            pDirtyRegion,
            dwFlags);

    // a successful game subscreen present suppresses the next forced fallback present.
    if (SUCCEEDED(result) &&
        (this == pDev->implicit_sub_swapchain || this == pDev->sub_swapchain[0])) {
        graphics_d3d9_notify_subscreen_present();
    }

    // Some drivers report S_PRESENT_MODE_CHANGED after Windows moves the portrait SMALL
    // head into the requested exclusive mode, leaving both group heads black. Toggle SMALL
    // through another supported mode and restore it once, then retry the interrupted MAIN
    // present. Recovery is restricted to the creation thread and consumes its armed state,
    // so subsequent presents cannot repeat the mode switch.
    if (pDev->is_gfdm_two_head_exclusive()
            && native_group_head == NativeGroupHead::Main
            && result == S_PRESENT_MODE_CHANGED)
    {
        HRESULT recovery_failure = D3D_OK;
        const auto recovery = pDev->gfdm_recover_present_mode_change(
                &recovery_failure);
        if (recovery
                == WrappedIDirect3DDevice9::GfdmPresentModeRecoveryResult::ReadyToRetry)
        {
            result = pReal->Present(
                    pSourceRect,
                    pDestRect,
                    hDestWindowOverride,
                    pDirtyRegion,
                    dwFlags);
        } else if (recovery
                        == WrappedIDirect3DDevice9::GfdmPresentModeRecoveryResult::Failed
                && FAILED(recovery_failure))
        {
            result = recovery_failure;
        }
    }
    CHECK_RESULT(result);
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetFrontBufferData(IDirect3DSurface9 *pDestSurface) {
    CHECK_RESULT(pReal->GetFrontBufferData(pDestSurface));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type,
        IDirect3DSurface9 **ppBackBuffer)
{
    if (scales_small_head(this) && iBackBuffer == 0 && Type == D3DBACKBUFFER_TYPE_MONO) {
        CHECK_RESULT(pDev->gfdm_small_head.backbuffer(pDev->pReal, ppBackBuffer));
    }

    CHECK_RESULT(pReal->GetBackBuffer(iBackBuffer, Type, ppBackBuffer));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) {
    CHECK_RESULT(pReal->GetRasterStatus(pRasterStatus));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetDisplayMode(D3DDISPLAYMODE *pMode) {
    CHECK_RESULT(pReal->GetDisplayMode(pMode));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetDevice(IDirect3DDevice9 **ppDevice) {
    if (ppDevice == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    pDev->AddRef();
    *ppDevice = pDev;

    return D3D_OK;
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetPresentParameters(
        D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    HRESULT result = pReal->GetPresentParameters(pPresentationParameters);
    if (SUCCEEDED(result) && pPresentationParameters != nullptr && scales_small_head(this)) {
        pDev->gfdm_small_head.apply_logical_size(
                &pPresentationParameters->BackBufferWidth,
                &pPresentationParameters->BackBufferHeight);
    }

    CHECK_RESULT(result);
}

/*
 * IDirect3DSwapChain9Ex
 */

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetLastPresentCount(UINT *pLastPresentCount) {
    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DSwapChain9Ex *>(pReal)->GetLastPresentCount(pLastPresentCount));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetPresentStats(D3DPRESENTSTATS *pPresentationStatistics) {
    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DSwapChain9Ex *>(pReal)->GetPresentStats(pPresentationStatistics));
}
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetDisplayModeEx(D3DDISPLAYMODEEX *pMode,
        D3DDISPLAYROTATION *pRotation)
{
    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DSwapChain9Ex *>(pReal)->GetDisplayModeEx(pMode, pRotation));
}
