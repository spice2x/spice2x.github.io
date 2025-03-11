#include "overlay.h"

#include "avs/game.h"
#include "cfg/configurator.h"
#include "games/io.h"
#include "games/iidx/iidx.h"
#include "hooks/graphics/graphics.h"
#include "misc/eamuse.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/resutils.h"
#include "build/resource.h"

#include "imgui/impl_dx9.h"
#include "imgui/impl_spice.h"
#include "imgui/impl_sw.h"
#include "overlay/imgui/impl_dx9.h"
#include "overlay/imgui/impl_spice.h"
#include "overlay/imgui/impl_sw.h"

#include "window.h"
#ifdef SPICE64
#include "windows/camera_control.h"
#endif
#include "windows/card_manager.h"
#include "windows/screen_resize.h"
#include "windows/config.h"
#include "windows/control.h"
#include "windows/fps.h"
#include "windows/generic_sub.h"
#include "windows/iidx_seg.h"
#include "windows/iidx_sub.h"
#include "windows/drs_dancefloor.h"
#include "windows/iopanel.h"
#include "windows/iopanel_ddr.h"
#include "windows/iopanel_gfdm.h"
#include "windows/iopanel_iidx.h"
#include "windows/sdvx_sub.h"
#include "windows/keypad.h"
#include "windows/log.h"
#include "windows/patch_manager.h"
#include "windows/exitprompt.cpp"

static inline ImVec4 operator*(const ImVec4& lhs, const ImVec4& rhs) \
    { return ImVec4(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w); }

namespace overlay {

    // settings
    bool ENABLED = true;
    bool AUTO_SHOW_FPS = false;
    bool AUTO_SHOW_SUBSCREEN = false;
    bool AUTO_SHOW_IOPANEL = false;
    bool AUTO_SHOW_KEYPAD_P1 = false;
    bool AUTO_SHOW_KEYPAD_P2 = false;

    bool USE_WM_CHAR_FOR_IMGUI_CHAR_INPUT = false;

    bool FPS_SHOULD_FLIP = false;

    // global
    std::mutex OVERLAY_MUTEX;
    std::unique_ptr<overlay::SpiceOverlay> OVERLAY = nullptr;
    ImFont* DSEG_FONT = nullptr;
}

static void *ImGui_Alloc(size_t sz, void *user_data) {
    void *data = malloc(sz);
    if (!data) {
        return nullptr;
    }

    memset(data, 0, sz);

    return data;
}

static void ImGui_Free(void *data, void *user_data) {
    free(data);
}

void overlay::create_d3d9(HWND hWnd, IDirect3D9 *d3d, IDirect3DDevice9 *device) {
    if (!overlay::ENABLED) {
        return;
    }

    const std::lock_guard<std::mutex> lock(OVERLAY_MUTEX);

    if (!overlay::OVERLAY) {
        overlay::OVERLAY = std::make_unique<overlay::SpiceOverlay>(hWnd, d3d, device);
    }
}

void overlay::create_software(HWND hWnd) {
    if (!overlay::ENABLED) {
        return;
    }

    const std::lock_guard<std::mutex> lock(OVERLAY_MUTEX);

    if (!overlay::OVERLAY) {
        overlay::OVERLAY = std::make_unique<overlay::SpiceOverlay>(hWnd);
    }
}

void overlay::destroy(HWND hWnd) {
    if (!overlay::ENABLED) {
        return;
    }

    const std::lock_guard<std::mutex> lock(OVERLAY_MUTEX);

    if (overlay::OVERLAY && (hWnd == nullptr || overlay::OVERLAY->uses_window(hWnd))) {
        overlay::OVERLAY.reset();
    }
}

overlay::SpiceOverlay::SpiceOverlay(HWND hWnd, IDirect3D9 *d3d, IDirect3DDevice9 *device)
        : renderer(OverlayRenderer::D3D9), hWnd(hWnd), d3d(d3d), device(device) {
    log_info("overlay", "initializing (D3D9)");

    // increment reference counts
    this->d3d->AddRef();
    this->device->AddRef();

    // get creation parameters
    HRESULT ret;
    ret = this->device->GetCreationParameters(&this->creation_parameters);
    if (FAILED(ret)) {
        log_fatal("overlay", "GetCreationParameters failed, hr={}", FMT_HRESULT(ret));
    }

    // get adapter identifier
    ret = this->d3d->GetAdapterIdentifier(
            creation_parameters.AdapterOrdinal,
            0,
            &this->adapter_identifier);
    if (FAILED(ret)) {
        log_fatal("overlay", "GetAdapterIdentifier failed, hr={}", FMT_HRESULT(ret));
    }

    // init
    this->init();
}

