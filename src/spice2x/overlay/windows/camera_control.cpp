#include "camera_control.h"

#if SPICE64

#include <games/io.h>
#include <strmif.h>

#include "games/iidx/camera.h"
#include "games/iidx/local_camera.h"
#include "misc/eamuse.h"
#include "util/utils.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "overlay/imgui/extensions.h"
#include "misc/clipboard.h"

using namespace games::iidx;

namespace overlay::windows {

    CameraControl::CameraControl(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "IIDX Camera Control";
        this->flags |= ImGuiWindowFlags_AlwaysAutoResize;
        this->init_pos = overlay::apply_scaling_to_vector(40, 40);
        this->toggle_button = games::OverlayButtons::ToggleCameraControl;
    }

    CameraControl::~CameraControl() {
    }

    void CameraControl::build_content() {
        if (!CAMERA_READY) {
            ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "%s", "Camera not ready");
            return;
        }

        // camera combo box
        auto numCameras = LOCAL_CAMERA_LIST.size();

        if (numCameras == 0) {
            return;
        }

        IIDXLocalCamera *selectedCamera = LOCAL_CAMERA_LIST.at(m_selectedCameraIndex);
        auto selectedCameraChanged = ImGui::BeginCombo(
            "Source", selectedCamera->GetName().c_str()
        );

        if (selectedCameraChanged) {
            for (size_t i = 0; i < numCameras; i++) {
                IIDXLocalCamera *cameraItem = LOCAL_CAMERA_LIST.at(i);
                const bool is_selected = (m_selectedCameraIndex == (int) i);
                if (ImGui::Selectable(cameraItem->GetName().c_str(), is_selected)) {
                    m_selectedCameraIndex = i;
                }

                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        selectedCamera = LOCAL_CAMERA_LIST.at(m_selectedCameraIndex);

        ImGui::AlignTextToFramePadding();
        if (selectedCamera->GetFriendlyName().length() < 22) {
            ImGui::Text("%s", selectedCamera->GetFriendlyName().c_str());
        } else {
            ImGui::Text("%.19s...", selectedCamera->GetFriendlyName().c_str());
        }
        ImGui::SameLine();
        ImGui::HelpMarker(selectedCamera->GetSymLink().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            const auto s = selectedCamera->GetFriendlyName() + "\n" + selectedCamera->GetSymLink();
            clipboard::copy_text(s);
        }

        // Render parameters
        ImGui::Separator();
        ImGui::Text("Rendering");

        // Media Type Selector
        int selectedMediaTypeIndex = selectedCamera->m_selectedMediaTypeIndex;
        auto numMediaTypes = selectedCamera->m_mediaTypeInfos.size();

        auto selectedMediaTypeIndexChanged = ImGui::BeginCombo(
            "Media Type", selectedCamera->m_mediaTypeInfos.at(selectedMediaTypeIndex).description.c_str()
        );

        if (selectedMediaTypeIndexChanged) {
            for (size_t i = 0; i < numMediaTypes; i++) {
                const bool is_selected = (selectedMediaTypeIndex == (int) i);
                if (ImGui::Selectable(selectedCamera->m_mediaTypeInfos.at(i).description.c_str(), is_selected)) {
                    selectedMediaTypeIndex = i;
                }

                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (selectedMediaTypeIndexChanged && selectedMediaTypeIndex != selectedCamera->m_selectedMediaTypeIndex) {
            selectedCamera->m_useAutoMediaType = false;
            selectedCamera->ChangeMediaType(selectedCamera->m_mediaTypeInfos.at(selectedMediaTypeIndex).p_mediaType);
        }

        // Auto media type
        bool isAutoMediaType = selectedCamera->m_useAutoMediaType;
        ImGui::SameLine(300);
        if (ImGui::Checkbox("Auto##MediaType", &isAutoMediaType)) {
            selectedCamera->m_useAutoMediaType = isAutoMediaType;
            if (isAutoMediaType) {
                selectedCamera->ChangeMediaType(selectedCamera->m_pAutoMediaType);
            }
        }

        // Draw mode
        int selectedDrawModeIndex = selectedCamera->m_drawMode;
        if (ImGui::BeginCombo("Draw Mode", DRAW_MODE_LABELS[selectedCamera->m_drawMode].c_str())) {
            for (size_t i = 0; i < DRAW_MODE_SIZE; i++) {
                const bool is_selected = (selectedDrawModeIndex == (int) i);
                if (ImGui::Selectable(DRAW_MODE_LABELS[i].c_str(), is_selected)) {
                    selectedDrawModeIndex = i;
                }

                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Stretch: direct copy from source to destination\n\n"
            "Crop: Keep aspect ratio (16:9) and cut off horizontally or vertically\n\n"
            "Letterbox: Keep aspect ratio (16:9) and add black space horizontally or vertically\n\n"
            "Crop to 4:3: Crop to display as 4:3\n\n"
            "Letterbox to 4:3: Like Letterbox, but target 4:3"
            );

        if (selectedDrawModeIndex != selectedCamera->m_drawMode) {
            selectedCamera->m_drawMode = (LocalCameraDrawMode)selectedDrawModeIndex;
            selectedCamera->UpdateDrawRect();
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Checkbox("Horizontal Flip", &selectedCamera->m_flipHorizontal);
        ImGui::SameLine();
        ImGui::Checkbox("Vertical Flip", &selectedCamera->m_flipVertical);

        // Camera control parameters
        ImGui::Separator();
        ImGui::Text("Camera control");

        // some high end webcams store settings on its onboard memory, with the user configuring it
        // via proprietary software outside of the game, so don't mess with it unless the user
        // explicitly wants to change things here
        ImGui::Checkbox("Allow manual control", &selectedCamera->m_allowManualControl);

        ImGui::BeginDisabled(!selectedCamera->m_allowManualControl);
        IAMCameraControl *pCameraControl = selectedCamera->GetCameraControl();
        if (pCameraControl) {
            for (size_t i = 0; i < CAMERA_CONTROL_PROP_SIZE; i++) {
                CameraControlProp prop = {};
                selectedCamera->GetCameraControlProp(i, &prop);

                auto value = prop.value;

                bool isDisabled = (prop.defFlags == 0 || prop.valueFlags & CameraControl_Flags_Auto);
                ImGui::BeginDisabled(isDisabled);

                int sliderFlag = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat;

                bool isDefAuto = prop.defFlags & CameraControl_Flags_Auto;

                if (ImGui::SliderInt(CAMERA_CONTROL_LABELS[i].c_str(), (int*) &value, prop.minValue, prop.maxValue, "%d", sliderFlag)) {
                    selectedCamera->SetCameraControlProp(i, value, prop.valueFlags);
                }

                ImGui::EndDisabled();

                if (isDefAuto) {
                    ImGui::SameLine(300);
                    if (ImGui::CheckboxFlags(("Auto##" + CAMERA_CONTROL_LABELS[i]).c_str(), (int*) &prop.valueFlags, CameraControl_Flags_Auto)) {
                        selectedCamera->SetCameraControlProp(i, value, prop.valueFlags);
                    }
                }
            }
        }

        ImGui::Separator();
        
        // reset button
        if (ImGui::Button("Reset")) {
            selectedCamera->ResetCameraControlProps();
        }

        ImGui::EndDisabled();

        // save button
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            this->config_save();
        }
    }

    void CameraControl::config_save() {
        camera_config_save();
    }
}

#endif
