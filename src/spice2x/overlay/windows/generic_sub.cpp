#undef CINTERFACE

#include "generic_sub.h"

#include <fmt/format.h>

#include "games/io.h"
#include "cfg/screen_resize.h"
#include "hooks/graphics/backends/d3d9/d3d9_backend.h"
#include "hooks/graphics/backends/d3d9/d3d9_device.h"
#include "hooks/graphics/graphics.h"
#include "util/logging.h"
#include "util/utils.h"
#include "touch/touch.h"

int GENERIC_SUB_WINDOW_X = 0;
int GENERIC_SUB_WINDOW_Y = 0;
int GENERIC_SUB_WINDOW_WIDTH = 0;
int GENERIC_SUB_WINDOW_HEIGHT = 0;
bool GENERIC_SUB_WINDOW_FULLSIZE = false;

// #define OVERLAYDBG 1

namespace overlay::windows {

    const ImVec4 YELLOW(1.f, 1.f, 0.f, 1.f);
    const ImVec4 WHITE(1.f, 1.f, 1.f, 1.f);

    GenericSubScreen::GenericSubScreen(SpiceOverlay *overlay) : Window(overlay), device(overlay->get_device()) {
        this->remove_window_padding = true;
        // ImGuiWindowFlags_NoBackground is needed as the background is drawn on top of the subscreen image
        this->flags = ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoDocking;

        this->size_max = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        this->size_min = ImVec2(240, 135 + ImGui::GetFrameHeight());
        this->init_size = size_min;
        this->resize_callback = keep_16_by_9;

        this->toggle_button = games::OverlayButtons::ToggleSubScreen;
        this->texture_width = 0;
        this->texture_height = 0;

        overlay->set_subscreen_mouse_handler([this](LONG *x, LONG *y) -> bool {
            // convert to normalized form (relative window coordinates 0.f-1.f)
            ImVec2 xy;

            // log_misc("sub::overlay", "mouse handler {} {}", to_string(*x), to_string(*y));
            // log_misc("sub::overlay", "spicetouch coords {} {} {} {}", to_string(SPICETOUCH_TOUCH_X), to_string(SPICETOUCH_TOUCH_Y), to_string(SPICETOUCH_TOUCH_WIDTH), to_string(SPICETOUCH_TOUCH_HEIGHT));
            
            float ratio_x, ratio_y;

            if (GRAPHICS_WINDOWED) {
                // input coords are relative to spicetouch wnd
                ratio_x = (float)*x / SPICETOUCH_TOUCH_WIDTH;
                ratio_y = (float)*y / SPICETOUCH_TOUCH_HEIGHT;
            } else {
                // inputs coords are relative to (0,0) for non-windowed mode
                ratio_x = (float)*x / ImGui::GetIO().DisplaySize.x;
                ratio_y = (float)*y / ImGui::GetIO().DisplaySize.y;
            }
            // log_misc("sub::overlay", "game coords {} {}", to_string(ratio_x), to_string(ratio_y));

            // transform to subscreen overlay coords
            if (!GENERIC_SUB_WINDOW_FULLSIZE) {
                ratio_x = (ratio_x * ImGui::GetIO().DisplaySize.x - GENERIC_SUB_WINDOW_X) / GENERIC_SUB_WINDOW_WIDTH;
                ratio_y = (ratio_y * ImGui::GetIO().DisplaySize.y - GENERIC_SUB_WINDOW_Y) / GENERIC_SUB_WINDOW_HEIGHT;
                // log_misc("sub::overlay", "overlay coords {} {} {} {}", to_string(GENERIC_SUB_WINDOW_X), to_string(GENERIC_SUB_WINDOW_Y), to_string(GENERIC_SUB_WINDOW_WIDTH), to_string(GENERIC_SUB_WINDOW_HEIGHT));
            }

            xy.x = ratio_x;
            xy.y = ratio_y;
            // log_misc("sub::overlay", "ratio {} {}", to_string(xy.x), to_string(xy.y));

            // x/y can be outside of window
            if (xy.x < 0.f || 1.f < xy.x || xy.y < 0.f || 1.f < xy.y) {
                return false;
            }

            // call into child
            this->touch_transform(xy, x, y);
            return true;
        });
    }