overlay::SpiceOverlay::SpiceOverlay(HWND hWnd)
        : renderer(OverlayRenderer::SOFTWARE), hWnd(hWnd) {
    log_info("overlay", "initializing (SOFTWARE)");

    // init
    this->init();
}

void overlay::SpiceOverlay::init() {

    // init imgui
    IMGUI_CHECKVERSION();
    ImGui::SetAllocatorFunctions(ImGui_Alloc, ImGui_Free, nullptr);
    ImGui::CreateContext();
    ImGui::GetIO();

    // set style
    ImGui::StyleColorsDark();
    if (this->renderer == OverlayRenderer::SOFTWARE) {
        imgui_sw::make_style_fast();
        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Border].w = 0;
        colors[ImGuiCol_Separator].w = 0.25f;
    } else {
        auto &style = ImGui::GetStyle();
        style.WindowRounding = 0;
    }

    // red theme based on:
    // https://github.com/ocornut/imgui/issues/707#issuecomment-760220280
    // r, g, b, a
    ImVec4* colors = ImGui::GetStyle().Colors;
    // colors[ImGuiCol_Text]                   = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    // colors[ImGuiCol_TextDisabled]           = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);

    // colors[ImGuiCol_WindowBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.94f);
    // colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.f, 0.f, 0.94f);

    colors[ImGuiCol_Border]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.37f, 0.14f, 0.00f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.37f, 0.14f, 0.14f, 0.67f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.39f, 0.20f, 0.20f, 0.67f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.48f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.48f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.56f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(1.00f, 0.19f, 0.19f, 0.40f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.89f, 0.00f, 0.19f, 1.00f);

    colors[ImGuiCol_Button]                 = ImVec4(1.00f, 0.19f, 0.19f, 0.40f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.80f, 0.17f, 0.00f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.89f, 0.00f, 0.19f, 1.00f);

    colors[ImGuiCol_Header]                 = ImVec4(0.33f, 0.35f, 0.36f, 0.53f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.76f, 0.28f, 0.44f, 0.67f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.47f, 0.47f, 0.47f, 0.67f);
    colors[ImGuiCol_Separator]              = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);

    colors[ImGuiCol_Tab]                    = colors[ImGuiCol_Button];
    colors[ImGuiCol_TabHovered]             = colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_TabActive]              = colors[ImGuiCol_ButtonActive];
    colors[ImGuiCol_TabUnfocused]           = colors[ImGuiCol_Tab] * ImVec4(1.0f, 1.0f, 1.0f, 0.6f);
    colors[ImGuiCol_TabUnfocusedActive]     = colors[ImGuiCol_TabActive] * ImVec4(1.0f, 1.0f, 1.0f, 0.6f);

    colors[ImGuiCol_DockingPreview]         = ImVec4(0.47f, 0.47f, 0.47f, 0.47f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    // configure IO
    auto &io = ImGui::GetIO();
    io.UserData = this;
    io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard
                     | ImGuiConfigFlags_NavEnableGamepad
                     | ImGuiConfigFlags_NavEnableSetMousePos
                     | ImGuiConfigFlags_DockingEnable
                     | ImGuiConfigFlags_ViewportsEnable;
    if (is_touch_available()) {
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    }

    io.MouseDrawCursor = !GRAPHICS_SHOW_CURSOR;

    // disable config
    io.IniFilename = nullptr;

    // allow CTRL+WHEEL scaling
    io.FontAllowUserScaling = true;

    // add default font
    io.Fonts->AddFontDefault();

    // add fallback fonts for missing glyph ranges
    ImFontConfig config {};
    config.MergeMode = true;
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\simsun.ttc)",
            13.0f, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\arial.ttf)",
            13.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\meiryu.ttc)",
            13.0f, &config, io.Fonts->GetGlyphRangesJapanese());
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\meiryo.ttc)",
            13.0f, &config, io.Fonts->GetGlyphRangesJapanese());
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\gulim.ttc)",
            13.0f, &config, io.Fonts->GetGlyphRangesKorean());
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\cordia.ttf)",
            13.0f, &config, io.Fonts->GetGlyphRangesThai());
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\arial.ttf)",
            13.0f, &config, io.Fonts->GetGlyphRangesVietnamese());

    // add special font
    if (avs::game::is_model("LDJ")) {
        DWORD size;
        ImFontConfig config {};
        config.FontDataOwnedByAtlas = false;
        auto buffer = resutil::load_file(IDR_DSEGFONT, &size);
        DSEG_FONT = io.Fonts->AddFontFromMemoryTTF((void *)buffer, size,
            overlay::windows::IIDX_SEGMENT_FONT_SIZE);
    }

    // init implementation
    ImGui_ImplSpice_Init(this->hWnd);
    switch (this->renderer) {
        case OverlayRenderer::D3D9:
            ImGui_ImplDX9_Init(this->device);
            break;
        case OverlayRenderer::SOFTWARE:
            imgui_sw::bind_imgui_painting();
            break;
    }

    // create empty frame
    switch (this->renderer) {
        case OverlayRenderer::D3D9:
            ImGui_ImplDX9_NewFrame();
            break;
        case OverlayRenderer::SOFTWARE:
            break;
    }
    ImGui_ImplSpice_NewFrame();
    ImGui::NewFrame();
    ImGui::EndFrame();

    // fix navigation buttons causing crash on overlay activation
    ImGui::Begin("Temp");
    ImGui::End();

    bool set_overlay_active = false;

    // referenced windows
    this->window_add(window_fps = new overlay::windows::FPS(this));
    if (!cfg::CONFIGURATOR_STANDALONE && AUTO_SHOW_FPS) {
        window_fps->set_active(true);
        set_overlay_active = true;
    }

    this->window_add(window_main_menu = new overlay::windows::ExitPrompt(this));

    // add default windows
    this->window_add(window_config = new overlay::windows::Config(this));
    this->window_add(window_control = new overlay::windows::Control(this));
    this->window_add(window_log = new overlay::windows::Log(this));
