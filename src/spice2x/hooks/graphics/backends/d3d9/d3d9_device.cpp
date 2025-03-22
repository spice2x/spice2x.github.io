#include "d3d9_device.h"

#include <cassert>
#include <mutex>

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "util/flags_helper.h"
#include "cfg/screen_resize.h"

#include "d3d9_backend.h"
#include "d3d9_texture.h"

#ifndef SPICE64
#include "shaders/vertex_shader.h"
#endif

#define CHECK_RESULT_FMT(x, fmt, ...) \
    HRESULT __ret = (x); \
    if (GRAPHICS_LOG_HRESULT && FAILED(__ret)) [[unlikely]] { \
        log_warning("graphics::d3d9", "{} failed, hr={} " fmt, __FUNCTION__, FMT_HRESULT(__ret), ## __VA_ARGS__); \
    } \
    return __ret

#define CHECK_RESULT(x) \
    HRESULT __ret = (x); \
    if (GRAPHICS_LOG_HRESULT && FAILED(__ret)) [[unlikely]] { \
        log_warning("graphics::d3d9", "{} failed, hr={}", __FUNCTION__, FMT_HRESULT(__ret)); \
    } \
    return __ret

// TODO: At some point, support a manual `Reset` call by reloading all textures
constexpr bool CUSTOM_RESET = false;

constexpr D3DFORMAT D3DFMT_DF24 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'F', '2', '4'));

std::string usage2s(DWORD dwUsage) {
    FLAGS_START(dwUsage);
    FLAG(dwUsage, D3DUSAGE_RENDERTARGET);
    FLAG(dwUsage, D3DUSAGE_DEPTHSTENCIL);
    FLAG(dwUsage, D3DUSAGE_WRITEONLY);
    FLAG(dwUsage, D3DUSAGE_SOFTWAREPROCESSING);
    FLAG(dwUsage, D3DUSAGE_DONOTCLIP);
    FLAG(dwUsage, D3DUSAGE_POINTS);
    FLAG(dwUsage, D3DUSAGE_RTPATCHES);
    FLAG(dwUsage, D3DUSAGE_NPATCHES);
    FLAG(dwUsage, D3DUSAGE_DYNAMIC);
    FLAG(dwUsage, D3DUSAGE_AUTOGENMIPMAP);
    FLAG(dwUsage, D3DUSAGE_DMAP);
    FLAGS_END(dwUsage);
}