    void GenericSubScreen::touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) {}

    void GenericSubScreen::build_content() {
        if (this->disabled_message.has_value()) {
            this->flags &= ~ImGuiWindowFlags_NoBackground;
            ImGui::TextColored(YELLOW, "%s", this->disabled_message.value().c_str());
            return;
        }
        this->draw_texture();

#if OVERLAYDBG
        if (this->status_message.has_value()) {
            log_warning("sub::overlay", "{}", this->status_message.value().c_str());
        }
        if (this->status_message.has_value()) {
            ImGui::TextColored(YELLOW, "%s", this->status_message.value().c_str());
        } else if (this->texture) {
            ImGui::TextColored(WHITE, "Successfully acquired surface texture");
        } else {
            ImGui::TextColored(YELLOW, "Failed to acquire surface texture");
        }
#endif
    }

    bool GenericSubScreen::build_texture(IDirect3DSurface9 *surface, UINT width, UINT height) {
        HRESULT hr;

        D3DSURFACE_DESC desc {};
        hr = surface->GetDesc(&desc);
        if (FAILED(hr)) {
            this->status_message = fmt::format("Failed to get surface descriptor, hr={}", FMT_HRESULT(hr));
            return false;
        }

        hr = this->device->CreateTexture(width, height, 0, desc.Usage, desc.Format,
                desc.Pool, &this->texture, nullptr);
        if (FAILED(hr)) {
            this->status_message = fmt::format("Failed to create render target, hr={}", FMT_HRESULT(hr));
            return false;
        }

        this->texture_width = width;
        this->texture_height = height;
        return true;
    }

    void GenericSubScreen::draw_texture() {
        HRESULT hr;

        auto surface = graphics_d3d9_ldj_get_sub_screen();
        if (surface == nullptr) {
            return;
        }

        if (this->draws_window) {
            // calculate the **content** location and size.
            // GetContentRegionAvail returns the correct dimension, up to (and including) the resize handle
            // don't be tempted to change it to some other ImGui routine that accomplishes similar things!
            overlay_content_top_left = ImGui::GetCursorScreenPos();
            overlay_content_size = ImGui::GetContentRegionAvail();
        } else {
            // no window, full screen
            overlay_content_top_left = ImVec2(0, 0);
            overlay_content_size = ImGui::GetIO().DisplaySize;
        }

        GENERIC_SUB_WINDOW_X = overlay_content_top_left.x;
        GENERIC_SUB_WINDOW_Y = overlay_content_top_left.y;
        GENERIC_SUB_WINDOW_WIDTH = overlay_content_size.x;
        GENERIC_SUB_WINDOW_HEIGHT = overlay_content_size.y;

        if (this->draws_window &&
            this->texture &&
            ((UINT)overlay_content_size.x != this->texture_width)) {

#if OVERLAYDBG
            log_info("sub::overlay", "resize {} != {} ", overlay_content_size.x, this->texture_width);
#endif

            // hack needed for SDVX; resizing the texture results in darker image, so allocate texture
            // again with the new size when the window is resized
            this->texture->Release();
            this->texture = nullptr;
            this->texture_width = 0;
            this->texture_height = 0;
        }

        if (this->texture == nullptr) {
            if (!this->build_texture(surface, overlay_content_size.x, overlay_content_size.y)) {
                this->texture = nullptr;
                this->texture_width = 0;
                this->texture_height = 0;
                surface->Release();
                return;
            }
        }

        IDirect3DSurface9 *texture_surface = nullptr;
        hr = this->texture->GetSurfaceLevel(0, &texture_surface);
        if (FAILED(hr)) {
            this->status_message = fmt::format("Failed to get texture surface, hr={}", FMT_HRESULT(hr));
            surface->Release();
            return;
        }

        hr = this->device->StretchRect(surface, nullptr, texture_surface, nullptr, D3DTEXF_LINEAR);
        if (FAILED(hr)) {
            this->status_message = fmt::format("Failed to copy back buffer contents, hr={}", FMT_HRESULT(hr));
            surface->Release();
            texture_surface->Release();
            return;
        }

        surface->Release();
        texture_surface->Release();

        // draw the subscreen (this draws *under* ImGui windows, over the game surface)
        auto bottom_right = overlay_content_size;
        bottom_right.x += overlay_content_top_left.x;
        bottom_right.y += overlay_content_top_left.y;
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<void *>(this->texture),
            overlay_content_top_left,
            bottom_right);

        if (this->draws_window) {
            // draw an invisible button so that it swallows mouse input
            // this is needed to prevent the window from being dragged around
            // (alternatively io.ConfigWindowsMoveFromTitleBarOnly can be set but that is global)
            ImGui::InvisibleButton(
                (this->title + "__InvisibleButton").c_str(),
                overlay_content_size,
                ImGuiButtonFlags_None);
        }

#if OVERLAYDBG
        log_info("sub::overlay", "{} / {} = {}", overlay_content_size.x, overlay_content_size.y, overlay_content_size.x / overlay_content_size.y);
#endif

    }
}
