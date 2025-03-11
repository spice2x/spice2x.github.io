#include "acio_status_buffers.h"
#include "overlay/imgui/extensions.h"
#include "external/imgui/imgui_memory_editor.h"


namespace overlay::windows {

    ACIOStatusBuffers::ACIOStatusBuffers(SpiceOverlay *overlay, acio::ACIOModule *module)
    : Window(overlay), module(module) {
        this->title = module->name + " Status Buffers";
        this->init_size = ImVec2(600, 400);
        this->init_pos = ImVec2(
                ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2);
        this->active = true;

        // configure editor defaults
        this->editor = new MemoryEditor();
        this->editor->OptShowDataPreview = true;
        this->editor->PreviewDataType = ImGuiDataType_U16;
    }

    ACIOStatusBuffers::~ACIOStatusBuffers() {

        // kill editor
        delete this->editor;
    }

    void ACIOStatusBuffers::build_content() {

        // freeze checkbox
        if (module->status_buffer_freeze) {
            ImGui::Checkbox("Freeze", module->status_buffer_freeze);
            ImGui::SameLine();
            ImGui::HelpMarker("Prevent automatic modifications to the buffer.");
            ImGui::Separator();
        }

        // draw editor
        this->editor->DrawContents(
                this->module->status_buffer,
                this->module->status_buffer_size);
    }
}
