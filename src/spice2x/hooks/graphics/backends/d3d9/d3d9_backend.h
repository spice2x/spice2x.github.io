#pragma once

#include <d3d9.h>

// {EEE9CCF6-53D6-4326-9AE5-60921B3DB394}
static const GUID IID_WrappedIDirect3D9 = {
    0xeee9ccf6, 0x53d6, 0x4326, { 0x9a, 0xe5, 0x60, 0x92, 0x1b, 0x3d, 0xb3, 0x94 }
};

void graphics_d3d9_init();
void graphics_d3d9_on_present(
    HWND hFocusWindow,
    IDirect3DDevice9 *device,
    IDirect3DDevice9 *wrapped_device);

IDirect3DSurface9 *graphics_d3d9_ldj_get_sub_screen();

struct WrappedIDirect3D9 : IDirect3D9Ex {
    explicit WrappedIDirect3D9(IDirect3D9 *orig) : pReal(orig), is_d3d9ex(false) {}

    explicit WrappedIDirect3D9(IDirect3D9Ex *orig) : pReal(orig), is_d3d9ex(true) {}

    WrappedIDirect3D9(const WrappedIDirect3D9 &) = delete;
    WrappedIDirect3D9 &operator=(const WrappedIDirect3D9 &) = delete;

    virtual ~WrappedIDirect3D9() = default;

#pragma region IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;
#pragma endregion

#pragma region IDirect3D9
    virtual HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *pInitializeFunction) override;
    virtual UINT STDMETHODCALLTYPE GetAdapterCount() override;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier) override;
    virtual UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override;
    virtual HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode) override;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) override;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT iAdapter, D3DDEVTYPE DevType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) override;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels) override;
    virtual HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) override;
    virtual HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override;
    virtual HRESULT STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppReturnedDeviceInterface) override;
#pragma endregion

#pragma region IDirect3D9Ex
    virtual UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter) override;
    virtual HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode, D3DDISPLAYMODEEX *pMode) override;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) override;
    virtual HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3DDevice9Ex **ppReturnedDeviceInterface) override;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT Adapter, LUID *pLUID) override;
#pragma endregion

private:
    IDirect3D9 *pReal;
    bool is_d3d9ex = false;

    //bool attempted_sub_swap_chain_acquire = false;
    //IDirect3DSwapChain9 *sub_swap_chain = nullptr;
};
