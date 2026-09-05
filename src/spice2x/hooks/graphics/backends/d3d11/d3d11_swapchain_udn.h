#pragma once

#ifdef SPICE_D3D11

#include <windows.h>

struct IDXGISwapChain;

namespace d3d11_hooks::udn {

    void schedule_window_attach(IDXGISwapChain *swapchain);
    void apply_present_policy(IDXGISwapChain *swapchain, UINT &sync_interval);
    void install_swapchain_hooks(IDXGISwapChain *swapchain);

}

#endif
