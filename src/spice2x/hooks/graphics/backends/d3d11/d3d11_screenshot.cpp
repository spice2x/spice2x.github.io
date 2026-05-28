// dx11 screenshot capture.
//
// mirrors the d3d9 backend's behaviour: when a screenshot trigger is
// pending we copy the current backbuffer into a staging texture, force
// alpha to 255, encode a PNG via stb_image_write, push it to the
// clipboard and notify via the overlay toast system.

#include "d3d11_backend.h"

#ifdef SPICE_D3D11

#include <vector>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "d3d11_internal.h"

#include "external/stb_image_write.h"
#include "hooks/graphics/graphics.h"
#include "misc/clipboard.h"
#include "overlay/notifications.h"
#include "util/fileutils.h"

namespace {

// allocate an `D3D11_USAGE_STAGING` copy of the swapchain backbuffer, then
// memcpy it (with BGRA->RGBA swizzle and forced alpha=255) into a flat
// rgba8 buffer suitable for stb_image_write_png.
bool copy_backbuffer_to_rgba(IDXGISwapChain *swapchain,
                             ID3D11Device *device,
                             ID3D11DeviceContext *context,
                             std::vector<uint8_t> &out,
                             uint32_t &out_w, uint32_t &out_h)
{
    ID3D11Texture2D *backbuffer = nullptr;
    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || !backbuffer) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc {};
    backbuffer->GetDesc(&desc);

    // multisampled backbuffers can't be CopyResource'd into a non-MS
    // staging target; resolve first.
    ID3D11Texture2D *source = backbuffer;
    ID3D11Texture2D *resolved = nullptr;
    if (desc.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC rd = desc;
        rd.SampleDesc.Count = 1;
        rd.SampleDesc.Quality = 0;
        rd.Usage = D3D11_USAGE_DEFAULT;
        rd.BindFlags = D3D11_BIND_RENDER_TARGET;
        rd.CPUAccessFlags = 0;
        rd.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&rd, nullptr, &resolved)) || !resolved) {
            backbuffer->Release();
            return false;
        }
        context->ResolveSubresource(resolved, 0, backbuffer, 0, desc.Format);
        source = resolved;
    }

    D3D11_TEXTURE2D_DESC sd {};
    sd.Width = desc.Width;
    sd.Height = desc.Height;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = desc.Format;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D *staging = nullptr;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging)) || !staging) {
        if (resolved) { resolved->Release(); }
        backbuffer->Release();
        return false;
    }
    context->CopyResource(staging, source);
    if (resolved) {
        resolved->Release();
    }
    backbuffer->Release();

    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }

    const bool is_bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM
                      || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                      || desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;

    out.resize(static_cast<size_t>(desc.Width) * desc.Height * 4);
    const uint8_t *src_base = reinterpret_cast<const uint8_t *>(mapped.pData);
    for (uint32_t y = 0; y < desc.Height; ++y) {
        const uint8_t *row = src_base + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t *dst = out.data() + static_cast<size_t>(y) * desc.Width * 4;
        if (is_bgra) {
            for (uint32_t x = 0; x < desc.Width; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 2];
                dst[x * 4 + 1] = row[x * 4 + 1];
                dst[x * 4 + 2] = row[x * 4 + 0];
                dst[x * 4 + 3] = 255;
            }
        } else {
            for (uint32_t x = 0; x < desc.Width; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 0];
                dst[x * 4 + 1] = row[x * 4 + 1];
                dst[x * 4 + 2] = row[x * 4 + 2];
                dst[x * 4 + 3] = 255;
            }
        }
    }

    context->Unmap(staging, 0);
    staging->Release();

    out_w = desc.Width;
    out_h = desc.Height;
    return true;
}

} // namespace

namespace d3d11_hooks {

void try_screenshot(IDXGISwapChain *swapchain) {
    if (!swapchain || !graphics_screenshot_consume()) {
        return;
    }

    auto file_path = graphics_screenshot_genpath();
    if (file_path.empty()) {
        return;
    }

    ID3D11Device *device = nullptr;
    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) || !device) {
        return;
    }
    ID3D11DeviceContext *context = nullptr;
    device->GetImmediateContext(&context);
    if (!context) {
        device->Release();
        return;
    }

    std::vector<uint8_t> pixels;
    uint32_t w = 0, h = 0;
    const bool ok = copy_backbuffer_to_rgba(swapchain, device, context, pixels, w, h);

    context->Release();
    device->Release();

    if (!ok) {
        log_warning("graphics::d3d11", "screenshot: failed to capture backbuffer");
        overlay::notifications::add(
            overlay::notifications::Severity::Error,
            "Screenshot failed to capture");
        return;
    }

    log_info("graphics::d3d11", "saving screenshot to {}", file_path);
    if (stbi_write_png(file_path.c_str(), (int) w, (int) h, 4,
                       pixels.data(), (int) w * 4))
    {
        clipboard::copy_image(file_path);
        overlay::notifications::add(
            overlay::notifications::Severity::Success,
            fmt::format("Screenshot saved: {}", fileutils::basename(file_path)));
    } else {
        log_warning("graphics::d3d11", "screenshot: stbi_write_png failed");
        overlay::notifications::add(
            overlay::notifications::Severity::Error,
            "Screenshot failed to save");
    }
}

}

#endif // SPICE_D3D11
