#pragma once

#include <d3d9.h>

bool graphics_d3d9_process_screenshot(
        IDirect3DDevice9 *device,
        IDirect3DSwapChain9 *sub_swap_chain);

void graphics_d3d9_process_capture(
        IDirect3DDevice9 *device,
        IDirect3DSwapChain9 *sub_swap_chain);