#ifdef SPICE64
    if (avs::game::is_model("LDJ")) {
        this->window_add(window_camera = new overlay::windows::CameraControl(this));
    }
#endif
    this->window_add(window_cards = new overlay::windows::CardManager(this));
    if (!cfg::CONFIGURATOR_STANDALONE) {
        this->window_add(window_resize = new overlay::windows::ScreenResize(this));
    }
    this->window_add(new overlay::windows::PatchManager(this));

    {
        window_keypad1 = new overlay::windows::Keypad(this, 0);
        this->window_add(window_keypad1);
        if (!cfg::CONFIGURATOR_STANDALONE && AUTO_SHOW_KEYPAD_P1) {
            window_keypad1->set_active(true);
            set_overlay_active = true;
        }
    }
    if (eamuse_get_game_keypads() > 1) {
        window_keypad2 = new overlay::windows::Keypad(this, 1);
        this->window_add(window_keypad2);
        if (!cfg::CONFIGURATOR_STANDALONE && AUTO_SHOW_KEYPAD_P2) {
            window_keypad2->set_active(true);
            set_overlay_active = true;
        }
    }

    // IO panel needs to know what game is running
    if (!cfg::CONFIGURATOR_STANDALONE) {
        window_iopanel = nullptr;
        if (avs::game::is_model("LDJ")) {
            window_iopanel = new overlay::windows::IIDXIOPanel(this);
        } else if (avs::game::is_model("MDX")) {
            window_iopanel = new overlay::windows::DDRIOPanel(this);
        } else if (avs::game::is_model({"J32", "J33", "K32", "K33", "L32", "L33", "M32"})) {
            window_iopanel = new overlay::windows::GitadoraIOPanel(this);
        } else {
            window_iopanel = new overlay::windows::IOPanel(this);
        }
        if (window_iopanel) {
            this->window_add(window_iopanel);
            if (AUTO_SHOW_IOPANEL) {
                window_iopanel->set_active(true);
                set_overlay_active = true;
            }
        }
    }

    // subscreens need DirectX, so don't try to initialize them in standalone
    if (!cfg::CONFIGURATOR_STANDALONE) {
        window_sub = nullptr;
        if (avs::game::is_model("LDJ")) {
            if (games::iidx::TDJ_MODE) {
                window_sub = new overlay::windows::IIDXSubScreen(this);
            } else {
                window_sub = new overlay::windows::IIDXSegmentDisplay(this);
            }
        } else if (avs::game::is_model("REC")) {
            window_sub = new overlay::windows::DRSDanceFloorDisplay(this);
        } else if (avs::game::is_model("KFC")) {
            window_sub = new overlay::windows::SDVXSubScreen(this);
        }
        if (window_sub) {
            this->window_add(window_sub);
            if (AUTO_SHOW_SUBSCREEN) {
                window_sub->set_active(true);
                set_overlay_active = true;
            }
        }
    }

    if (set_overlay_active) {
        this->set_active(true);
    }
}

