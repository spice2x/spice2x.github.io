#pragma once

// this file uses the C++ interface of Direct3D9
#ifdef CINTERFACE
#undef CINTERFACE
#endif

#include <atomic>

#include <d3d9.h>

#include "util/logging.h"

struct FakeIDirect3DSwapChain9 : IDirect3DSwapChain9Ex {
    FakeIDirect3DSwapChain9(IDirect3DDevice9 *pDev, D3DPRESENT_PARAMETERS *present_params, bool is_d3d9ex) :
        pDev(pDev), is_d3d9ex(is_d3d9ex)
    {
        // copy presentation parameters
        memcpy(&this->present_params, present_params, sizeof(this->present_params));

        // From MSDN https://docs.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters:
        // Values of 0 are treated as 1
        if (this->present_params.BackBufferCount == 0) {
            this->present_params.BackBufferCount = 1;
        }

        for (size_t i = 0; i < this->present_params.BackBufferCount; i++) {
            IDirect3DSurface9 *render_target = nullptr;
            HRESULT hr = pDev->CreateRenderTarget(
                    this->present_params.BackBufferWidth,
                    this->present_params.BackBufferHeight,
                    this->present_params.BackBufferFormat,
                    this->present_params.MultiSampleType,
                    this->present_params.MultiSampleQuality,
                    FALSE,
                    &render_target,
                    nullptr
            );
            if (SUCCEEDED(hr)) {
                this->render_targets.push_back(render_target);
            } else {
                log_warning("graphics::d3d9", "failed to create backing render target for fake swap chain, hr={}",
                        FMT_HRESULT(hr));
            }
        }
    }

    FakeIDirect3DSwapChain9(const FakeIDirect3DSwapChain9 &) = delete;
    FakeIDirect3DSwapChain9 &operator=(const FakeIDirect3DSwapChain9 &) = delete;

    virtual ~FakeIDirect3DSwapChain9(void) {
        for (auto &render_target : this->render_targets) {
            render_target->Release();
        }
    }

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

    IDirect3DDevice9 *const pDev;
    bool is_d3d9ex;

    std::atomic<ULONG> ref_cnt = 1;

    D3DPRESENT_PARAMETERS present_params {};
    std::vector<IDirect3DSurface9 *> render_targets;
};
