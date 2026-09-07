#pragma once

#include <d3d9.h>
#include "modules.h"

namespace sdk::d3d9 {

SPICE_SDK_STATUS_CODE register_d3d9(const std::vector<SdkModule> &modules,
    spice_sdk_d3d9_callback_func *callback, void *userdata);
void draw(HWND window, IDirect3DDevice9 *device);
void present_complete(IDirect3DDevice9 *device, HRESULT result);
void invalidate(IDirect3DDevice9 *device);
void reset_complete(IDirect3DDevice9 *device, bool success);
void destroy(IDirect3DDevice9 *device);
void shutdown(bool graphics_stopped);

}