overlay::SpiceOverlay::~SpiceOverlay() {

    // imgui shutdown
    ImGui_ImplSpice_Shutdown();
    switch (this->renderer) {
        case OverlayRenderer::D3D9:
            ImGui_ImplDX9_Shutdown();

            // drop references
            this->device->Release();
            this->d3d->Release();

            break;
        case OverlayRenderer::SOFTWARE:
            imgui_sw::unbind_imgui_painting();
            break;
    }
    ImGui::DestroyContext();
}

void overlay::SpiceOverlay::window_add(Window *wnd) {
    this->windows.emplace_back(std::unique_ptr<Window>(wnd));
}

void overlay::SpiceOverlay::new_frame() {

    // update implementation
    ImGui_ImplSpice_NewFrame();
    this->total_elapsed += ImGui::GetIO().DeltaTime;

    // check if inactive
    if (!this->active) {
        return;
    }

    // init frame
    switch (this->renderer) {
        case OverlayRenderer::D3D9:
            ImGui_ImplDX9_NewFrame();
            break;
        case OverlayRenderer::SOFTWARE:
            ImGui_ImplSpice_UpdateDisplaySize();
            break;
    }
    ImGui::NewFrame();

    // build windows
    for (auto &window : this->windows) {
        window->build();
    }

    // end frame
    ImGui::EndFrame();
}

void overlay::SpiceOverlay::render() {

    // check if inactive
    if (!this->active) {
        return;
    }

    // imgui render
    ImGui::Render();

    // implementation render
    switch (this->renderer) {
        case OverlayRenderer::D3D9:
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            break;
        case OverlayRenderer::SOFTWARE: {

            // get display metrics
            auto &io = ImGui::GetIO();
            auto width = static_cast<size_t>(std::ceil(io.DisplaySize.x));
            auto height = static_cast<size_t>(std::ceil(io.DisplaySize.y));
            auto pixels = width * height;

            // make sure buffer is big enough
            if (this->pixel_data.size() < pixels) {
                this->pixel_data.resize(pixels, 0);
            }

            // reset buffer
            memset(&this->pixel_data[0], 0, width * height * sizeof(uint32_t));

            // render to pixel data
            imgui_sw::SwOptions options {
                .optimize_text = true,
                .optimize_rectangles = true,
            };
            imgui_sw::paint_imgui(&this->pixel_data[0], width, height, options);
            pixel_data_width = width;
            pixel_data_height = height;

            break;
        }
    }

    for (auto &window : this->windows) {
        window->after_render();
    }
}

void overlay::SpiceOverlay::update() {

    // check overlay toggle
    auto overlay_buttons = games::get_buttons_overlay(eamuse_get_game());
    bool toggle_down_new = overlay_buttons
            && this->hotkeys_triggered()
            && GameAPI::Buttons::getState(RI_MGR, overlay_buttons->at(games::OverlayButtons::ToggleOverlay));
    if (toggle_down_new && !this->toggle_down) {
        toggle_active(true);
    }
    this->toggle_down = toggle_down_new;

    // check main menu
    const auto main_menu_down_new = overlay_buttons
            && this->hotkeys_triggered()
            && GameAPI::Buttons::getState(RI_MGR, overlay_buttons->at(games::OverlayButtons::ToggleMainMenu));
    if (main_menu_down_new && !this->main_menu_down) {
        show_main_menu();
    }
    this->main_menu_down = main_menu_down_new;

    // update windows
    for (auto &window : this->windows) {
        window->update();
    }

    // deactivate if no windows are shown
    bool window_active = false;
    for (auto &window : this->windows) {
        if (window->get_active()) {
            window_active = true;
            break;
        }
    }
    if (!window_active) {
        this->set_active(false);
    }
}

