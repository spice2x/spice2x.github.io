#pragma once

#include <d3d9.h>

interface WrappedIDirect3DDevice9;

struct WrappedIDirect3DSwapChain9 : IDirect3DSwapChain9Ex {
    WrappedIDirect3DSwapChain9(WrappedIDirect3DDevice9 *dev, IDirect3DSwapChain9 *orig) :
        pDev(dev), pReal(orig), is_d3d9ex(false)
    {
        IDirect3DSwapChain9Ex *swapchain = nullptr;

        // attempt to upgrade handle
        if (SUCCEEDED(this->QueryInterface(IID_PPV_ARGS(&swapchain))) && swapchain != nullptr) {
            swapchain->Release();
        }
    }

    WrappedIDirect3DSwapChain9(WrappedIDirect3DDevice9 *dev, IDirect3DSwapChain9Ex *orig) :
        pDev(dev), pReal(orig), is_d3d9ex(true)
    {
    }

    virtual ~WrappedIDirect3DSwapChain9(void) {
    }

    WrappedIDirect3DSwapChain9(const WrappedIDirect3DSwapChain9 &) = delete;
    WrappedIDirect3DSwapChain9 &operator=(const WrappedIDirect3DSwapChain9 &) = delete;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) noexcept override;
    virtual ULONG STDMETHODCALLTYPE AddRef(void) noexcept override;
    virtual ULONG STDMETHODCALLTYPE Release(void) noexcept override;
#pragma endregion

#pragma region IDirect3DSwapChain9
    virtual HRESULT STDMETHODCALLTYPE Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD dwFlags) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9 *pDestSurface) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE *pMode) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters) noexcept override;
#pragma endregion

#pragma region IDirect3DSwapChain9Ex
    virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *pLastPresentCount) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetPresentStats(D3DPRESENTSTATS *pPresentationStatistics) noexcept override;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayModeEx(D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) noexcept override;
#pragma endregion

    WrappedIDirect3DDevice9 *const pDev;

    IDirect3DSwapChain9 *pReal;
    bool is_d3d9ex = false;

    bool should_run_hooks = true;
};
