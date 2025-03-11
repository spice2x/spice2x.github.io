#include "midi.h"
#include "launcher/launcher.h"
#include "util/logging.h"

namespace overlay::windows {

    static std::string midi_cmd_str(uint8_t cmd) {
        const char *name = "UNKNOWN";
        switch (cmd) {
            case 0x8:
                name = "NOTE OFF";
                break;
            case 0x9:
                name = "NOTE ON";
                break;
            case 0xA:
                name = "POLY.PRESS.";
                break;
            case 0xB:
                name = "CTRL CHANGE";
                break;
            case 0xC:
                name = "PRG CHANGE";
                break;
            case 0xD:
                name = "CHAN.PRESS.";
                break;
            case 0xE:
                name = "PITCH BEND";
                break;
            case 0xF:
                name = "SYSTEM";
                break;
        }
        return fmt::format("{} (0x{:X})", name, cmd);
    }

    MIDIWindow::MIDIWindow(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "MIDI Control";
        this->init_size = ImVec2(
                ImGui::GetIO().DisplaySize.x * 0.8f,
                ImGui::GetIO().DisplaySize.y * 0.8f);
        this->size_min = ImVec2(250, 200);
        this->init_pos = ImVec2(
                ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2);
        this->active = true;

        // add hook for receiving midi messages
        RI_MGR->add_callback_midi(this, MIDIWindow::midi_hook);
    }

    MIDIWindow::~MIDIWindow() {
        RI_MGR->remove_callback_midi(this, MIDIWindow::midi_hook);
    }

    void MIDIWindow::build_content() {

        // reset button
        if (ImGui::Button("Reset")) {
            this->midi_data.clear();
        }

        // autoscroll checkbox
        ImGui::SameLine();
        ImGui::Checkbox("Autoscroll", &this->autoscroll);

        // log section
        ImGui::BeginChild("MidiLog", ImVec2(), false);

        // header
        ImGui::Columns(5, "MidiLogColumns", true);
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Device"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Command"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Channel"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Data 1"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0, 1), "Data 2"); ImGui::NextColumn();

        // data
        ImGui::Separator();
        for (auto &data : this->midi_data) {

            // set color
            srand(data.device->id * 2111);
            float hue = ((float) rand()) / ((float) RAND_MAX);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(hue, 0.8f, 0.8f, 1.f).Value);

            // data cells
            ImGui::Text("%i: %s", (int) data.device->id, data.device->desc.c_str());
            ImGui::NextColumn();
            ImGui::Text("%s", midi_cmd_str(data.cmd).c_str());
            ImGui::NextColumn();
            ImGui::Text("0x%X - Ch%i", data.ch, data.ch + 1);
            ImGui::NextColumn();
            ImGui::Text("0x%02X", data.b1);
            ImGui::NextColumn();
            ImGui::Text("0x%02X", data.b2);
            ImGui::NextColumn();

            // clean up
            ImGui::PopStyleColor();
        }

        // autoscroll
        if (this->autoscroll_apply) {
            this->autoscroll_apply = false;
            ImGui::SetScrollHereY(1.f);
        }

        // clean up section
        ImGui::Columns();
        ImGui::EndChild();
    }

    void MIDIWindow::midi_hook(void *user, rawinput::Device *device,
            uint8_t cmd, uint8_t ch, uint8_t b1, uint8_t b2) {
        auto This = (MIDIWindow*) user;
        This->midi_data.emplace_back(MIDIData {
            .device = device,
            .cmd = cmd,
            .ch = ch,
            .b1 = b1,
            .b2 = b2,
        });
        if (This->autoscroll) {
            This->autoscroll_apply = true;
        }
    }
}