std::string pool2s(D3DPOOL Pool) {
    switch (Pool) {
        ENUM_VARIANT(D3DPOOL_DEFAULT);
        ENUM_VARIANT(D3DPOOL_MANAGED);
        ENUM_VARIANT(D3DPOOL_SYSTEMMEM);
        ENUM_VARIANT(D3DPOOL_SCRATCH);
        default:
            return fmt::format("Pool(0x{:08x})", Pool);
    }
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::QueryInterface(
        REFIID riid,
        void **ppvObj)
{
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIDirect3DDevice9 ||
        riid == IID_IDirect3DDevice9 ||
        riid == IID_IDirect3DDevice9Ex)
    {
        // update to IDirect3DDevice9Ex interface
        if (!is_d3d9ex && riid == IID_IDirect3DDevice9Ex) {
            IDirect3DDevice9Ex *deviceex = nullptr;

            HRESULT ret = pReal->QueryInterface(IID_PPV_ARGS(&deviceex));
            if (FAILED(ret) || deviceex == nullptr) {
                if (ret != E_NOINTERFACE) {
                    log_warning("graphics::d3d9",
                            "failed to upgrade to IDirect3DDevice9Ex, hr={}",
                            FMT_HRESULT(ret));
                }
                return E_NOINTERFACE;
            }

            pReal->Release();
            pReal = deviceex;
            is_d3d9ex = true;
        }

        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9::AddRef() {
    this->refs++;

    return pReal->AddRef();
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9::Release() {

    // get what this thread thinks the reference count is
    auto local_refs = --this->refs;

    // release owned objects if there are no more references
    if (local_refs == 0) {
        if (this->main_swapchain) {
            this->main_swapchain->Release();
            this->main_swapchain = nullptr;
        }
        if (this->sub_swapchain) {
            this->sub_swapchain->Release();
            this->sub_swapchain = nullptr;
        }
        if (this->fake_sub_swapchain) {
            this->fake_sub_swapchain->Release();
            this->fake_sub_swapchain = nullptr;
        }

        if (overlay::ENABLED) {
            const std::lock_guard<std::mutex> lock(overlay::OVERLAY_MUTEX);

            // release overlay if we are the backing device which will release its owned references
            if (overlay::OVERLAY && overlay::OVERLAY->uses_device(this->pReal)) {
                overlay::OVERLAY.reset();
            }
        }
    }

    // get reference count of underlying interface
    ULONG refs = this->pReal != nullptr ? this->pReal->Release() : 0;

    // the reference counts should be equivalent at this point, so log if they are not
    if (local_refs == 0 && refs != 0) {
        log_warning("graphics::d3d9",
                "reference count for `IDirect3DDevice9` object {} wrapping {} "
                "is inconsistent: {}, but expected 0",
                fmt::ptr(this),
                fmt::ptr(this->pReal),
                refs);

        refs = 0;
    }

    if (refs == 0) {
        delete this;
    }

    return refs;
}

/*
 * IDirect3DDevice9
 */

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::TestCooperativeLevel() {
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->TestCooperativeLevel());
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetAvailableTextureMem() {
    WRAP_DEBUG;
    return pReal->GetAvailableTextureMem();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EvictManagedResources() {
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->EvictManagedResources());
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDirect3D(
        IDirect3D9 **ppD3D9)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->GetDirect3D(ppD3D9));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDeviceCaps(
        D3DCAPS9 *pCaps)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->GetDeviceCaps(pCaps));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDisplayMode(
        UINT iSwapChain,
        D3DDISPLAYMODE *pMode)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->GetDisplayMode(iSwapChain, pMode));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetCreationParameters(
        D3DDEVICE_CREATION_PARAMETERS *pParameters)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->GetCreationParameters(pParameters));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetCursorProperties(
        UINT XHotSpot,
        UINT YHotSpot,
        IDirect3DSurface9 *pCursorBitmap)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap));
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetCursorPosition(
        int X,
        int Y,
        DWORD Flags)
{
    WRAP_VERBOSE;
    return pReal->SetCursorPosition(X, Y, Flags);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9::ShowCursor(
        BOOL bShow)
{
    WRAP_VERBOSE;
    return pReal->ShowCursor(bShow);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateAdditionalSwapChain(
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        IDirect3DSwapChain9 **ppSwapChain)
{
    WRAP_VERBOSE;

    HRESULT hr = pReal->CreateAdditionalSwapChain(pPresentationParameters, ppSwapChain);

    if (avs::game::is_model({"LDJ", "KFC"})) {
        if (SUCCEEDED(hr) && !sub_swapchain) {
            sub_swapchain = new WrappedIDirect3DSwapChain9(this, *ppSwapChain);
            sub_swapchain->should_run_hooks = false;
        } else if (FAILED(hr) && !fake_sub_swapchain) {
            log_warning("graphics::d3d9",
                    "failed to create sub swap chain, hr={}, using fake swap chain",
                    FMT_HRESULT(hr));

            fake_sub_swapchain = new FakeIDirect3DSwapChain9(this, pPresentationParameters, false);
            fake_sub_swapchain->AddRef();
            *ppSwapChain = static_cast<IDirect3DSwapChain9 *>(fake_sub_swapchain);

            return D3D_OK;
        }
    }

    CHECK_RESULT(hr);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetSwapChain(
        UINT iSwapChain,
        IDirect3DSwapChain9 **ppSwapChain)
{
    WRAP_VERBOSE_FMT("GetSwapChain({})", iSwapChain);

    if (iSwapChain == 0) {
        if (!main_swapchain) {
            HRESULT ret = pReal->GetSwapChain(iSwapChain, ppSwapChain);
            if (FAILED(ret)) {
                log_warning("graphics::d3d9", "failed to get swapchain {}, hr={}",
                        iSwapChain, FMT_HRESULT(ret));
                return ret;
            }

            main_swapchain = new WrappedIDirect3DSwapChain9(this, *ppSwapChain);
        }

        main_swapchain->AddRef();
        *ppSwapChain = static_cast<IDirect3DSwapChain9 *>(main_swapchain);

        graphics_screens_register(iSwapChain);
        return D3D_OK;
    }

    if (iSwapChain == 1 && avs::game::is_model({"LDJ", "KFC"})) {
        if (sub_swapchain) {
            sub_swapchain->AddRef();
            *ppSwapChain = static_cast<IDirect3DSwapChain9 *>(sub_swapchain);

            graphics_screens_register(iSwapChain);
            return D3D_OK;
        } else if (fake_sub_swapchain) {
            fake_sub_swapchain->AddRef();
            *ppSwapChain = static_cast<IDirect3DSwapChain9 *>(fake_sub_swapchain);

            graphics_screens_register(iSwapChain);
            return D3D_OK;
        }
    }

    HRESULT ret = pReal->GetSwapChain(iSwapChain, ppSwapChain);
    if (GRAPHICS_LOG_HRESULT && FAILED(ret)) {
        log_warning("graphics::d3d9", "{} failed, hr={}", __FUNCTION__, FMT_HRESULT(ret));
    } else {
        graphics_screens_register(iSwapChain);
    }
    return ret;
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetNumberOfSwapChains() {
    WRAP_VERBOSE;

    UINT n = pReal->GetNumberOfSwapChains();

    if (sub_swapchain && avs::game::is_model({"LDJ", "KFC"})) {
        n += 1;
    }

    return n;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Reset(
        D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    log_misc("graphics::d3d9", "WrappedIDirect3DDevice9::Reset");

    if (pPresentationParameters) {
        if (GRAPHICS_WINDOWED) {
            pPresentationParameters->Windowed = true;
            pPresentationParameters->FullScreen_RefreshRateInHz = 0;
        } else if (GRAPHICS_FORCE_REFRESH > 0) {
            pPresentationParameters->FullScreen_RefreshRateInHz = GRAPHICS_FORCE_REFRESH;
        }
    }

    // reset overlay
    if (overlay::OVERLAY && overlay::OVERLAY->uses_device(pReal)) {
        overlay::OVERLAY->reset_invalidate();
    }

    HRESULT res = pReal->Reset(pPresentationParameters);

    // recreate overlay
    if (overlay::OVERLAY && overlay::OVERLAY->uses_device(pReal) && SUCCEEDED(res)) {
        overlay::OVERLAY->reset_recreate();
    }

    CHECK_RESULT(res);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Present(
        const RECT *pSourceRect,
        const RECT *pDestRect,
        HWND hDestWindowOverride,
        const RGNDATA *pDirtyRegion)
{
    WRAP_DEBUG;

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "WrappedIDirect3DDevice9::Present");
    });

    graphics_d3d9_on_present(hFocusWindow, pReal, this);

    CHECK_RESULT(pReal->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetBackBuffer(
        UINT iSwapChain,
        UINT iBackBuffer,
        D3DBACKBUFFER_TYPE Type,
        IDirect3DSurface9 **ppBackBuffer)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRasterStatus(
        UINT iSwapChain,
        D3DRASTER_STATUS *pRasterStatus)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetRasterStatus(iSwapChain, pRasterStatus));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetDialogBoxMode(
        BOOL bEnableDialogs)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetDialogBoxMode(bEnableDialogs));
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetGammaRamp(
        UINT iSwapChain,
        DWORD Flags,
        const D3DGAMMARAMP *pRamp)
{
    WRAP_DEBUG;
    pReal->SetGammaRamp(iSwapChain, Flags, pRamp);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetGammaRamp(
        UINT iSwapChain,
        D3DGAMMARAMP *pRamp)
{
    WRAP_DEBUG;
    pReal->GetGammaRamp(iSwapChain, pRamp);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateTexture(
        UINT Width,
        UINT Height,
        UINT Levels,
        DWORD Usage,
        D3DFORMAT Format,
        D3DPOOL Pool,
        IDirect3DTexture9 **ppTexture,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateTexture({}, {}, {}, {}, {}, {})",
            Width, Height, Levels, usage2s(Usage), Format, pool2s(Pool));

    // fix nvidia proprietary depthstencil texture format
    if (avs::game::is_model("I36") && (Usage & D3DUSAGE_DEPTHSTENCIL) && Format == D3DFMT_DF24) {
        HRESULT ret = pReal->CreateTexture(Width, Height, Levels,
                Usage, Format, Pool, ppTexture, pSharedHandle);

        if (FAILED(ret)) {
            Format = D3DFMT_D24S8;
        } else {
            return ret;
        }
    }

#ifndef SPICE64
    // texture fix for iidx (iix18 resort anthem, iidx19 lincle)
    if (avs::game::is_model({ "JDZ", "KDZ" })) {

        // patch texture format type from 15-bit+alpha to 32-bit.
        if (Width == 256 && Height == 256 && Levels == 1 && Usage == D3DUSAGE_RENDERTARGET
        && Format == D3DFMT_A1R5G5B5 && Pool == D3DPOOL_DEFAULT && !pSharedHandle) {
            Format = D3DFMT_A8R8G8B8;
        }
    }
#endif

    HRESULT res = pReal->CreateTexture(Width, Height, Levels,
            Usage, Format, Pool, ppTexture, pSharedHandle);

    if (CUSTOM_RESET && ppTexture && Pool == D3DPOOL_MANAGED && SUCCEEDED(res)) {
        *ppTexture = new WrappedIDirect3DTexture9(this, *ppTexture);
    }

    CHECK_RESULT_FMT(res, "({}, {}, {}, {}, {}, {})",
            Width, Height, Levels, usage2s(Usage), Format, pool2s(Pool));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVolumeTexture(
        UINT Width,
        UINT Height,
        UINT Depth,
        UINT Levels,
        DWORD Usage,
        D3DFORMAT Format,
        D3DPOOL Pool,
        IDirect3DVolumeTexture9 **ppVolumeTexture,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateVolumeTexture({}, {}, {}, {}, {}, {}, {})",
            Width, Height, Depth, Levels, usage2s(Usage), Format, pool2s(Pool));

    CHECK_RESULT(pReal->CreateVolumeTexture(Width, Height, Depth, Levels,
            Usage, Format, Pool, ppVolumeTexture, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateCubeTexture(
        UINT EdgeLength,
        UINT Levels,
        DWORD Usage,
        D3DFORMAT Format,
        D3DPOOL Pool,
        IDirect3DCubeTexture9 **ppCubeTexture,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateCubeTexture({}, {}, {}, {}, {})",
            EdgeLength, Levels, usage2s(Usage), Format, pool2s(Pool));
    CHECK_RESULT(pReal->CreateCubeTexture(
            EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexBuffer(
        UINT Length,
        DWORD Usage,
        DWORD FVF,
        D3DPOOL Pool,
        IDirect3DVertexBuffer9 **ppVertexBuffer,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateVertexBuffer({}, {}, {}, {})", Length, usage2s(Usage), FVF, pool2s(Pool));
    CHECK_RESULT(pReal->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateIndexBuffer(
        UINT Length,
        DWORD Usage,
        D3DFORMAT Format,
        D3DPOOL Pool,
        IDirect3DIndexBuffer9 **ppIndexBuffer,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateIndexBuffer({}, {}, {}, {})", Length, usage2s(Usage), Format, pool2s(Pool));
    CHECK_RESULT(pReal->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateRenderTarget(
        UINT Width,
        UINT Height,
        D3DFORMAT Format,
        D3DMULTISAMPLE_TYPE MultiSample,
        DWORD MultisampleQuality,
        BOOL Lockable,
        IDirect3DSurface9 **ppSurface,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateRenderTarget({}, {}, {}, {}, {}, {})",
            Width, Height, Format, MultiSample, MultisampleQuality, Lockable);
    CHECK_RESULT(pReal->CreateRenderTarget(Width, Height, Format,
            MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateDepthStencilSurface(
        UINT Width,
        UINT Height,
        D3DFORMAT Format,
        D3DMULTISAMPLE_TYPE MultiSample,
        DWORD MultisampleQuality,
        BOOL Discard,
        IDirect3DSurface9 **ppSurface,
        HANDLE *pSharedHandle)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->CreateDepthStencilSurface(Width, Height, Format,
            MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::UpdateSurface(
        IDirect3DSurface9 *pSourceSurface,
        const RECT *pSourceRect,
        IDirect3DSurface9 *pDestinationSurface,
        const POINT *pDestPoint)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::UpdateTexture(
        IDirect3DBaseTexture9 *pSourceTexture,
        IDirect3DBaseTexture9 *pDestinationTexture)
{
    WRAP_DEBUG;

    if (CUSTOM_RESET) {
#ifdef __GNUC__

        // Do a pointer compare rather than `QueryInterface` because it is a lot cheaper than incrementing
        // the reference count
        if (pSourceTexture && (&pSourceTexture->QueryInterface) == WrappedIDirect3DTexture9::QueryInterface) {
            pSourceTexture = static_cast<WrappedIDirect3DTexture9 *>(pSourceTexture)->pReal;
        }
        if (pDestinationTexture &&
                (&pDestinationTexture->QueryInterface) == WrappedIDirect3DTexture9::QueryInterface)
        {
            pDestinationTexture = static_cast<WrappedIDirect3DTexture9 *>(pDestinationTexture)->pReal;
        }
#else
        void *dummy = nullptr;

        if (pSourceTexture && SUCCEEDED(pSourceTexture->QueryInterface(
                IID_WrappedIDirect3DTexture9, &dummy)))
        {
            pSourceTexture = static_cast<WrappedIDirect3DTexture9 *>(pSourceTexture)->pReal;
        }
        if (pDestinationTexture && SUCCEEDED(pDestinationTexture->QueryInterface(
                IID_WrappedIDirect3DTexture9, &dummy)))
        {
            pDestinationTexture = static_cast<WrappedIDirect3DTexture9 *>(pDestinationTexture)->pReal;
        }
#endif
    }

    CHECK_RESULT(pReal->UpdateTexture(pSourceTexture, pDestinationTexture));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderTargetData(
        IDirect3DSurface9 *pRenderTarget,
        IDirect3DSurface9 *pDestSurface)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetRenderTargetData(pRenderTarget, pDestSurface));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetFrontBufferData(
        UINT iSwapChain,
        IDirect3DSurface9 *pDestSurface)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetFrontBufferData(iSwapChain, pDestSurface));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::StretchRect(
        IDirect3DSurface9 *pSourceSurface,
        const RECT *pSourceRect,
        IDirect3DSurface9 *pDestSurface,
        const RECT *pDestRect,
        D3DTEXTUREFILTERTYPE Filter)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ColorFill(
        IDirect3DSurface9 *pSurface,
        const RECT *pRect,
        D3DCOLOR color)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->ColorFill(pSurface, pRect, color));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateOffscreenPlainSurface(
        UINT Width,
        UINT Height,
        D3DFORMAT Format,
        D3DPOOL Pool,
        IDirect3DSurface9 **ppSurface,
        HANDLE *pSharedHandle)
{
    WRAP_VERBOSE_FMT("CreateOffscreenPlainSurface({}, {}, {}, {})",
            Width, Height, Format, pool2s(Pool));
    CHECK_RESULT(pReal->CreateOffscreenPlainSurface(
            Width, Height, Format, Pool, ppSurface, pSharedHandle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetRenderTarget(
        DWORD RenderTargetIndex,
        IDirect3DSurface9 *pRenderTarget)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetRenderTarget(RenderTargetIndex, pRenderTarget));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderTarget(
        DWORD RenderTargetIndex,
        IDirect3DSurface9 **ppRenderTarget)
{
    WRAP_DEBUG;
    return pReal->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetDepthStencilSurface(
        IDirect3DSurface9 *pNewZStencil)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetDepthStencilSurface(pNewZStencil));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDepthStencilSurface(
        IDirect3DSurface9 **ppZStencilSurface)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetDepthStencilSurface(ppZStencilSurface));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::BeginScene() {
    WRAP_DEBUG;

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "WrappedIDirect3DDevice9::BeginScene");
    });

    return pReal->BeginScene();
}

static IDirect3DSurface9 *topSurface = nullptr;
static IDirect3DSurface9 *backbuffer = nullptr;
static LPDIRECT3DSWAPCHAIN9 mSwapChain = nullptr;
static IDirect3DTexture9* tex;

void SurfaceHook(IDirect3DDevice9 *pReal) {
    // log_misc("graphics::d3d9", "SurfaceHook called");
    D3DPRESENT_PARAMETERS param {};

    pReal->GetSwapChain(0, &mSwapChain);
    mSwapChain->GetPresentParameters(&param);
    if (!topSurface) {
        if (pReal->CreateTexture(4096, 4096, 1,
            D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT, &tex, NULL) != D3D_OK) {
            log_misc("graphics::d3d9", "create texture failed");

        }
        log_misc("graphics::d3d9", "Backbuffer: {} {} {}",
                param.BackBufferWidth, param.BackBufferHeight, param.BackBufferCount);
        tex->GetSurfaceLevel(0, &topSurface);

        if (mSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backbuffer) != D3D_OK) {
            log_misc("graphics::d3d9", "GetBackBuffer failed");
        }
    }

    const int rectLeft = 1024;
    const int rectTop = 576;
    const int w = param.BackBufferWidth;
    const int h = param.BackBufferHeight;
    D3DLOCKED_RECT rect;

    RECT targetRect {
        rectLeft,
        rectTop,
        (LONG)(rectLeft + w),
        (LONG)(rectTop + h),
    };

    // stretch to add top/left offset to avoid going negative
    topSurface->LockRect(&rect, NULL, D3DLOCK_DONOTWAIT);
    auto hr = pReal->StretchRect(
        backbuffer, nullptr,
        topSurface, &targetRect,
        D3DTEXF_LINEAR);

    if (hr != D3D_OK) {
        log_misc("graphics::d3d9", "StretchRect backbuffer failed");
    }
    topSurface->UnlockRect();

    // do the actual zoom / offset math
    auto& scene = cfg::SCREENRESIZE->scene_settings[cfg::SCREENRESIZE->screen_resize_current_scene];
    if (scene.centered) {
        targetRect.right = (w + rectLeft) / scene.scale_x;
        targetRect.bottom = (h + rectTop) / scene.scale_y;
        const LONG deltaH = ((targetRect.bottom - targetRect.top) - h) / 2;
        const LONG deltaW = ((targetRect.right - targetRect.left) - w) / 2;
        targetRect.top -= deltaH;
        targetRect.bottom -= deltaH;
        targetRect.left -= deltaW;
        targetRect.right -= deltaW;
    } else {
        targetRect.left -= scene.offset_x;
        targetRect.top += scene.offset_y;
        targetRect.right = -scene.offset_x;
        targetRect.right += (w + rectLeft) / scene.scale_x;
        targetRect.bottom = scene.offset_y;
        targetRect.bottom += (h + rectTop) / scene.scale_y;
    }

    // draw to back buffer
    backbuffer->LockRect(&rect, NULL, D3DLOCK_DONOTWAIT);
    hr = pReal->StretchRect(
        topSurface, &targetRect,
        backbuffer, nullptr,
        cfg::SCREENRESIZE->enable_linear_filter ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    backbuffer->UnlockRect();
    if (hr != D3D_OK) {
        log_misc("graphics::d3d9", "StretchRect targetRect failed");
    }
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EndScene() {
    WRAP_DEBUG;

    if (cfg::SCREENRESIZE->enable_screen_resize) {
        SurfaceHook(pReal);
    }

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "WrappedIDirect3DDevice9::EndScene");
    });

    return pReal->EndScene();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Clear(
        DWORD Count,
        const D3DRECT *pRects,
        DWORD Flags,
        D3DCOLOR Color,
        float Z,
        DWORD Stencil)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->Clear(Count, pRects, Flags, Color, Z, Stencil));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTransform(
        D3DTRANSFORMSTATETYPE State,
        const D3DMATRIX *pMatrix)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetTransform(State, pMatrix));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetTransform(
        D3DTRANSFORMSTATETYPE State,
        D3DMATRIX *pMatrix)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetTransform(State, pMatrix));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::MultiplyTransform(
        D3DTRANSFORMSTATETYPE State,
        const D3DMATRIX *pMatrix)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->MultiplyTransform(State, pMatrix));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetViewport(
        const D3DVIEWPORT9 *pViewport)
{
    WRAP_DEBUG;

#ifndef SPICE64
    if (avs::game::is_model({ "JDZ", "KDZ", "K39", "L39", "LBR" })) {
        // Set vertex shader constant to half pixel viewport offset
        const float ConstantHalfTexelFixupOffset[2] = {-1.0F / (float)pViewport->Width, 1.0F / (float)pViewport->Height};
        HRESULT ret = pReal->SetVertexShaderConstantF(63, ConstantHalfTexelFixupOffset, sizeof(ConstantHalfTexelFixupOffset) / sizeof(ConstantHalfTexelFixupOffset[0]));
        CHECK_RESULT(ret);
    }
#endif

    CHECK_RESULT(pReal->SetViewport(pViewport));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetViewport(
        D3DVIEWPORT9 *pViewport)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetViewport(pViewport));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetMaterial(
        const D3DMATERIAL9 *pMaterial)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetMaterial(pMaterial));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetMaterial(
        D3DMATERIAL9 *pMaterial)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetMaterial(pMaterial));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetLight(
        DWORD Index,
        const D3DLIGHT9 *pLight)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetLight(Index, pLight));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetLight(
        DWORD Index,
        D3DLIGHT9 *pLight)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetLight(Index, pLight));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::LightEnable(
        DWORD Index,
        BOOL Enable)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->LightEnable(Index, Enable));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetLightEnable(
        DWORD Index,
        BOOL *pEnable)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetLightEnable(Index, pEnable));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetClipPlane(
        DWORD Index,
        const float *pPlane)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetClipPlane(Index, pPlane));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetClipPlane(
        DWORD Index,
        float *pPlane)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetClipPlane(Index, pPlane));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetRenderState(
        D3DRENDERSTATETYPE State,
        DWORD Value)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetRenderState(State, Value));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderState(
        D3DRENDERSTATETYPE State,
        DWORD *pValue)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetRenderState(State, pValue));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateStateBlock(
        D3DSTATEBLOCKTYPE Type,
        IDirect3DStateBlock9 **ppSB)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->CreateStateBlock(Type, ppSB));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::BeginStateBlock() {
    WRAP_DEBUG;
    return pReal->BeginStateBlock();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EndStateBlock(
        IDirect3DStateBlock9 **ppSB)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->EndStateBlock(ppSB));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetClipStatus(
        const D3DCLIPSTATUS9 *pClipStatus)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetClipStatus(pClipStatus));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetClipStatus(
        D3DCLIPSTATUS9 *pClipStatus)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetClipStatus(pClipStatus));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetTexture(
        DWORD Stage,
        IDirect3DBaseTexture9 **ppTexture)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetTexture(Stage, ppTexture));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTexture(
        DWORD Stage,
        IDirect3DBaseTexture9 *pTexture)
{
    WRAP_DEBUG_FMT("SetTexture({}, {})", Stage, fmt::ptr(pTexture));

    if (CUSTOM_RESET) {
        void *dummy = nullptr;

        if (pTexture && SUCCEEDED(pTexture->QueryInterface(
                IID_WrappedIDirect3DTexture9, &dummy)))
        {
            pTexture = static_cast<WrappedIDirect3DTexture9 *>(pTexture)->pReal;
        }
    }

    CHECK_RESULT(pReal->SetTexture(Stage, pTexture));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetTextureStageState(
        DWORD Stage,
        D3DTEXTURESTAGESTATETYPE Type,
        DWORD *pValue)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetTextureStageState(Stage, Type, pValue));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTextureStageState(
        DWORD Stage,
        D3DTEXTURESTAGESTATETYPE Type,
        DWORD Value)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetTextureStageState(Stage, Type, Value));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetSamplerState(
        DWORD Sampler,
        D3DSAMPLERSTATETYPE Type,
        DWORD *pValue)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetSamplerState(Sampler, Type, pValue));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetSamplerState(
        DWORD Sampler,
        D3DSAMPLERSTATETYPE Type,
        DWORD Value)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetSamplerState(Sampler, Type, Value));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ValidateDevice(
        DWORD *pNumPasses)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->ValidateDevice(pNumPasses));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPaletteEntries(
        UINT PaletteNumber,
        const PALETTEENTRY *pEntries)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetPaletteEntries(PaletteNumber, pEntries));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPaletteEntries(
        UINT PaletteNumber,
        PALETTEENTRY *pEntries)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetPaletteEntries(PaletteNumber, pEntries));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetCurrentTexturePalette(
        UINT PaletteNumber)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetCurrentTexturePalette(PaletteNumber));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetCurrentTexturePalette(
        UINT *PaletteNumber)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetCurrentTexturePalette(PaletteNumber));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetScissorRect(
        const RECT *pRect)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetScissorRect(pRect));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetScissorRect(
        RECT *pRect)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetScissorRect(pRect));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetSoftwareVertexProcessing(
        BOOL bSoftware)
{
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->SetSoftwareVertexProcessing(bSoftware));
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetSoftwareVertexProcessing() {
    WRAP_VERBOSE;
    return pReal->GetSoftwareVertexProcessing();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetNPatchMode(float nSegments) {
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetNPatchMode(nSegments));
}

float STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetNPatchMode() {
    WRAP_DEBUG;
    return pReal->GetNPatchMode();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawPrimitive(
        D3DPRIMITIVETYPE PrimitiveType,
        UINT StartVertex,
        UINT PrimitiveCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawIndexedPrimitive(
        D3DPRIMITIVETYPE PrimitiveType,
        INT BaseVertexIndex,
        UINT MinVertexIndex,
        UINT NumVertices,
        UINT StartIndex,
        UINT PrimitiveCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->DrawIndexedPrimitive(
            PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices,
            StartIndex, PrimitiveCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawPrimitiveUP(
        D3DPRIMITIVETYPE PrimitiveType,
        UINT PrimitiveCount,
        const void *pVertexStreamZeroData,
        UINT VertexStreamZeroStride)
{
    WRAP_DEBUG_FMT("DrawPrimitiveUP({}, {}, {}, {})",
            PrimitiveType, PrimitiveCount,
            fmt::ptr(pVertexStreamZeroData), VertexStreamZeroStride);
    CHECK_RESULT(pReal->DrawPrimitiveUP(
            PrimitiveType, PrimitiveCount,
            pVertexStreamZeroData, VertexStreamZeroStride));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawIndexedPrimitiveUP(
        D3DPRIMITIVETYPE PrimitiveType,
        UINT MinVertexIndex,
        UINT NumVertices,
        UINT PrimitiveCount,
        const void *pIndexData,
        D3DFORMAT IndexDataFormat,
        const void *pVertexStreamZeroData,
        UINT VertexStreamZeroStride)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->DrawIndexedPrimitiveUP(
            PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData,
            IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ProcessVertices(
        UINT SrcStartIndex,
        UINT DestIndex,
        UINT VertexCount,
        IDirect3DVertexBuffer9 *pDestBuffer,
        IDirect3DVertexDeclaration9 *pVertexDecl,
        DWORD Flags)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->ProcessVertices(SrcStartIndex, DestIndex,
            VertexCount, pDestBuffer, pVertexDecl, Flags));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexDeclaration(
        const D3DVERTEXELEMENT9 *pVertexElements,
        IDirect3DVertexDeclaration9 **ppDecl)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->CreateVertexDeclaration(pVertexElements, ppDecl));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexDeclaration(
        IDirect3DVertexDeclaration9 *pDecl)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetVertexDeclaration(pDecl));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexDeclaration(
        IDirect3DVertexDeclaration9 **ppDecl)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetVertexDeclaration(ppDecl));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetFVF(
        DWORD FVF)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetFVF(FVF));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetFVF(
        DWORD *pFVF)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetFVF(pFVF));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexShader(
        const DWORD *pFunction,
        IDirect3DVertexShader9 **ppShader)
{
    WRAP_VERBOSE_FMT("CreateVertexShader({})", fmt::ptr(pFunction));
    CHECK_RESULT(pReal->CreateVertexShader(pFunction, ppShader));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShader(
        IDirect3DVertexShader9 *pShader)
{
    WRAP_DEBUG_FMT("SetVertexShader({})", fmt::ptr(pShader));

#ifndef SPICE64

    // diagonal line fix
    if (avs::game::is_model({ "JDZ", "KDZ", "K39", "L39", "LBR" })) {
        if (!vertex_shader) {
            log_info("graphics::d3d9", "initializing and setting up vertex shaders");

            // create and set up the vertex shader shim
            HRESULT ret = pReal->CreateVertexShader((const DWORD *) g_vs11_vs_main, &vertex_shader);
            if (FAILED(ret)) {
                log_warning("graphics::d3d9", "CreateVertexShader failed, hr={}", FMT_HRESULT(ret));
            }
        }
        if (pShader != nullptr) {
            pShader = vertex_shader;
        }
    }
#endif

    CHECK_RESULT(pReal->SetVertexShader(pShader));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShader(
        IDirect3DVertexShader9 **ppShader)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetVertexShader(ppShader));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantF(
        UINT StartRegister,
        const float *pConstantData,
        UINT Vector4fCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetVertexShaderConstantF(
            StartRegister, pConstantData, Vector4fCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShaderConstantF(
        UINT StartRegister,
        float *pConstantData,
        UINT Vector4fCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetVertexShaderConstantF(
            StartRegister, pConstantData, Vector4fCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantI(
        UINT StartRegister,
        const int *pConstantData,
        UINT Vector4iCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetVertexShaderConstantI(
            StartRegister, pConstantData, Vector4iCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShaderConstantI(
        UINT StartRegister,
        int *pConstantData,
        UINT Vector4iCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetVertexShaderConstantI(
            StartRegister, pConstantData, Vector4iCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantB(
        UINT StartRegister,
        const BOOL *pConstantData,
        UINT BoolCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetVertexShaderConstantB(
            StartRegister, pConstantData, BoolCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShaderConstantB(
        UINT StartRegister,
        BOOL *pConstantData,
        UINT BoolCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetVertexShaderConstantB(
            StartRegister, pConstantData, BoolCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetStreamSource(
        UINT StreamNumber,
        IDirect3DVertexBuffer9 *pStreamData,
        UINT OffsetInBytes,
        UINT Stride)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetStreamSource(
            StreamNumber, pStreamData, OffsetInBytes, Stride));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetStreamSource(
        UINT StreamNumber,
        IDirect3DVertexBuffer9 **ppStreamData,
        UINT *OffsetInBytes,
        UINT *pStride)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetStreamSource(
            StreamNumber, ppStreamData, OffsetInBytes, pStride));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetStreamSourceFreq(
        UINT StreamNumber,
        UINT Divider)
{
    WRAP_DEBUG;
    return pReal->SetStreamSourceFreq(StreamNumber, Divider);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetStreamSourceFreq(
        UINT StreamNumber,
        UINT *Divider)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetStreamSourceFreq(StreamNumber, Divider));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetIndices(
        IDirect3DIndexBuffer9 *pIndexData)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetIndices(pIndexData));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetIndices(
        IDirect3DIndexBuffer9 **ppIndexData)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetIndices(ppIndexData));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreatePixelShader(
        const DWORD *pFunction,
        IDirect3DPixelShader9 **ppShader)
{
    WRAP_VERBOSE_FMT("CreatePixelShader({})", fmt::ptr(pFunction));
    CHECK_RESULT(pReal->CreatePixelShader(pFunction, ppShader));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShader(
        IDirect3DPixelShader9 *pShader)
{
    WRAP_DEBUG_FMT("SetPixelShader({})", fmt::ptr(pShader));
    CHECK_RESULT(pReal->SetPixelShader(pShader));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShader(
        IDirect3DPixelShader9 **ppShader)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetPixelShader(ppShader));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantF(
        UINT StartRegister,
        const float *pConstantData,
        UINT Vector4fCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetPixelShaderConstantF(
            StartRegister, pConstantData, Vector4fCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShaderConstantF(
        UINT StartRegister,
        float *pConstantData,
        UINT Vector4fCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetPixelShaderConstantF(
            StartRegister, pConstantData, Vector4fCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantI(
        UINT StartRegister,
        const int *pConstantData,
        UINT Vector4iCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetPixelShaderConstantI(
            StartRegister, pConstantData, Vector4iCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShaderConstantI(
        UINT StartRegister,
        int *pConstantData,
        UINT Vector4iCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetPixelShaderConstantI(
            StartRegister, pConstantData, Vector4iCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantB(
        UINT StartRegister,
        const BOOL *pConstantData,
        UINT BoolCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->SetPixelShaderConstantB(
            StartRegister, pConstantData, BoolCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShaderConstantB(
        UINT StartRegister,
        BOOL *pConstantData,
        UINT BoolCount)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->GetPixelShaderConstantB(
            StartRegister, pConstantData, BoolCount));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawRectPatch(
        UINT Handle,
        const float *pNumSegs,
        const D3DRECTPATCH_INFO *pRectPatchInfo)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawTriPatch(
        UINT Handle,
        const float *pNumSegs,
        const D3DTRIPATCH_INFO *pTriPatchInfo)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DeletePatch(UINT Handle) {
    WRAP_VERBOSE;
    CHECK_RESULT(pReal->DeletePatch(Handle));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateQuery(
        D3DQUERYTYPE Type,
        IDirect3DQuery9 **ppQuery)
{
    WRAP_DEBUG;
    CHECK_RESULT(pReal->CreateQuery(Type, ppQuery));
}

/*
 * IDirect3DDevice9Ex
 */

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetConvolutionMonoKernel(
        UINT width,
        UINT height,
        float *rows,
        float *columns)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->SetConvolutionMonoKernel(
            width, height, rows, columns));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ComposeRects(
        IDirect3DSurface9 *pSrc,
        IDirect3DSurface9 *pDst,
        IDirect3DVertexBuffer9 *pSrcRectDescs,
        UINT NumRects,
        IDirect3DVertexBuffer9 *pDstRectDescs,
        D3DCOMPOSERECTSOP Operation,
        int Xoffset,
        int Yoffset)
{
    WRAP_DEBUG;
    assert(is_d3d9ex);

    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->ComposeRects(
            pSrc, pDst, pSrcRectDescs, NumRects, pDstRectDescs,
            Operation, Xoffset, Yoffset));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::PresentEx(
        const RECT *pSourceRect,
        const RECT *pDestRect,
        HWND hDestWindowOverride,
        const RGNDATA *pDirtyRegion,
        DWORD dwFlags)
{
    WRAP_DEBUG;
    assert(is_d3d9ex);

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("graphics::d3d9", "WrappedIDirect3DDevice9::PresentEx");
    });

    graphics_d3d9_on_present(hFocusWindow, pReal, this);

    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->PresentEx(
            pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetGPUThreadPriority(
        INT *pPriority)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->GetGPUThreadPriority(pPriority));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetGPUThreadPriority(
        INT Priority)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->SetGPUThreadPriority(Priority));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::WaitForVBlank(
        UINT iSwapChain)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->WaitForVBlank(iSwapChain));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CheckResourceResidency(
        IDirect3DResource9 **pResourceArray,
        UINT32 NumResources)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->CheckResourceResidency(
            pResourceArray, NumResources));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetMaximumFrameLatency(
        UINT MaxLatency)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->SetMaximumFrameLatency(MaxLatency));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetMaximumFrameLatency(
        UINT *pMaxLatency)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->GetMaximumFrameLatency(pMaxLatency));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CheckDeviceState(HWND hDestinationWindow) {
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->CheckDeviceState(hDestinationWindow));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateRenderTargetEx(
        UINT Width,
        UINT Height,
        D3DFORMAT Format,
        D3DMULTISAMPLE_TYPE MultiSample,
        DWORD MultisampleQuality,
        BOOL Lockable,
        IDirect3DSurface9 **ppSurface,
        HANDLE *pSharedHandle,
        DWORD Usage)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->CreateRenderTargetEx(
            Width, Height, Format, MultiSample,
            MultisampleQuality, Lockable, ppSurface, pSharedHandle, Usage));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateOffscreenPlainSurfaceEx(
        UINT Width,
        UINT Height,
        D3DFORMAT Format,
        D3DPOOL Pool,
        IDirect3DSurface9 **ppSurface,
        HANDLE *pSharedHandle,
        DWORD Usage)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->CreateOffscreenPlainSurfaceEx(
            Width, Height, Format, Pool, ppSurface, pSharedHandle, Usage));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateDepthStencilSurfaceEx(
        UINT Width,
        UINT Height,
        D3DFORMAT Format,
        D3DMULTISAMPLE_TYPE MultiSample,
        DWORD MultisampleQuality,
        BOOL Discard,
        IDirect3DSurface9 **ppSurface,
        HANDLE *pSharedHandle,
        DWORD Usage)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->CreateDepthStencilSurfaceEx(
            Width, Height, Format, MultiSample,
            MultisampleQuality, Discard, ppSurface, pSharedHandle, Usage));
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ResetEx(
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);

    log_misc("graphics::d3d9", "WrappedIDirect3DDevice9::ResetEx");

    if (GRAPHICS_WINDOWED) {
        if (pPresentationParameters) {
            pPresentationParameters->Windowed = true;
            pPresentationParameters->FullScreen_RefreshRateInHz = 0;
        }

        pFullscreenDisplayMode = nullptr;
    } else if (GRAPHICS_FORCE_REFRESH > 0) {
        if (pPresentationParameters) {
            pPresentationParameters->FullScreen_RefreshRateInHz = GRAPHICS_FORCE_REFRESH;
        }
        if (pFullscreenDisplayMode) {
            pFullscreenDisplayMode->RefreshRate = GRAPHICS_FORCE_REFRESH;
        }
    }

    // reset overlay
    if (overlay::OVERLAY && overlay::OVERLAY->uses_device(pReal)) {
        overlay::OVERLAY->reset_invalidate();
    }

    HRESULT res = static_cast<IDirect3DDevice9Ex *>(pReal)->ResetEx(
            pPresentationParameters, pFullscreenDisplayMode);

    // recreate overlay
    if (overlay::OVERLAY && overlay::OVERLAY->uses_device(pReal) && SUCCEEDED(res)) {
        overlay::OVERLAY->reset_recreate();
    }

    CHECK_RESULT(res);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDisplayModeEx(
        UINT iSwapChain,
        D3DDISPLAYMODEEX *pMode,
        D3DDISPLAYROTATION *pRotation)
{
    WRAP_DEBUG;

    assert(is_d3d9ex);
    CHECK_RESULT(static_cast<IDirect3DDevice9Ex *>(pReal)->GetDisplayModeEx(
            iSwapChain, pMode, pRotation));
}
