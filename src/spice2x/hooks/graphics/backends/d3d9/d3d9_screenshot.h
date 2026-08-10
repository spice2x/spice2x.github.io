#pragma once

#include <d3d9.h>

void graphics_d3d9_process_capture(
	IDirect3DDevice9 *device,
	IDirect3DSwapChain9 *sub_swap_chain);