bool overlay::SpiceOverlay::update_cursor() {
    return ImGui_ImplSpice_UpdateMouseCursor();
}

void overlay::SpiceOverlay::toggle_active(bool overlay_key) {

    // invert active state
    this->active = !this->active;

    // get rid of main menu if it was visible
    if (this->window_main_menu) {
        this->window_main_menu->set_active(false);
    }

    // show FPS window if toggled with overlay key
    if (overlay_key) {
        this->window_fps->set_active(this->active);
    }
}

void overlay::SpiceOverlay::show_main_menu() {
    if (!this->window_main_menu) {
        return;
    }
    if (this->window_main_menu->get_active()) {
        // window already visible - close the window
        this->window_main_menu->set_active(false);
        return;
    }
    if (ImGui::IsPopupOpen(0, ImGuiPopupFlags_AnyPopup)) {
        return;
    }

    if (this->get_active()) {
        if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
            this->window_main_menu->set_active(true);
        }
    } else  {
        this->set_active(true);
        this->window_main_menu->set_active(true);
    }
}

void overlay::SpiceOverlay::set_active(bool new_active) {

    // toggle if different
    if (this->active != new_active) {
        this->toggle_active();
    }
}

bool overlay::SpiceOverlay::get_active() {
    return this->active;
}

bool overlay::SpiceOverlay::has_focus() {
    return this->get_active() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
}

bool overlay::SpiceOverlay::hotkeys_triggered() {

    // check if disabled first
    if (!this->hotkeys_enable) {
        return false;
    }

    // get buttons
    auto buttons = games::get_buttons_overlay(eamuse_get_game());
    if (!buttons) {
        return false;
    }

    auto &hotkey1 = buttons->at(games::OverlayButtons::HotkeyEnable1);
    auto &hotkey2 = buttons->at(games::OverlayButtons::HotkeyEnable2);
    auto &toggle = buttons->at(games::OverlayButtons::HotkeyToggle);

    // check hotkey toggle
    auto toggle_state = GameAPI::Buttons::getState(RI_MGR, toggle);
    if (toggle_state) {
        if (!this->hotkey_toggle_last) {
            this->hotkey_toggle_last = true;
            this->hotkey_toggle = !this->hotkey_toggle;
        }
    } else {
        this->hotkey_toggle_last = false;
    }

    // hotkey toggle overrides hotkey enable button states
    if (hotkey_toggle) {
        return true;
    }

    // check hotkey enable buttons
    bool triggered = true;
    if (hotkey1.isSet() && !GameAPI::Buttons::getState(RI_MGR, hotkey1)) {
        triggered = false;
    }
    if (hotkey2.isSet() && !GameAPI::Buttons::getState(RI_MGR, hotkey2)) {
        triggered = false;
    }
    return triggered;
}

void overlay::SpiceOverlay::reset_invalidate() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

void overlay::SpiceOverlay::reset_recreate() {
    ImGui_ImplDX9_CreateDeviceObjects();
}

void overlay::SpiceOverlay::input_char(unsigned int c) {
    // add character to ImGui
    ImGui::GetIO().AddInputCharacter(c);
}

uint32_t *overlay::SpiceOverlay::sw_get_pixel_data(int *width, int *height) {

    // check if active
    if (!this->active) {
        *width = 0;
        *height = 0;
        return nullptr;
    }

    // ensure buffer has the right size
    const size_t total_size = this->pixel_data_width * this->pixel_data_height;
    if (this->pixel_data.size() < total_size) {
        this->pixel_data.resize(total_size, 0);
    }

    // check for empty surface
    if (this->pixel_data.empty()) {
        *width = 0;
        *height = 0;
        return nullptr;
    }

    // copy and return pointer to data
    *width = this->pixel_data_width;
    *height = this->pixel_data_height;
    return &this->pixel_data[0];
}
