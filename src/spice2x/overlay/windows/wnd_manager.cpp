#include "wnd_manager.h"
#include "hooks/graphics/graphics.h"
#include "util/logging.h"

namespace overlay::windows {

    WndManagerWindow::WndManagerWindow(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Window Manager";
        this->flags |= ImGuiWindowFlags_AlwaysAutoResize;
        this->init_pos = ImVec2(
                ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2);
        this->size_min = ImVec2(400, 400);
        this->active = true;
    }

    WndManagerWindow::~WndManagerWindow() {
    }

    static std::string hwnd_preview(int index, HWND hwnd) {
        char hwnd_title[256];
        if (GetWindowText(hwnd, hwnd_title, sizeof(hwnd_title)) > 0) {
            return hwnd_title;
        } else {
            return fmt::format("{}: {}", index, (void*) hwnd);
        }
    }

    void WndManagerWindow::build_content() {

        // get current window
        auto &windows_list = GRAPHICS_WINDOWS;
        HWND hwnd_current = 0;
        std::string preview = "None";
        if (this->window_current >= (int) windows_list.size()) {
            this->window_current = windows_list.size() - 1;
        }
        if (this->window_current >= 0) {
            hwnd_current = windows_list[this->window_current];
            preview = hwnd_preview(this->window_current, hwnd_current);
        }

        // window selection
        if (ImGui::BeginCombo("Window Selection", preview.c_str(), 0)) {
            size_t count = 0;
            for (auto &hwnd : windows_list) {
                bool selected = hwnd_current == hwnd;
                auto cur_preview = hwnd_preview(count, hwnd);
                if (ImGui::Selectable(cur_preview.c_str(), selected)) {
                    this->window_current = count;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                count++;
            }
            ImGui::EndCombo();
        }

        // window information
        ImGui::Separator();
        if (hwnd_current == 0) {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                    "Please select a window first...");
        } else {

            // window information
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Information")) {
                static struct {
                    const char *desc;
                    int index;
                } INFORMATION [] {
                        { .desc = "GWL_EXSTYLE", .index = GWL_EXSTYLE },
                        { .desc = "GWLP_HINSTANCE", .index = -6 },
                        { .desc = "GWLP_HWNDPARENT", .index = -8 },
                        { .desc = "GWLP_ID", .index = GWL_ID },
                        { .desc = "GWL_STYLE", .index = GWL_STYLE },
                        { .desc = "GWLP_USERDATA", .index = -21 },
                        { .desc = "GWLP_WNDPROC", .index = -4 },
                };

                // columns header
                ImGui::Columns(2);
                ImGui::TextUnformatted("Index"); ImGui::NextColumn();
                ImGui::TextUnformatted("Value"); ImGui::NextColumn();

                // add information
                ImGui::Separator();
                for (auto &entry : INFORMATION) {

                    // index
                    ImGui::TextUnformatted(entry.desc);
                    ImGui::NextColumn();

                    // value
                    ImGui::Text("%p", (void*) GetWindowLongPtr(hwnd_current, entry.index));
                    ImGui::NextColumn();
                }

                // end columns
                ImGui::Columns();
            }

            // size information
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Sizes")) {

                // window rect
                RECT hwnd_rect {};
                if (GetWindowRect(hwnd_current, &hwnd_rect)) {
                    ImGui::Text("Window Rect: %ld %ld %ld %ld - %ld %ld",
                            hwnd_rect.left, hwnd_rect.top, hwnd_rect.right, hwnd_rect.bottom,
                            hwnd_rect.right - hwnd_rect.left, hwnd_rect.bottom - hwnd_rect.top);

                    // client rect
                    RECT client_rect {};
                    if (GetClientRect(hwnd_current, &client_rect)) {
                        ImGui::Text("Client Rect: %ld %ld %ld %ld - %ld %ld",
                                    client_rect.left, client_rect.top, client_rect.right, client_rect.bottom,
                                    client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
                        ImGui::Text("Decoration Size: %ld %ld",
                                    (hwnd_rect.right - hwnd_rect.left) - (client_rect.right - client_rect.left),
                                    (hwnd_rect.bottom - hwnd_rect.top) - (client_rect.bottom - client_rect.top));
                    }
                }
            }

            // position information
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Positions")) {

                // window position
                RECT hwnd_rect {};
                if (GetWindowRect(hwnd_current, &hwnd_rect)) {
                    ImGui::Text("Window Position: %ld %ld",
                                hwnd_rect.left, hwnd_rect.top);
                }

                // cursor position
                POINT cursor_pos;
                if (GetCursorPos(&cursor_pos)) {
                    ImGui::Text("Cursor Position: %ld %ld",
                            cursor_pos.x, cursor_pos.y);
                    if (ScreenToClient(hwnd_current, &cursor_pos)) {
                        ImGui::Text("Cursor Client Position: %ld %ld",
                                cursor_pos.x, cursor_pos.y);
                    }
                }
            }
        }
    }
}
