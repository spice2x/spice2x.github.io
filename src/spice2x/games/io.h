#pragma once

#include <vector>
#include "cfg/api.h"

namespace games {

    namespace OverlayButtons {
        enum {
            Screenshot,
            ToggleMainMenu,
            ToggleSubScreen,
            InsertCoin,
            ToggleIOPanel,
            ToggleConfig,
            ToggleVirtualKeypadP1,
            ToggleVirtualKeypadP2,
            ToggleCardManager,
            ToggleLog,
            ToggleControl,
            TogglePatchManager,
            ToggleScreenResize,
            ToggleOverlay,
            ToggleCameraControl,
            TriggerPinMacroP1,
            TriggerPinMacroP2,
            ScreenResize,
            ScreenResizeScene1,
            ScreenResizeScene2,
            ScreenResizeScene3,
            ScreenResizeScene4,
            SuperExit,
            NavigatorActivate,
            NavigatorCancel,
            NavigatorUp,
            NavigatorDown,
            NavigatorLeft,
            NavigatorRight,
            HotkeyEnable1,
            HotkeyEnable2,
            HotkeyToggle,
        };
    }

    namespace KeypadButtons {
        enum {
            Keypad0,
            Keypad1,
            Keypad2,
            Keypad3,
            Keypad4,
            Keypad5,
            Keypad6,
            Keypad7,
            Keypad8,
            Keypad9,
            Keypad00,
            KeypadDecimal,
            InsertCard,
            Size,
        };
    }

    const std::vector<std::string> &get_games();
    std::vector<Button> *get_buttons(const std::string &game);
    std::string get_buttons_help(const std::string &game);
    std::vector<Button> *get_buttons_keypads(const std::string &game);
    std::vector<Button> *get_buttons_overlay(const std::string &game);
    std::vector<Analog> *get_analogs(const std::string &game);
    std::vector<Light> *get_lights(const std::string &game);
    std::vector<Option> *get_options(const std::string &game);
    std::vector<std::string> *get_game_file_hints(const std::string &game);
}
