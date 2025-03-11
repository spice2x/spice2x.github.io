#ifndef SPICETOOLS_OVERLAY_WINDOWS_GENERIC_SUB_H
#define SPICETOOLS_OVERLAY_WINDOWS_GENERIC_SUB_H

#include <optional>

#include <windows.h>
#include <d3d9.h>

#include "overlay/window.h"

//=================================================================================================
//  Global variable to track the coords of Subscreen.
//  Values are in original coord-space (not scaled to Windowed mode size).
//  
//  For use with touch coords transformation.
//=================================================================================================

extern int GENERIC_SUB_WINDOW_X;
extern int GENERIC_SUB_WINDOW_Y;
extern int GENERIC_SUB_WINDOW_WIDTH;
extern int GENERIC_SUB_WINDOW_HEIGHT;
extern bool GENERIC_SUB_WINDOW_FULLSIZE;

namespace overlay::windows {

    class GenericSubScreen : public Window {
    public:
        GenericSubScreen(SpiceOverlay *overlay);

        void build_content() override;

    protected:
        virtual void touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out);
        ImVec2 overlay_content_top_left;
        ImVec2 overlay_content_size;
        std::optional<std::string> disabled_message = std::nullopt;
    
    private:
        static void keep_16_by_9(ImGuiSizeCallbackData* data) {
            data->DesiredSize.y = (data->DesiredSize.x * 9.f / 16.f) + ImGui::GetFrameHeight();
        }

        bool build_texture(IDirect3DSurface9 *surface, UINT width, UINT height);
        void draw_texture();

        std::optional<std::string> status_message = std::nullopt;

        IDirect3DDevice9 *device = nullptr;
        IDirect3DTexture9 *texture = nullptr;

        UINT texture_width;
        UINT texture_height;
    };
}

#endif // SPICETOOLS_OVERLAY_WINDOWS_GENERIC_SUB_H
