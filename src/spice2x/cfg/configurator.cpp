#include "configurator.h"

#include <d3d9.h>

#include "overlay/overlay.h"
#include "util/logging.h"

namespace cfg {

    // globals
    bool CONFIGURATOR_STANDALONE = false;
    bool CONFIGURATOR_FORCE_SOFTWARE_RENDER = false;
    ConfigType CONFIGURATOR_TYPE = ConfigType::Config;

    Configurator::Configurator() {
        CONFIGURATOR_STANDALONE = true;
    }

    Configurator::~Configurator() {
        CONFIGURATOR_STANDALONE = false;
    }

    // Attempt to bring up a D3D9 device backing the configurator's window so the
    // overlay can use the hardware-accelerated imgui_impl_dx9 path instead of
    // the CPU rasterizer. Returns true if both Direct3DCreate9 and CreateDevice
    // succeed; the caller falls back to overlay::create_software() otherwise so
    // that environments without D3D9 (Wine without dxvk, headless test boxes,
    // GPUs whose drivers reject HAL) still get a working configurator.
    static bool try_init_d3d9(ConfiguratorWindow &wnd) {
        if (cfg::CONFIGURATOR_FORCE_SOFTWARE_RENDER) {
            return false;
        }

        if (wnd.hWnd == nullptr) {
            return false;
        }

        IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (d3d == nullptr) {
            log_warning("configurator", "Direct3DCreate9 returned NULL, falling back to software renderer");
            return false;
        }

        D3DPRESENT_PARAMETERS pp {};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        // D3DFMT_UNKNOWN -> driver picks the current desktop format
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth = static_cast<UINT>(wnd.client_width);
        pp.BackBufferHeight = static_cast<UINT>(wnd.client_height);
        pp.hDeviceWindow = wnd.hWnd;
        pp.EnableAutoDepthStencil = FALSE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

        IDirect3DDevice9 *device = nullptr;
        // SOFTWARE_VERTEXPROCESSING is the most compatible behavior flag; the UI
        // is tiny so we don't need hardware T&L. FPU_PRESERVE keeps our floating
        // point environment intact in case other spice code relies on it.
        const DWORD behavior_flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
        HRESULT hr = d3d->CreateDevice(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                wnd.hWnd,
                behavior_flags,
                &pp,
                &device);
        if (FAILED(hr) || device == nullptr) {
            log_warning("configurator",
                "D3D9 CreateDevice failed (hr={:#x}), falling back to software renderer",
                static_cast<unsigned int>(hr));
            d3d->Release();
            return false;
        }

        wnd.d3d = d3d;
        wnd.device = device;
        wnd.pp = pp;
        wnd.use_d3d9 = true;
        return true;
    }

    void Configurator::run() {

        // bring up the overlay against either a real D3D9 device or the software
        // rasterizer. The choice is one-shot - the in-game overlay always uses
        // the same renderer the configurator picked here.
        overlay::ENABLED = true;
        if (try_init_d3d9(this->wnd)) {
            log_info("configurator", "using D3D9 hardware-accelerated renderer");
            overlay::create_d3d9(this->wnd.hWnd, this->wnd.d3d, this->wnd.device);
        } else {
            log_info("configurator", "using software renderer");
            overlay::create_software(this->wnd.hWnd);
        }
        overlay::OVERLAY->set_active(true);
        overlay::OVERLAY->hotkeys_enable = false;
        ImGui::GetIO().MouseDrawCursor = false;

        // run window
        this->wnd.run();
    }
}
