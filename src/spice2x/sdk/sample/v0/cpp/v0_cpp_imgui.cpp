#include "v0_cpp_imgui.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cfloat>
#include <windows.h>

#include "imgui.h"
#include "backends/imgui_impl_dx9.h"

namespace sample_imgui {
namespace {

struct SampleRenderer {
    ImGuiContext *context = nullptr;
    bool backend_initialized = false;
    bool was_visible = false;
    bool dummy_enabled = true;
    float dummy_value = 0.5f;
    int32_t dummy_mode = 0;
    uint32_t click_count = 0;
    std::chrono::steady_clock::time_point previous_frame;
};

SampleRenderer renderer;
std::atomic_bool window_visible = false;

void update_mouse(const SPICE_SDK_D3D9_FRAME &frame) {
    auto &io = ImGui::GetIO();
    const auto window = static_cast<HWND>(frame.window);
    const bool focused = window &&
        GetAncestor(GetForegroundWindow(), GA_ROOT) == GetAncestor(window, GA_ROOT);
    io.AddFocusEvent(focused);

    POINT position{};
    RECT client{};

    // can window receive mouse input?
    const bool mouse_available =
        focused &&
        GetCursorPos(&position) &&
        ScreenToClient(window, &position) &&
        GetClientRect(window, &client) &&
        client.right > 0 &&
        client.bottom > 0;

    // add mouse position
    if (mouse_available) {
        io.AddMousePosEvent(
            static_cast<float>(position.x) * frame.width / client.right,
            static_cast<float>(position.y) * frame.height / client.bottom);
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    // add click to imgui
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    io.AddMouseButtonEvent(0, mouse_available &&
        ((GetAsyncKeyState(swapped ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0));
    io.AddMouseButtonEvent(1, mouse_available &&
        ((GetAsyncKeyState(swapped ? VK_LBUTTON : VK_RBUTTON) & 0x8000) != 0));
}

void draw_window(SampleRenderer &state, const SPICE_SDK_D3D9_FRAME &frame) {
    auto &io = ImGui::GetIO();

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.8f);

    if (ImGui::Begin("SDK sample", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {

        ImGui::Text("%u x %u", frame.width, frame.height);
        ImGui::Text("%.1f FPS", io.Framerate);

        ImGui::Separator();

        ImGui::Checkbox("Enable dummy widgets", &state.dummy_enabled);

        ImGui::BeginDisabled(!state.dummy_enabled);
        {
            ImGui::SliderFloat("Value", &state.dummy_value, 0.0f, 1.0f, "%.2f",
                ImGuiSliderFlags_NoInput);
            ImGui::Combo("Mode", &state.dummy_mode, "Off\0Low\0High\0");
            if (ImGui::Button("Increment")) {
                ++state.click_count;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                state.click_count = 0;
                state.dummy_value = 0.5f;
                state.dummy_mode = 0;
            }
            ImGui::Text("Count: %u", state.click_count);
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void draw_frame(SampleRenderer &state, const SPICE_SDK_D3D9_FRAME &frame) {
    if (!state.backend_initialized) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(now - state.previous_frame).count();
    state.previous_frame = now;
    const bool visible = window_visible.load(std::memory_order_relaxed);
    if (!visible && !state.was_visible) {
        return;
    }
    state.was_visible = visible;

    // update imgui state
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(frame.width), static_cast<float>(frame.height));
    io.DeltaTime = elapsed > 0.0f ? elapsed : 1.0f / 60.0f;

    // update mouse i/o
    if (visible) {
        update_mouse(frame);
    } else {
        io.ClearEventsQueue();
        io.ClearInputMouse();
        io.AddFocusEvent(false);
    }

    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    if (!visible) {
        ImGui::SetWindowFocus(nullptr);
        ImGui::EndFrame();
        return;
    }

    draw_window(state, frame);
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void __cdecl render_callback(
    SPICE_SDK_D3D9_EVENT event,
    const SPICE_SDK_D3D9_FRAME *frame,
    void *userdata) {

    auto &state = *static_cast<SampleRenderer *>(userdata);
    auto *previous_context = ImGui::GetCurrentContext();

    // init imgui context
    if (event == SPICE_SDK_D3D9_READY && !state.context) {
        ImGui::SetAllocatorFunctions(
            [](size_t size, void *) -> void * { return std::malloc(size); },
            [](void *memory, void *) { std::free(memory); });
        IMGUI_CHECKVERSION();
        state.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(state.context);
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.MouseDrawCursor = true;
        ImGui::StyleColorsDark();
        state.backend_initialized = ImGui_ImplDX9_Init(
            static_cast<IDirect3DDevice9 *>(frame->device));
    }

    if (state.context) {
        ImGui::SetCurrentContext(state.context);
        switch (event) {
            case SPICE_SDK_D3D9_READY:
                state.previous_frame = std::chrono::steady_clock::now();
                if (state.backend_initialized) {
                    ImGui_ImplDX9_CreateDeviceObjects();
                }
                break;
            case SPICE_SDK_D3D9_DRAW:
                draw_frame(state, *frame);
                break;
            case SPICE_SDK_D3D9_INVALIDATE:
                if (state.backend_initialized) {
                    ImGui_ImplDX9_InvalidateDeviceObjects();
                }
                break;
            case SPICE_SDK_D3D9_DESTROY:
                if (state.backend_initialized) {
                    ImGui_ImplDX9_Shutdown();
                }
                ImGui::DestroyContext(state.context);
                state.context = nullptr;
                state.backend_initialized = false;
                state.was_visible = false;
                break;
        }
    }

    ImGui::SetCurrentContext(previous_context);
}

}

SPICE_SDK_STATUS_CODE initialize(const SPICE_SDK_V0 &spice) {
    if (!spice.register_d3d9) {
        return SPICE_SDK_STATUS_NOT_SUPPORTED;
    }
    return spice.register_d3d9(render_callback, &renderer);
}

void toggle() {
    const bool visible = !window_visible.load(std::memory_order_relaxed);
    window_visible.store(visible, std::memory_order_relaxed);
}

}