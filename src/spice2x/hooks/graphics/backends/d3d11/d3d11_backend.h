#pragma once

#include "overlay/overlay.h"

void graphics_d3d11_init();

#ifdef SPICE_D3D11

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;

namespace overlay::d3d11 {

    void render(ID3D11Device *device,
                ID3D11DeviceContext *context,
                IDXGISwapChain *swapchain,
                ID3D11RenderTargetView **rtv);

}

#endif
