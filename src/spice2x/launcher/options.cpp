#include "options.h"

#include "external/tinyxml2/tinyxml2.h"
#include "util/utils.h"
#include "util/fileutils.h"

#include <fstream>

#ifdef NO_SCARD
#define WITH_SCARD 0
#else
#define WITH_SCARD 1
#endif

static const std::vector<std::string> CATEGORY_ORDER_API = {
    "SpiceCompanion and API",
    "API (Serial)",
    "API (Dev)",
    "BT5 API",
};

static const std::vector<std::string> CATEGORY_ORDER_BASIC = {
    "Game Options",
    "Common",
    "Network",
    "Graphics (Common)",
    "Graphics (Full Screen)",
    "Graphics (Windowed)",
    "Audio",
};

static const std::vector<std::string> CATEGORY_ORDER_ADVANCED = {
    "Game Options (Advanced)",
    "Overlay",
    "Network (Advanced)",
    "Performance",
    "Miscellaneous",
    "Touch Parameters",
    "I/O Options",
    "NFC Card Readers",
};
static const std::vector<std::string> CATEGORY_ORDER_DEV = {
    "Path Overrides",
    "Network (Development)",
    "Audio (Hacks)",
    "I/O Modules",
    "Development",
    "Debug Log",
};
static const std::vector<std::string> CATEGORY_ORDER_NONE = {
    ""
};

static auto format_as(OptionType f) {
    return fmt::underlying(f);
}

bool launcher::USE_CMD_OVERRIDE = false;

/*
 * Option Definitions
 * Be aware that the order must be the same as in the enum launcher::Options!
 */
static const std::vector<OptionDefinition> OPTION_DEFINITIONS = {
    {
        .title = "Override Game Executable & Disable Hooks",
        .name = "exec",
        .desc = "Path to the game DLL file.\n\n"
            "This option is typically not needed; leave it empty to allow spice to auto-detect your game.\n\n"
            "WARNING: this option also disables all game-specific hooks and turns off most I/O modules!",
        .type = OptionType::Text,
        .setting_name = "*.dll",
        .category = "Path Overrides",
    },
    {
        .title = "Open Configurator",
        .name = "cfg",
        .desc = "Opens configuration window. This can only be launched via the command line "
            "(spice -cfg or spice64 -cfg) or just launch spicecfg.exe.",
        .type = OptionType::Bool,
        .hidden = true,
        .disabled = true,
    },
    {
        .title = "(REMOVED) Open KFControl",
        .name = "kfcontrol",
        .desc = "This feature has been removed; please use an older version.",
        .type = OptionType::Bool,
        .hidden = true,
        .disabled = true,
    },
    {
        .title = "Basic Local EA Emulation",
        .name = "ea",
        .desc = "Enables the integrated local EA server, just enough to boot the game; no card in, no data saving.",
        .type = OptionType::Bool,
        .category = "Network",
    },
    {
        // ServiceURL
        .title = "EA Service URL",
        .name = "url",
        .desc = "Sets a custom service URL override.",
        .type = OptionType::Text,
        .category = "Network",
    },
    {
        .title = "PCBID",
        .name = "p",
        .desc = "Sets a custom PCBID override.",
        .type = OptionType::Text,
        .setting_name = "04040000000000000000",
        .category = "Network",
        .sensitive = true,
    },
    {
        .title = "Player 1 Card",
        .name = "card0",
        .desc = "Set a card number for reader 1. Overrides the selected card file.",
        .type = OptionType::Text,
        .setting_name = "E004010000000000",
        .category = "Network",
        .sensitive = true,
    },
    {
        .title = "Player 2 Card",
        .name = "card1",
        .desc = "Set a card number for reader 2. Overrides the selected card file.",
        .type = OptionType::Text,
        .setting_name = "E004010000000000",
        .category = "Network",
        .sensitive = true,
    },
    {
        // Player1PinMacro
        .title = "Player 1 PIN Macro",
        .name = "pinmacro0",
        .desc = "Set a PIN for Player 1 that will cause the PIN to be automatically typed when Player 1 PIN Macro overlay key is pressed.",
        .type = OptionType::Text,
        .setting_name = "1234",
        .category = "Network (Advanced)",
        .sensitive = true,
    },
    {
        // Player2PinMacro
        .title = "Player 2 PIN Macro",
        .name = "pinmacro1",
        .desc = "Set a PIN for Player 2 that will cause the PIN to be automatically typed when Player 2 PIN Macro overlay key is pressed.",
        .type = OptionType::Text,
        .setting_name = "5678",
        .category = "Network (Advanced)",
        .sensitive = true,
    },
    {
        .title = "Windowed Mode",
        .name = "w",
        .desc = "Enables windowed mode.",
        .type = OptionType::Bool,
        .category = "Graphics (Windowed)",
    },
    {
        // InjectHook
        .title = "Inject DLL Hooks",
        .name = "k",
        .desc = "Multiple files are allowed; use multiple -k flags, or in SpiceCfg, separate by "
                "space (foo.dll bar.dll baz.dll). Injects a hook by using LoadLibrary on the "
                "specified file before running the main game code.",
        .type = OptionType::Text,
        .setting_name = "a.dll b.dll c.dll",
        .category = "Common",
    },
    {
        // EarlyInjectHook
        .title = "Inject Early DLL Hooks",
        .name = "z",
        .desc = "Equivalent to 'Inject DLL Hooks' option, but ensures hooks are injected before "
                "the game module has been loaded into the process.",
        .type = OptionType::Text,
        .setting_name = "a.dll b.dll c.dll",
        .category = "Common",
    },
    {
        .title = "(REMOVED) Execute Lua Script",
        .name = "script",
        .desc = "This feature has been removed.",
        .type = OptionType::Text,
        .hidden = true,
        .category = "Miscellaneous",
        .disabled = true,
    },
    {
        .title = "Lock Cursor to Window",
        .name = "c",
        .desc = "Confines the cursor to be within the game window.",
        .type = OptionType::Bool,
        .category = "Common",
    },
    {
        .title = "Show Cursor & Touch Emulation Enable",
        .name = "s",
        .desc = "Shows the cursor in the game window; also turns on touch emulation. Do not use if you use a real touch screen.",
        .type = OptionType::Bool,
        .category = "Common",
    },
    {
        .title = "Monitor",
        .name = "monitor",
        .desc = "Sets the display that the game will be opened in, for multiple monitors.\n\n"
            "0 is the primary monitor, 1 is the second monitor, and so on.\n\n"
            "Not all games will respect this. Not recommended for multi-monitor games like Lightning Model / Valkyrie Model modes. "
            "Disable Full Screen Optimizations for best results.",
        .type = OptionType::Integer,
        .category = "Graphics (Full Screen)",
    },
    {
        .title = "Only Use One Monitor",
        .name = "graphics-force-single-adapter",
        .desc = "Force the graphics device to be opened utilizing only one adapter in multi-monitor systems.\n\n"
            "May cause unstable framerate and desyncs, especially if monitors have different refresh rates!",
        .type = OptionType::Bool,
        .category = "Graphics (Full Screen)",
    },
    {
        .title = "Force Monitor Refresh Rate",
        .name = "graphics-force-refresh",
        .desc =
            "Attempt to change the refresh rate for the primary monitor before the game boots; "
            "works in both full screen and windowed modes, but known to fail for some games.",
        .type = OptionType::Integer,
        .category = "Graphics (Common)",
    },
    {
        // FullscreenResolution
        .title = "Force Full Screen Resolution",
        .name = "forceres",
        .desc =
            "For full screen mode, forcibly set a custom resolution.\n\n"
            "Works great for some games, but can COMPLETELY BREAK other games - YMMV!\n\n"
            "If you are using -forceresswap/-sdvxlandscape, put the TARGET monitor resolution in this field.\n\n"
            "This should only be used as last resort if your GPU/monitor can't display the resolution required by the game.",
        .type = OptionType::Text,
        .setting_name = "1280,720",
        .category = "Graphics (Full Screen)"
    },
    {
        // FullscreenOrientationFlip
        .title = "Full Screen Orientation Swap (EXPERIMENTAL)",
        .name = "forceresswap",
        .desc =
            "Allows you to play portrait games in in landscape (and vice versa) by transposing resolution and applying image scaling.\n\n"
            "Works great for some games, but can COMPLETELY BREAK other games - YMMV!\n\n"
            "Strongly consider combining this with -forceres option to render at monitor native resolution\n\n"
            "WARNING: for SDVX, this messes with camera angle for note lanes, causing it to be zoomed out, and causes performance issues "
            "with Live2D!",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Graphics (Full Screen)"
    },
    {
        // FullscreenSubRefreshRate
        .title = "Force Submonitor Refresh Rate (EXPERIMENTAL)",
        .name = "graphics-force-refresh-sub",
        .desc =
            "Override fullscreen refresh rate requested by the game for second monitor, "
            "useful if you have a sub monitor that is not quite exactly 60Hz.\n\n"
            "WARNING: experimental as we have not done extensive testing to see if this causes desyncs.",
        .type = OptionType::Integer,
        .category = "Graphics (Full Screen)"
    },
    {
        // Graphics9On12
        .title = "DirectX 9 on 12 (DEPRECATED - use -dx9on12 instead)",
        .name = "9on12",
        .desc = "Use D3D9On12 wrapper library, requires Windows 10. Deprecated - use -dx9on12 instead.",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Graphics (Common)",
    },
    {
        // spice2x_Dx9On12
        .title = "DirectX 9 on 12",
        .name = "sp2x-dx9on12",
        .display_name = "dx9on12",
        .aliases= "dx9on12",
        .desc = "Use D3D9On12 wrapper library, requires Windows 10. Has no effect games on that don't use DX9. Can cause some games to crash.\n\n"
            "Default: auto (use DX9 for most games, but turn on 9on12 for games that require it on non-NVIDIA GPUs).",
        .type = OptionType::Enum,
        .category = "Graphics (Common)",
        .elements = {
            {"auto", "Automatic"},
            {"0", "Use DX9"},
            {"1", "Use DX9on12"},
        },
    },
    {
        .title = "Disable Win/Media/Special Keys",
        .name = "nolegacy",
        .desc = "Disables legacy key activation in-game.",
        .type = OptionType::Bool,
        .category = "I/O Options",
    },
    {
        .title = "Discord Rich Presence",
        .name = "richpresence",
        .desc = "Enables Discord Rich Presence support.",
        .type = OptionType::Bool,
        .category = "Miscellaneous",
    },
    {
        .title = "Smart Local EA",
        .name = "smartea",
        .desc = "Automatically enables -ea when server is offline.",
        .type = OptionType::Bool,
        .category = "Network (Advanced)",
    },
    {
        // EAmusementMaintenance
        .title = "EA Maintenance (DEPRECATED - use -forceeamaint instead)",
        .name = "eamaint",
        .desc = "Enables EA Maintenance, 1 for on, 0 for off.",
        .type = OptionType::Enum,
        .hidden = true,
        .category = "Network (Advanced)",
        .elements = {{"0", "Off"}, {"1", "On"}},
    },
    {
        // spice2x_EAmusementMaintenance
        .title = "Local EA Maintenance",
        .name = "sp2x-eamaint",
        .display_name = "forceeamaint",
        .aliases= "forceeamaint",
        .desc = "Causes local EA to start in maintenance mode. Must be used with -ea or -smartea.",
        .type = OptionType::Bool,
        .category = "Network (Advanced)",
    },
    {
        .title = "Preferred NetAdapter IP",
        .name = "network",
        .desc = "This is NOT the EA service URL; use -url for that. "
            "Force the use of an adapter with the specified network. Must also provide -subnet.",
        .type = OptionType::Text,
        .category = "Network (Development)",
        .sensitive = true,
    },
    {
        .title = "Preferred NetAdapter Subnet",
        .name = "subnet",
        .desc = "Force the use of an adapter with the specified subnet. Must also provide -network.",
        .type = OptionType::Text,
        .category = "Network (Development)",
    },
    {
        .title = "Disable Network Fixes",
        .name = "netfixdisable",
        .desc = "Force disables network fixes.",
        .type = OptionType::Bool,
        .category = "Network (Development)",
    },
    {
        .title = "HTTP/1.1",
        .name = "http11",
        .desc = "Sets EA3 http11 value.",
        .type = OptionType::Enum,
        .category = "Network (Development)",
        .elements = {{"0", "Off"}, {"1", "On"}},
    },
    {
        .title = "Disable SSL Protocol",
        .name = "ssldisable",
        .desc = "Prevents the SSL protocol from being registered.",
        .type = OptionType::Bool,
        .category = "Network (Development)",
    },
    {
        .title = "URL Slash",
        .name = "urlslash",
        .desc = "Sets EA3 urlslash value.",
        .type = OptionType::Enum,
        .category = "Network (Advanced)",
        .elements = {{"0", "Off"}, {"1", "On"}},
    },
    {
        .title = "SOFTID",
        .name = "r",
        .desc = "Set custom SOFTID override.",
        .type = OptionType::Text,
        .category = "Development",
        .sensitive = true,
    },
    {
        .title = "(REMOVED) VR controls (experimental)",
        .name = "vr",
        .desc = "This feature has been removed.",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "DANCERUSH",
        .category = "Game Options",
        .disabled = true,
    },
    {
        // DisableOverlay
        .title = "Disable Spice Overlay",
        .name = "overlaydisable",
        .desc = "Disables the in-game overlay.",
        .type = OptionType::Bool,
        .category = "Overlay",
    },
    {
        // OverlayScaling
        .title = "Spice Overlay UI Scale %",
        .name = "overlayscale",
        .desc = "Forces UI scaling for the overlay, "
            "things can look off since the UI was written for 100% scaling. "
            "Enter value in percentage, between 10-400, inclusive.",
        .type = OptionType::Integer,
        .setting_name = "200",
        .category = "Overlay",
    },
    {
        // spice2x_FpsAutoShow
        .title = "Auto Show FPS/Clock",
        .name = "sp2x-autofps",
        .display_name = "autofps",
        .aliases= "autofps",
        .desc = "Automatically show FPS / clock / timer overlay window when the game starts.",
        .type = OptionType::Bool,
        .category = "Overlay",
    },
    {
        // spice2x_FpsOpposite
        .title = "Show FPS/Clock top-left",
        .name = "fpsflip",
        .desc = "Show FPS / clock / timer overlay on the top left of the screen instead of the top right.",
        .type = OptionType::Bool,
        .category = "Overlay",
    },
    {
        // spice2x_SubScreenAutoShow
        .title = "Auto Show Subscreen",
        .name = "sp2x-autosubscreen",
        .display_name = "autosubscreen",
        .aliases= "autosubscreen",
        .desc = "Automatically show the subscreen overlay when the game starts, if the game has one.",
        .type = OptionType::Bool,
        .category = "Overlay",
    },
    {
        // spice2x_IOPanelAutoShow
        .title = "Auto Show IO Panel",
        .name = "sp2x-autoiopanel",
        .display_name = "autoiopanel",
        .aliases= "autoiopanel",
        .desc = "Automatically show I/O panel window when the game starts.",
        .type = OptionType::Bool,
        .category = "Overlay",
    },
    {
        // spice2x_KeypadAutoShow
        .title = "Auto Show Keypad",
        .name = "sp2x-autokeypad",
        .display_name = "autokeypad",
        .aliases= "autokeypad",
        .desc = "Automatically show virtual keypad window when the game starts.",
        .type = OptionType::Enum,
        .category = "Overlay",
        .elements = {
            {"0", "Off"},
            {"1", "P1"},
            {"2", "P2"},
            {"3", "P1 and P2"},
        },
    },
    {
        .title = "Force Load IIDX Module",
        .name = "iidx",
        .desc = "Manually enable Beatmania IIDX module.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "IIDX Camera Order Flip",
        .name = "iidxflipcams",
        .desc = "Flip the camera order.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // IIDXDisableCameras
        .title = "IIDX Disable Cameras (DEPRECATED - no longer needed)",
        .name = "iidxdisablecams",
        .desc = "Disables cameras.",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "Beatmania IIDX",
        .category = "Game Options",
    },
    {
        // IIDXCabCamAccess
        .title = "IIDX Use Official AC Cams",
        .name = "iidxcabcams",
        .desc = "For IIDX25+, allow direct access to cameras from real arcade cabinets. "
            "Only turn this on if you have OFFICIAL arcade cameras connected to the correct USB ports. Default: auto.",
        .type = OptionType::Enum,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
        .elements = {
            {"auto", ""},
            {"off", ""},
            {"on", ""}
        },
    },
    {
        // IIDXCamHook
        .title = "IIDX Cam Hook",
        .name = "iidxtdjcamhook",
        .display_name = "iidxcamhook",
        .aliases = "iidxcamhook",
        .desc = "Experimental camera support for IIDX 27+ via texture hooking. Requires camera(s) that support YUY2 or NV12 encoding.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // IIDXCamHookRatio
        .title = "IIDX Cam Hook Aspect Ratio",
        .name = "iidxtdjcamhookratio",
        .display_name = "iidxcamhookratio",
        .aliases = "iidxcamhookratio",
        .desc = "Determine what aspect ratio should be preferred for webcam input. Default: 16:9.",
        .type = OptionType::Enum,
        .hidden = true,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
        .elements = {
            {"43", "4:3"},
            {"169", "16:9"},
        },
    },
    {
        // IIDXCamHookOverride
        .title = "IIDX Cam Hook Offset Override",
        .name = "iidxtdjcamhookoffset",
        .display_name = "iidxcamhookoffset",
        .aliases = "iidxcamhookoffset",
        .desc = "Override DLL offsets for camera hook; "
            "format: 5 hex values separated by commas "
            "(offset_hook_a, offset_textures, offset_camera_manager, offset_d3d_device, offset_afp_texture).",
        .type = OptionType::Text,
        .setting_name = "0x5817a0,0x6fffbd8,0xbbae40,0xe0,0x30",
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // IIDXCamHookTopId
        .title = "IIDX Cam Hook Top ID",
        .name = "iidxtdjcamhooktop",
        .display_name = "iidxcamhooktop",
        .aliases = "iidxcamhooktop",
        .desc =
            "Specify a partial ID of the camera to use as the TOP camera. Useful if you have more than 3 cameras and need to tell spice which ones to use.\n\n"
            "To get IDs, launch the game and look for camera IDs from MFEnumDeviceSources.\n\n"
            "For example, `vid_1234&pid_5678` matches on `\\\\?\\usb#vid_1234&pid_5678&mi_00#7&234123456&1&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global`.",
        .type = OptionType::Text,
        .setting_name = "vid_1234&pid_5678",
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // IIDXCamHookFrontId
        .title = "IIDX Cam Hook Front ID",
        .name = "iidxtdjcamhookfront",
        .display_name = "iidxcamhookfront",
        .aliases = "iidxcamhookfront",
        .desc =
            "Specify a partial ID of the camera to use as the FRONT camera. Useful if you have more than 3 cameras and need to tell spice which ones to use.\n\n"
            "To get IDs, launch the game and look for camera IDs from MFEnumDeviceSources.\n\n"
            "For example, `vid_1234&pid_5678` matches on `\\\\?\\usb#vid_1234&pid_5678&mi_00#7&234123456&1&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global`.",
        .type = OptionType::Text,
        .setting_name = "vid_90ab&pid_cdef",
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "IIDX Sound Output Device",
        .name = "iidxsounddevice",
        .desc =
            "SOUND_OUTPUT_DEVICE environment variable override. Default: auto. "
            "Auto will use WASAPI, unless -iidxasio is set or if XONAR SOUND CARD is present, then ASIO is used.",
        .type = OptionType::Enum,
        .game_name = "Beatmania IIDX",
        .category = "Game Options",
        .elements = {
            {"auto", "Automatic"},
            {"wasapi", "Windows Audio"},
            {"asio", "ASIO"},
        },
    },
    {
        .title = "IIDX ASIO Driver",
        .name = "iidxasio",
        .desc = "ASIO driver name to use, replacing XONAR SOUND CARD(64). "
            "String should match registry key under HKLM\\SOFTWARE\\ASIO\\ \n\n"
            "IIDX is very picky about ASIO devices it can support, YMMV.",
        .type = OptionType::Text,
        .game_name = "Beatmania IIDX",
        .category = "Game Options",
    },
    {
        .title = "IIDX BIO2 Firmware Update",
        .name =  "iidxbio2fw",
        .desc = "Enables BIO2 firmware updates. WARNING - can cause semi-permanent change to your I/O board.",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "IIDX TDJ Mode (Lightning Model)",
        .name =  "iidxtdj",
        .desc = "Enables TDJ mode (Lightning Model cabinet).",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options",
    },
    {
        // spice2x_IIDXDigitalTTSensitivity
        .title = "IIDX Digital TT Sensitivity",
        .name = "sp2x-iidxdigitalttsens",
        .display_name = "iidxdigitalttsens",
        .aliases= "iidxdigitalttsens",
        .desc = "Adjust sensitivity of turntable when digital input (buttons) is used. Does not affect analog input! Default: 4.",
        .type = OptionType::Integer,
        .setting_name = "(0-255)",
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // IIDXDigitalTTSocd
        .title = "IIDX Digital TT SOCD Cleaner",
        .name = "iidxsocd",
        .desc = "SOCD for turntables when using button input; what happens when both directions are pressed.\n\n"
            "last: most recently pressed direction takes priority\n\n"
            "first: first pressed direction takes priority\n\n"
            "neutral (default): TT does not move when both directions are pressed, recommended to avoid excessive POORs.",
        .type = OptionType::Enum,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
        .elements = {
            {"last", ""},
            {"first", ""},
            {"neutral", ""},
        },
    },
    {
        // spice2x_IIDXLDJForce720p
        .title = "IIDX LDJ Force 720p (HD)",
        .name = "sp2x-iidxldjforce720p",
        .display_name = "iidxldjforce720p",
        .aliases= "iidxldjforce720p",
        .desc = "Force Definition Type HD (720p) mode instead of FHD (1080p) for IIDX30+. Only for LDJ! TDJ is always FHD in IIDX30+.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // spice2x_IIDXTDJSubSize
        .title = "IIDX TDJ Subscreen Size",
        .name = "sp2x-iidxtdjsubsize",
        .display_name = "iidxtdjsubsize",
        .aliases= "iidxtdjsubsize",
        .desc = "Default size of the subscreen overlay.\n\nNote: in windowed mode, subscreen will always be full size.\n\nDefault: medium.",
        .type = OptionType::Enum,
        .game_name = "Beatmania IIDX",
        .category = "Overlay",
        .elements = {
            {"small", ""},
            {"medium", ""},
            {"large", ""},
            {"fullscreen", ""},
        },
    },
    {
        // spice2x_IIDXLEDFontSize
        .title = "IIDX LED Ticker Font Size",
        .name = "sp2x-iidxledfontsize",
        .display_name = "iidxledfontsize",
        .aliases= "iidxledfontsize",
        .desc = "Configure the font size of the segment display, in pixels. Default: 64 (px).",
        .type = OptionType::Integer,
        .game_name = "Beatmania IIDX",
        .category = "Overlay",
    },
    {
        // spice2x_IIDXLEDColor
        .title = "IIDX LED Ticker Color",
        .name = "sp2x-iidxledcolor",
        .display_name = "iidxledcolor",
        .aliases= "iidxledcolor",
        .desc = "Configure the font color of the segment display; specify RGB value in hex. "
                "Default: 0xff0000 (red).",
        .type = OptionType::Hex,
        .game_name = "Beatmania IIDX",
        .category = "Overlay",
    },
    {
        // spice2x_IIDXLEDPos
        .title = "IIDX LED Ticker Position",
        .name = "sp2x-iidxledpos",
        .display_name = "iidxledpos",
        .aliases= "iidxledpos",
        .desc = "Initial position of the segment display. Default: bottom.",
        .type = OptionType::Enum,
        .game_name = "Beatmania IIDX",
        .category = "Overlay",
        .elements = {
            {"topleft", ""},
            {"top", ""},
            {"topright", ""},
            {"bottomleft", ""},
            {"bottom", ""},
            {"bottomright", ""},
        },
    },
    {
        // IIDXLEDBorderless
        .title = "IIDX LED Ticker Borderless",
        .name = "iidxledborderless",
        .aliases= "iidxledborderless",
        .desc = "Make LED ticker overlay window borderless (remove window decoration).",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Overlay",
    },
    {
        .title = "Force Load Sound Voltex Module",
        .name = "sdvx",
        .desc = "Manually enable Sound Voltex Module.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "SDVX Force 720p",
        .name = "sdvx720",
        .desc = "Force Sound Voltex 720p display mode, used by older games. "
            "For newer games, use window resize function instead.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "SDVX Printer Emulation",
        .name = "printer",
        .desc = "Enable Sound Voltex printer emulation.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        // SDVXPrinterOutputPath
        .title = "SDVX Printer Output Path",
        .name = "printerpath",
        .desc = "Path to folder where images will be stored.",
        .type = OptionType::Text,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "SDVX Printer Output Clear",
        .name = "printerclear",
        .desc = "Clean up saved images in the output directory on startup.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "SDVX Printer Output Overwrite",
        .name = "printeroverwrite",
        .desc = "Always overwrite the same file in output directory.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        // SDVXPrinterOutputFormat
        .title = "SDVX Printer Output Format",
        .name = "printerformat",
        .desc = "File format for printer output.",
        .type = OptionType::Text,
        .setting_name = "(png/bmp/tga/jpg)",
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "SDVX Printer JPG Quality",
        .name = "printerjpgquality",
        .desc = "Quality setting in percent if JPG format is used.",
        .type = OptionType::Integer,
        .setting_name = "(0-100)",
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "SDVX Disable Cameras (DEPRECATED - no longer needed)",
        .name = "sdvxdisablecams",
        .desc = "This option does nothing.\n\n"
            "This option was used in the past to fix cameras in SDVX5 but this is no longer "
            "needed as camera check will always be skipped with a built-in patch.",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "Sound Voltex",
        .category = "Game Options",
    },
    {
        .title = "SDVX Native Touch Handling",
        .name = "sdvxnativetouch",
        .desc = "Disables touch hooks and lets the game access a touch screen directly. "
                "Requires a touch screen to be connected as a secondary monitor. "
                "Touch input must be routed to the primary screen via Windows Tablet PC settings. "
                "Enable this when you get duplicate touch inputs from an actual touch screen.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        // spice2x_SDVXDigitalKnobSensitivity
        .title = "SDVX Digital Knob Sensitivity",
        .name = "sp2x-sdvxdigitalknobsens",
        .display_name = "sdvxdigitalknobsens",
        .aliases= "sdvxdigitalknobsens",
        .desc = "Adjust sensitivity of knobs when digital input (buttons) is used. Does not affect analog input! Default: 16.",
        .type = OptionType::Integer,
        .setting_name = "(0-255)",
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        // SDVXDigitalKnobSocd
        .title = "SDVX Digital Knob SOCD Cleaner",
        .name = "sdvxsocd",
        .desc = "SOCD for knobs when using button input; what happens when both directions are pressed.\n\n"
            "last (default): most recently pressed direction takes priority, recommended to deal with slams\n\n"
            "first: first pressed direction takes priority\n\n"
            "neutral: knob does not move when both directions are pressed.",
        .type = OptionType::Enum,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
        .elements = {
            {"last", ""},
            {"first", ""},
            {"neutral", ""},
        },
    },
    {
        // spice2x_SDVXAsioDriver
        .title = "SDVX ASIO driver",
        .name = "sp2x-sdvxasio",
        .display_name = "sdvxasio",
        .aliases= "sdvxasio",
        .desc = "ASIO driver name to use, replacing XONAR SOUND CARD(64). "
            "String should match registry key under HLKM\\SOFTWARE\\ASIO\\ \n\n"
            "SDVX is EXTREMELY picky about ASIO devices it can support!",
        .type = OptionType::Text,
        .game_name = "Sound Voltex",
        .category = "Game Options",
    },
    {
        // spice2x_SDVXSubPos
        .title = "SDVX Subscreen Overlay Position",
        .name = "sp2x-sdvxsubpos",
        .display_name = "sdvxsubpos",
        .aliases= "sdvxsubpos",
        .desc = "Location for the subscreen overlay. Default: bottom.",
        .type = OptionType::Enum,
        .game_name = "Sound Voltex",
        .category = "Overlay",
        .elements = {
            {"top", ""},
            {"center", ""},
            {"bottom", ""},
            {"bottomleft", "for landscape"},
            {"bottomright", "for landscape"},
        },
    },
    {
        // spice2x_SDVXSubRedraw
        .title = "SDVX Subscreen Force Redraw",
        .name = "sp2x-sdvxsubredraw",
        .display_name = "sdvxsubredraw",
        .aliases= "sdvxsubredraw",
        .desc = "Check if second monitor for subscreen doesn't update every frame; "
            "forces subscreen to redraw every frame, only needed for newer EG.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load DDR Module",
        .name = "ddr",
        .desc = "Manually enable Dance Dance Revolution module.",
        .type = OptionType::Bool,
        .game_name = "Dance Dance Revolution",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "DDR 4:3 Mode",
        .name = "ddrsd/o",
        .desc = "Enable DDR 4:3 (SD) mode.",
        .type = OptionType::Bool,
        .game_name = "Dance Dance Revolution",
        .category = "Game Options",
    },
    {
        // DDRSkipCodecRegisteration
        .title = "DDR Skip Codec Registration",
        .name = "ddrnocodec",
        .desc = "Prevent automatic registration of codecs in the com folder.",
        .type = OptionType::Bool,
        .game_name = "Dance Dance Revolution",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Pop'n Music Module",
        .name = "pnm",
        .desc = "Manually enable Pop'n Music module.",
        .type = OptionType::Bool,
        .game_name = "Pop'n Music",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Pop'n Music Force HD Mode",
        .name = "pnmhd",
        .desc = "Force enable Pop'n Music HD mode.",
        .type = OptionType::Bool,
        .game_name = "Pop'n Music",
        .category = "Game Options",
    },
    {
        .title = "Pop'n Music Force SD Mode",
        .name = "pnmsd",
        .desc = "Force enable Pop'n Music SD mode.",
        .type = OptionType::Bool,
        .game_name = "Pop'n Music",
        .category = "Game Options",
    },
    {
        .title = "Force Load HELLO! Pop'n Music Module",
        .name = "hpm",
        .desc = "Manually enable HELLO! Pop'n Music module.",
        .type = OptionType::Bool,
        .game_name = "HELLO! Pop'n Music",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load GitaDora Module",
        .name = "gd",
        .desc = "Manually enable GitaDora module.",
        .type = OptionType::Bool,
        .game_name = "GitaDora",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "GitaDora Two Channel Audio",
        .name = "2ch",
        .desc = "Attempt to reduce audio channels down to just two channels.",
        .type = OptionType::Bool,
        .game_name = "GitaDora",
        .category = "Game Options",
    },
    {
        .title = "GitaDora Cabinet Type",
        .name = "gdcabtype",
        .desc = "Select cabinet type. DX has more input and lights. Pick SD2 for single player guitar mode on white cab.",
        .type = OptionType::Enum,
        .game_name = "GitaDora",
        .category = "Game Options",
        .elements = {{"1", "DX"}, {"2", "SD"}, {"3", "SD2 - white cab"}},
    },
    {
        // GitaDoraArenaSingleWindow
        .title = "GitaDora Arena Single Window (EXPERIMENTAL)",
        .name = "gdaonewindow",
        .desc = "For Arena Model, when combined with windowed mode, only show the main game window; enables subscreen overlay.",
        .type = OptionType::Bool,
        .game_name = "GitaDora",
        .category = "Game Options",
    },
    {
        // GitaDoraLefty
        .title = "GitaDora Lefty Guitar (for Digital Wailing)",
        .name = "gdlefty",
        .desc = "Enables lefty mode, flipping motion sensor directions. Default: off.\n\n"
            "Without this option, enabling LEFT in the game option will continuously trigger UP wail.\n\n"
            "Has no effect if you are using analog bindings for X/Y axis; expectation is that your controller "
            "handles this correctly (most do not, however).\n\n"
            "As always, remember to restart the game after changing options. If you need to change in the game, "
            "use the I/O panel overlay window.",
        .type = OptionType::Enum,
        .game_name = "GitaDora",
        .category = "Game Options",
        .elements = {
            {"off", "both righty"},
            {"p1", "p1 is lefty"},
            {"p2", "p2 is lefty"},
            {"both", "both lefty"},
        },
    },
    {
        // GitaDoraWailHold
        .title = "GitaDora Digital Wail Hold",
        .name = "gdwailhold",
        .desc = "For digital wail input, how long (in milliseconds) to hold the wail state. Practically, this controls "
            "how sensitive wailing is.\n\n"
            "Default: 100 (ms)\n\n"
            "If set to 0, no hold will occur, which means you need to press the wail button longer to register; "
            "default value of 100ms will cause the game to trigger wail even on a light tap of the button.",
        .type = OptionType::Integer,
        .game_name = "GitaDora",
        .category = "Game Options",
    },
    {
        // GitaDoraPickAlgo
        .title = "GitaDora Picking Algorithm",
        .name = "gdpickalgo",
        .desc = "Select picking algorithm for guitar.\n\n"
            "recent (default): use SOCD cleaner algorithm that prioritizes recent input\n\n"
            "legacy: only the rising edge of the pick input is considered; cannot hold pick for menu navigation\n\n"
            "neutral: if both up and down are pressed, they cancel out\n\n"
            "raw: no filtering done by spice; game receives both input as-is, possible to input both up and down at the same time",
        .type = OptionType::Enum,
        .game_name = "GitaDora",
        .category = "Game Options",
        .elements = {
            {"recent", ""},
            {"legacy", ""},
            {"neutral", ""},
            {"raw", ""}
        },
    },
    {
        // GitaDoraSubOverlaySize
        .title = "GitaDora Subscreen Overlay Size",
        .name = "gdsubsize",
        .desc = "Default size of the subscreen overlay. Default: medium.",
        .type = OptionType::Enum,
        .game_name = "GitaDora",
        .category = "Overlay",
        .elements = {
            {"small", ""},
            {"medium", ""},
            {"large", ""}
        },
    },
    {
        .title = "Force Load Jubeat Module",
        .name = "jb",
        .desc = "Manually enable Jubeat module.",
        .type = OptionType::Bool,
        .game_name = "Jubeat",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Reflec Beat Module",
        .name = "rb",
        .desc = "Manually enable Reflec Beat module.",
        .type = OptionType::Bool,
        .game_name = "Reflec Beat",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Tenkaichi Shogikai Module",
        .name = "shogikai",
        .desc = "Manually enable Tenkaichi Shogikai module.",
        .type = OptionType::Bool,
        .game_name = "Tenkaichi Shogikai",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Beatstream Module",
        .name = "bs",
        .desc = "Manually enable Beatstream module.",
        .type = OptionType::Bool,
        .game_name = "Beatstream",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Nostalgia Module",
        .name = "nostalgia",
        .desc = "Manually enable Nostalgia module.",
        .type = OptionType::Bool,
        .game_name = "Nostalgia",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Dance Evolution Module",
        .name = "dea",
        .desc = "Manually enable Dance Evolution module.",
        .type = OptionType::Bool,
        .game_name = "Dance Evolution",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load FutureTomTom Module",
        .name = "ftt",
        .desc = "Manually enable FutureTomTom module.",
        .type = OptionType::Bool,
        .game_name = "FutureTomTom",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load BBC Module",
        .name = "bbc",
        .desc = "Manually enable Bishi Bashi Channel module.",
        .type = OptionType::Bool,
        .game_name = "Bishi Bashi Channel",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Metal Gear Arcade Module",
        .name = "mga",
        .desc = "Manually enable Metal Gear Arcade module.",
        .type = OptionType::Bool,
        .game_name = "Metal Gear",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Quiz Magic Academy Module",
        .name = "qma",
        .desc = "Manually enable Quiz Magic Academy module.",
        .type = OptionType::Bool,
        .game_name = "Quiz Magic Academy",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Road Fighters 3D Module",
        .name = "rf3d",
        .desc = "Manually enable Road Fighters 3D module.",
        .type = OptionType::Bool,
        .game_name = "Road Fighters 3D",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Steel Chronicle Module",
        .name = "sc",
        .desc = "Manually enable Steel Chronicle module.",
        .type = OptionType::Bool,
        .game_name = "Steel Chronicle",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Mahjong Fight Club Module",
        .name = "mfc",
        .desc = "Manually enable Mahjong Fight Club module.",
        .type = OptionType::Bool,
        .game_name = "Mahjong Fight Club",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Scotto Module",
        .name = "scotto",
        .desc = "Manually enable Scotto module.",
        .type = OptionType::Bool,
        .game_name = "Scotto",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Dance Rush Module",
        .name = "dr",
        .desc = "Manually enable Dance Rush module.",
        .type = OptionType::Bool,
        .game_name = "DANCERUSH",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Winning Eleven Module",
        .name = "we",
        .desc = "Manually enable Winning Eleven module.",
        .type = OptionType::Bool,
        .game_name = "Winning Eleven",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Otoca D'or Module",
        .name = "otoca",
        .desc = "Manually enable Otoca D'or module.",
        .type = OptionType::Bool,
        .game_name = "Otoca D'or",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load LovePlus Module",
        .name = "loveplus",
        .desc = "manually enable LovePlus module.",
        .type = OptionType::Bool,
        .game_name = "LovePlus",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Charge Machine Module",
        .name = "pcm",
        .desc = "manually enable Charge Machine module.",
        .type = OptionType::Bool,
        .game_name = "Charge Machine",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Ongaku Paradise Module",
        .name = "onpara",
        .desc = "manually enable Ongaku Paradise module.",
        .type = OptionType::Bool,
        .game_name = "Ongaku Paradise",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Force Load Busou Shinki Module",
        .name = "busou",
        .desc = "manually enable Busou Shinki module.",
        .type = OptionType::Bool,
        .game_name = "Busou Shinki: Armored Princess Battle Conductor",
        .category = "Game Options (Advanced)",
    },
    {
        // LoadCCJModule
        .title = "Force Load Chase Chase Jokers Module",
        .name = "ccj",
        .desc = "manually enable Chase Chase Jokers module.",
        .type = OptionType::Bool,
        .game_name = "Chase Chase Jokers",
        .category = "Game Options (Advanced)",
    },
    {
        // LoadQKSModule
        .title = "Force Load QuizKnock STADIUM Module",
        .name = "qks",
        .desc = "manually enable QuizKnock STADIUM module",
        .type = OptionType::Bool,
        .game_name = "QuizKnock STADIUM",
        .category = "Game Options (Advanced)",
    },
    {
        // LoadMFGModule
        .title = "Force Load Mahjong Fight Girl Module",
        .name = "mfg",
        .desc = "manually enable Mahjong Fight Girl module.",
        .type = OptionType::Bool,
        .game_name = "Mahjong Fight Girl",
        .category = "Game Options (Advanced)",
    },
    {
        // LoadPCModule
        .title = "Force Load Polaris Chord Module",
        .name = "pc",
        .desc = "manually enable Polaris Chord module.",
        .type = OptionType::Bool,
        .game_name = "Polaris Chord",
        .category = "Game Options (Advanced)",
    },
    {
        // LoadMusecaModule
        .title = "Force Load Museca Module",
        .name = "museca",
        .desc = "manually enable Museca module.",
        .type = OptionType::Bool,
        .game_name = "Museca",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "Modules Folder Override",
        .name = "modules",
        .desc = "Sets a custom path to the modules folder.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "Screenshot Folder Override",
        .name = "screenshotpath",
        .desc = "Sets a custom path to the screenshots folder.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "Configuration Path Override",
        .name = "cfgpath",
        .desc = "Sets a custom file path for config file. Must be passed via the command line. "
            "If left empty, %appdata%\\spicetools.xml will be used.",
        .type = OptionType::Text,
        .setting_name = "(default)",
        .category = "Path Overrides",
        .disabled = true,
    },
    {
        // ScreenResizeConfigPath
        .title = "Screen Resize Config Path Override",
        .name = "resizecfgpath",
        .desc = "Sets a custom file path for screen resize config file. "
            "If left empty, %appdata%\\spice2x\\spicetools_screen_resize.json will be used.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        // PatchManagerConfigPath
        .title = "Patch Manager Config Path Override",
        .name = "patchcfgpath",
        .desc = "Sets a custom file path for patch manager config file. Can be used to manage 'profiles' for auto-patches. "
            "If left empty, %appdata%\\spice2x\\spicetools_patch_manager.json will be used.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "Intel SDE",
        .name = "sde",
        .desc = "Path to Intel SDE kit path for automatic attaching.",
        .type = OptionType::Text,
        .category = "Development",
    },
    {
        .title = "ea3-config.xml Override",
        .name = "e",
        .desc = "Sets a custom path to ea3-config.xml.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "app-config.xml Override",
        .name = "a",
        .desc = "Sets a custom path to app-config.xml.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "avs-config.xml Override",
        .name = "v",
        .desc = "Sets a custom path to avs-config.xml.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "bootstrap.xml Override",
        .name = "b",
        .desc = "Sets a custom path to bootstrap.xml.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "log.txt Override",
        .name = "y",
        .desc = "Sets a custom path to log.txt.",
        .type = OptionType::Text,
        .category = "Path Overrides",
    },
    {
        .title = "API TCP Port",
        .name = "api",
        .desc = "Port the API should be listening on.",
        .type = OptionType::Integer,
        .category = "SpiceCompanion and API",
    },
    {
        .title = "API Password",
        .name = "apipass",
        .desc = "Set the custom user password needed to use the API.",
        .type = OptionType::Text,
        .category = "SpiceCompanion and API",
        .sensitive = true,
    },
    {
        .title = "API Verbose Logging",
        .name = "apilogging",
        .desc = "verbose logging of API activity.",
        .type = OptionType::Bool,
        .category = "API (Dev)",
    },
    {
        .title = "API Serial Port",
        .name = "apiserial",
        .desc = "Serial port the API should be listening on.",
        .type = OptionType::Text,
        .category = "API (Serial)",
    },
    {
        .title = "API Serial Baud",
        .name = "apiserialbaud",
        .desc = "Baud rate for the serial port.",
        .type = OptionType::Integer,
        .category = "API (Serial)",
    },
    {
        .title = "API Pretty",
        .name = "apipretty",
        .desc = "Slower, but pretty API output.",
        .type = OptionType::Bool,
        .category = "API (Dev)",
    },
    {
        .title = "API Debug Mode",
        .name = "apidebug",
        .desc = "Enables API debugging mode.",
        .type = OptionType::Bool,
        .category = "API (Dev)",
    },
    {
        // APIScreenMirrorQuality
        .title = "API Screen Mirror Quality Override",
        .name = "apiscreenq",
        .desc = "JPEG compression level of mirrored images, overriding any client request. "
            "Value must be between 0 (poor quality) and 100 (best quality), inclusive. Default: 70.",
        .type = OptionType::Integer,
        .setting_name = "(0-100)",
        .category = "SpiceCompanion and API",
    },
    {
        // APIScreenMirrorDivide
        .title = "API Screen Mirror Divide Override",
        .name = "apiscreendiv",
        .desc = "Divide value of mirrored images, overriding any client request. "
            "Divide of 1 means send every pixel, 2 means every other pixel, 3 means every third pixel, and so on; "
            "increasing the value drastically reduces transfer size at the cost of image quality. "
            "Value must be 1 or greater. Default: 1.",
        .type = OptionType::Integer,
        .setting_name = "1",
        .category = "SpiceCompanion and API",
    },
    {
        .title = "Enable All IO Modules",
        .name = "io",
        .desc = "Manually enable ALL IO emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable ACIO Module",
        .name = "acio",
        .desc = "Manually enable ACIO emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable ICCA Module",
        .name = "icca",
        .desc = "Manually enable ICCA emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable DEVICE Module",
        .name = "device",
        .desc = "Manually enable DEVICE emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable EXTDEV Module",
        .name = "extdev",
        .desc = "Manually enable EXTDEV emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable AMI2000 Module",
        .name = "ami2000",
        .desc = "Manually enable AMI2000 emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable SCIUNIT Module",
        .name = "sciunit",
        .desc = "Manually enable SCIUNIT emulation.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Enable device passthrough",
        .name = "devicehookdisable",
        .desc = "Disable I/O and serial device hooks.",
        .type = OptionType::Bool,
        .category = "I/O Modules",
    },
    {
        .title = "Disable Raw Input Touch",
        .name = "wintouch",
        .desc = "For touch screen input, disable usage of Raw Input API and instead use "
                "Win8 Pointer API or Win7 Touch API. Only enable this if you have trouble "
                "using the default (raw input) touch input, as Raw Input performs better.",
        .type = OptionType::Bool,
        .category = "Touch Parameters",
    },
    {
        .title = "Force Touch Emulation",
        .name = "touchemuforce",
        .desc = "Force enable hook for GetTouchInputInfo API and inject WM_TOUCH events. "
                "This is automatically turned on when needed, so it is not typically required to "
                "turn it on manually. Do not enable this unless you have trouble.",
        .type = OptionType::Bool,
        .category = "Touch Parameters",
    },
    {
        .title = "Invert Raw Input Touch",
        .name = "touchinvert",
        .desc = "Inverts raw touch coordinates; only works for default raw input handler, "
                "and not Windows Touch API.",
        .type = OptionType::Bool,
        .category = "Touch Parameters",
    },
    {
        // DisableTouchCardInsert
        .title = "Disable Touch Card Insert (DEPRECATED - use -touchcard instead)",
        .name = "touchnocard",
        .desc = "Disables touch overlay card insert button.",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Touch Parameters",
    },
    {
        // spice2x_TouchCardInsert
        .title = "Show Insert Card button",
        .name = "sp2x-touchcard",
        .display_name = "touchcard",
        .aliases= "touchcard",
        .desc = "Show Insert Card touch button on main display.",
        .type = OptionType::Bool,
        .category = "Touch Parameters",
    },
    {
        // ICCAReaderPort
        .title = "ICCA Reader Port",
        .name = "reader",
        .desc = "Connects to and uses a ICCA on a given COM port.",
        .type = OptionType::Text,
        .category = "NFC Card Readers",
    },
    {
        .title = "ICCA Reader Port (with toggle)",
        .name = "togglereader",
        .desc = "Connects to and uses a ICCA on a given COM port, and enabled NumLock toggling between P1/P2.",
        .type = OptionType::Text,
        .category = "NFC Card Readers",
    },
    {
        .title = "CardIO HID Reader Support",
        .name = "cardio",
        .desc = "Enables detection and support of cardio HID readers.",
        .type = OptionType::Bool,
        .category = "NFC Card Readers",
    },
    {
        .title = "CardIO HID Reader Order Flip",
        .name = "cardioflip",
        .desc = "Flips the order of detection for P1/P2.",
        .type = OptionType::Bool,
        .category = "NFC Card Readers",
    },
    {
        // CardIOHIDReaderOrderToggle
        .title = "CardIO HID Reader Order Toggle",
        .name = "cardiotoggle",
        .desc = "Toggles reader between P1/P2 using the NumLock key state.",
        .type = OptionType::Bool,
        .category = "NFC Card Readers",
    },
    {
        .title = "HID SmartCard (ACR122U)",
        .name = "scard",
        .desc = "Detects and uses HID smart card readers for card input. Developed for ACR122U reader.",
        .type = OptionType::Bool,
        .hidden = !WITH_SCARD,
        .category = "NFC Card Readers",
        .disabled = !WITH_SCARD,
    },
    {
        .title = "HID SmartCard Order Flip",
        .name = "scardflip",
        .desc = "Flips the order of detection for P1/P2.",
        .type = OptionType::Bool,
        .hidden = !WITH_SCARD,
        .category = "NFC Card Readers",
        .disabled = !WITH_SCARD,
    },
    {
        .title = "HID SmartCard Order Toggle",
        .name = "scardtoggle",
        .desc = "Toggles reader between P1/P2 using the NumLock key state.",
        .type = OptionType::Bool,
        .hidden = !WITH_SCARD,
        .category = "NFC Card Readers",
        .disabled = !WITH_SCARD,
    },
    {
        // HIDSmartCardIdConvert
        .title = "HID SmartCard Fix UID",
        .name = "scardfix",
        .desc = "Modify behavior of SmartCard UID logic.\n\n"
            "legacy: Preserve buggy old behavior (Default)\n\n"
            "fix: Add E00401 to non-FeliCa cards, scan FeliCa cards as-is (Recommended for most users)\n\n"
            "all: Add E00401 to all cards, including FeliCa cards (For really old games only)\n\n"
            "The algorithm is simple; the prefix is applied, the scanned card number is appended upto 8 bytes, and remaining digits are discarded. "
            "For example, abcd56780123 is converted to e00401abcd567801 on card in. This results in different card numbers so "
            "be careful when connecting to servers!",
        .type = OptionType::Enum,
        .hidden = !WITH_SCARD,
        .category = "NFC Card Readers",
        .elements = {
            {"legacy", "Use cards as-is"},
            {"fix", "Fix bad cards only"},
            {"all", "Force all cards"},
        },
        .disabled = !WITH_SCARD,
    },
    {
        // SextetStreamPort
        .title = "SextetStream Port",
        .name = "sextet",
        .desc = "Use a SextetStream device on a given COM port.",
        .type = OptionType::Text,
        .category = "I/O Options",
    },
    {
        .title = "Enable BemaniTools 5 API",
        .name = "bt5api",
        .desc = "Enables partial BemaniTools 5 API compatibility layer.",
        .type = OptionType::Bool,
        .category = "BT5 API",
    },
    {
        .title = "Realtime Process Priority (DEPRECATED - use -processpriority instead)",
        .name = "realtime",
        .desc = "Sets the process priority to realtime, can help with odd lag spikes.",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Performance",
    },
    {
        // spice2x_ProcessPriority
        .title = "Process Priority",
        .name = "sp2x-processpriority",
        .display_name = "processpriority",
        .aliases= "processpriority",
        .desc = "Sets the process priority, can help with odd lag spikes. Default: high.",
        .type = OptionType::Enum,
        .category = "Performance",
        .elements = {
            {"belownormal", ""},
            {"normal", ""},
            {"abovenormal", ""},
            {"high", ""},
            {"realtime", ""}
        },
    },
    {
        // spice2x_ProcessAffinity
        .title = "Process Affinity",
        .name = "sp2x-processaffinity",
        .display_name = "processaffinity",
        .aliases= "processaffinity",
        .desc = "Set process affinity by calling SetProcessAffinityMask. "
                "Must provide a hexadecimal mask (e.g., 0x1ff00).",
        .type = OptionType::Hex,
        .category = "Performance",
    },
    {
        // spice2x_ProcessorEfficiencyClass
        .title = "Process Efficiency Class",
        .name = "sp2x-processefficiency",
        .display_name = "processefficiency",
        .aliases= "processefficiency",
        .desc = "Has no effect if -processaffinity is set. Default: Use all cores.",
        .type = OptionType::Enum,
        .category = "Performance",
        .elements = {
            {"all", "Use all cores"},
            {"pcores", "Performant cores only"},
            {"ecores", "Efficient cores only"},
        },
    },
    {
        .title = "Heap Size",
        .name = "h",
        .desc = "Custom heap size in bytes.",
        .type = OptionType::Integer,
        .category = "Development",
    },
    // TODO: remove this and create an ignore list
    {
        // DisableGSyncDetection
        .title = "(REMOVED) Disable G-Sync Detection",
        .name = "keepgsync",
        .desc = "Broken feature that was not implemented correctly.",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Graphics (Common)",
    },
    {
        // spice2x_NvapiProfile
        .title = "NVIDIA profile optimization",
        .name = "sp2x-nvprofile",
        .display_name = "nvprofile",
        .aliases= "nvprofile",
        .desc = "Creates an NVIDIA application profile for spice(64).exe and applies GPU tweaks.\n\n"
                "Disables G-SYNC by setting the Monitor Technology to 'Fixed Refresh', "
                "sets v-sync to 'Application Controlled', "
                "and the Power Management Mode to 'Prefer maximum performance'.\n\n"
                "G-SYNC may fail to disable properly; try enabling full-screen optimizations (FSO) for spice(64).exe.\n\n"
                "May not work as expected for some games; namely G-SYNC and SDVX.",
        .type = OptionType::Bool,
        .category = "Graphics (Common)",
    },
    {
        // DisableAudioHooks
        .title = "Disable All Spice Audio Hooks",
        .name = "audiohookdisable",
        .desc = "Disables all audio hooks, including device initialization and volume hooks.\n\n"
            "Default: off (allow Spice to hook IMMDeviceEnumerator for and optionally let "
                "you use a different backend instead of exclusive WASAPI).\n\n"
            "Check this if you want games to natively access your audio device.",
        .type = OptionType::Bool,
        .category = "Audio (Hacks)",
    },
    {
        // spice2x_DisableVolumeHook
        .title = "Disable Audio Volume Hook",
        .name = "sp2x-volumehookdisable",
        .display_name = "volumehookdisable",
        .aliases= "volumehookdisable",
        .desc = "Disables audio volume hook.\n\n"
            "Default: off (prevent games from changing audio volume by hooking IAudioEndpointVolume).\n\n"
            "Check this if you want games to freely change your volume.",
        .type = OptionType::Bool,
        .category = "Audio (Hacks)",
    },
    {
        .title = "Spice Audio Hook Backend",
        .name = "audiobackend",
        .desc = "Selects the audio backend to use when spice audio hook is enabled, overriding exclusive WASAPI. "
            "Does nothing for games that do not output to exclusive WASAPI.",
        .type = OptionType::Enum,
        .category = "Audio",
        .elements = {
            {"asio", "ASIO"},
            {"waveout", "broken, do not use"}
        },
    },
    {
        .title = "Spice Audio Hook ASIO Driver ID",
        .name = "asiodriverid",
        .desc = "Selects the ASIO driver id to use when Spice Audio Backend is set to ASIO.",
        .type = OptionType::Integer,
        .category = "Audio",
    },
    {
        .title = "WASAPI Dummy Context",
        .name = "audiodummy",
        .desc = "Uses a dummy `IAudioClient` context to maintain full audio control. "
            "This is automatically enabled when required and not normally needed.",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Audio (Hacks)",
    },
    {
        // DelayBy5Seconds
        .title = "Delay by 5 Seconds (DEPRECATED - use -sleepduration instead)",
        .name = "sleep",
        .desc = "Waits five seconds before starting the game.",
        .type = OptionType::Bool,
        .hidden = true, // superseded by sp2x-sleep_duration
        .category = "Miscellaneous",
    },
    {
        // spice2x_DelayByNSeconds
        .title = "Delay Game Launch",
        .name = "sp2x-sleepduration",
        .display_name = "sleepduration",
        .aliases= "sleepduration",
        .desc = "Wait for N seconds before starting the game.",
        .type = OptionType::Integer,
        .category = "Miscellaneous",
    },
    {
        .title = "Load KBT/KLD Stubs",
        .name = "stubs",
        .desc = "Enables loading kbt/kld stub files.",
        .type = OptionType::Bool,
        .category = "Development",
    },
    {
        .title = "Adjust Display Orientation (DEPRECATED - use -autoorientation instead)",
        .name = "adjustorientation",
        .desc = "Automatically adjust the orientation of your display in portrait games. "
                "WARNING: game may launch at incorrect refresh rate! Use in combination with "
                "-graphics-force-refresh flag to fix.",
        .type = OptionType::Bool,
        .hidden = true, // superseded by sp2x-autoorientation
        .category = "Graphics (Common)",
    },
    {
        // spice2x_AutoOrientation
        .title = "Auto-rotate Display",
        .name = "sp2x-autoorientation",
        .display_name = "autoorientation",
        .aliases= "autoorientation",
        .desc = "Automatically adjust the orientation of your display when launched. "
                "WARNING: game may launch at incorrect refresh rate! Use in combination with "
                "-graphics-force-refresh and potentially either -9on12 or full-screen optimizations (FSO) to fix.",
        .type = OptionType::Enum,
        .category = "Graphics (Common)",
        // match graphics_orientation
        .elements = {{"0", "90 (CW)"}, {"1", "270 (CCW)"}},
    },
    {
        .title = "AVS Log Level",
        .name = "loglevel",
        .desc = "Set the level of detail for AVS log messages written to the log. Does not affect logging from spice.",
        .type = OptionType::Enum,
        .category = "Debug Log",
        .elements = {{"fatal", ""}, {"warning", ""}, {"info", ""}, {"misc", ""}, {"all", ""}, {"disable", ""}},
    },
    {
        .title = "EA Automap",
        .name = "automap",
        .desc = "Enable automap in patch configuration.",
        .type = OptionType::Bool,
        .category = "Network (Development)",
    },
    {
        .title = "EA Netdump",
        .name = "netdump",
        .desc = "Enable automap in network dumping configuration.",
        .type = OptionType::Bool,
        .category = "Network (Development)",
    },
    {
        .title = "Discord RPC AppID Override",
        .name = "discordappid",
        .desc = "Set the discord RPC AppID override.",
        .type = OptionType::Text,
        .category = "Development",
    },
    {
        .title = "Blocking Logger",
        .name = "logblock",
        .desc = "Slower but safer logging used for debugging.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        .title = "Debug CreateFile",
        .name = "createfiledebug",
        .desc = "Outputs CreateFile debug prints.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        .title = "Verbose Graphics Logging",
        .name = "graphicsverbose",
        .desc = "Enable the verbose logging of graphics hook code.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        .title = "Verbose AVS Logging",
        .name = "avsverbose",
        .desc = "Enable the verbose logging of AVS filesystem functions.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        // AllowEA3Verbose
        .title = "Allow EA3 Verbose Debug Logging",
        .name = "ea3verbose",
        .desc = "This option is only useful for network developers; leave this OFF.\n\n"
            "By default, ea3/debug/verbose node is removed by spice to prevent exposing PCBID in the logs. "
            "When this is enabled, ea3/debug/verbose node in the XML will be kept and exposed to the game.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        .title = "Disable Colored Output",
        .name = "nocolor",
        .desc = "Disable terminal colors for log outputs to console.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        .title = "Disable ACP Hook",
        .name = "acphookdisable",
        .desc = "Force disable advanced code pages hooks for encoding.",
        .type = OptionType::Bool,
        .category = "Development",
    },
    {
        .title = "Disable Signal Handling",
        .name = "signaldisable",
        .desc = "Force disable signal handling.",
        .type = OptionType::Bool,
        .category = "Development",
    },
    {
        .title = "Disable Debug Hooks",
        .name = "dbghookdisable",
        .desc = "Disable hooks for debug functions (e.g. OutputDebugString).",
        .type = OptionType::Bool,
        .category = "Development",
    },
    {
        .title = "Disable AVS VFS Drive Mount Redirection",
        .name = "avs-redirect-disable",
        .desc = "Disable D:/E:/F: AVS VFS mount redirection.",
        .type = OptionType::Bool,
        .category = "Development",
    },
    {
        // DisableAvsCache
        .title = "Disable AVS Cache",
        .name = "avscachedisable",
        .desc = "Disable optimization used for some games to cache data file info (only for IIDX32+ for now).",
        .type = OptionType::Bool,
        .category = "Development",
    },
    {
        .title = "Output PEB",
        .name = "pebprint",
        .desc = "Prints PEB on startup to console.",
        .type = OptionType::Bool,
        .category = "Debug Log",
    },
    {
        // DumpSystemInfo
        .title = "Dump System Information",
        .name = "sysdump",
        .desc = "Print system information to the log on startup. Default: basic.",
        .type = OptionType::Enum,
        .category = "Debug Log",
        .elements = {
            {"none", "Nothing"},
            {"basic", "OS, CPU, SMBIOS, GPU"},
            {"all", "basic + HID devices"},
        },
    },
    {
        .title = "QKS Arguments Override",
        .name = "qksargs",
        .desc = "Command line arguments passed to the game.",
        .type = OptionType::Text,
        .setting_name = "",
        .game_name = "QuizKnock STADIUM",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "CCJ Arguments Override",
        .name = "ccjargs",
        .desc = "Command line arguments passed to the game. "
            "If left blank, '-nomatchselect' is applied, which disables matchmaking debug menu.",
        .type = OptionType::Text,
        .setting_name = "",
        .game_name = "Chase Chase Jokers",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "CCJ Mouse Trackball",
        .name = "ccjmousetb",
        .desc = "Use mouse for trackball input; disables any keyboard / joystick control for the trackball. "\
            "Hold RMB and move the cursor to control the trackball.",
        .type = OptionType::Bool,
        .game_name = "Chase Chase Jokers",
        .category = "Game Options",
    },
    {
        .title = "CCJ Mouse Trackball Toggle",
        .name = "ccjmousetbt",
        .desc = "Instead of holding RMB, click RMB to toggle trackball input.",
        .type = OptionType::Bool,
        .game_name = "Chase Chase Jokers",
        .category = "Game Options",
    },
    {
        .title = "CCJ Trackball Sensitivity",
        .name = "ccjtrackballsens",
        .desc = "Adjust sensitivity of trackball. Affects both digital and analog input. Default: 10.",
        .type = OptionType::Integer,
        .setting_name = "(0-255)",
        .game_name = "Chase Chase Jokers",
        .category = "Game Options",
    },
    {
        .title = "MFG Arguments Override",
        .name = "mfgargs",
        .desc = "Command line arguments passed to the game.",
        .type = OptionType::Text,
        .setting_name = "",
        .game_name = "Mahjong Fight Girl",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "MFG Cabinet Type",
        .name = "mfgcabtype",
        .desc = "MFG Cabinet Type. Default is HG.",
        .type = OptionType::Enum,
        .setting_name = "",
        .game_name = "Mahjong Fight Girl",
        .category = "Game Options",
        .elements = {
            {"HG", "HG"},
            {"B", "B"},
            {"C", "C"},
            {"UKS", "UKS"},
        },
    },
    {
        .title = "MFG Disable IO Emulation",
        .name = "mfgnoio",
        .desc = "Disables BI2X hooks for MFG.",
        .type = OptionType::Bool,
        .setting_name = "",
        .game_name = "Mahjong Fight Girl",
        .category = "Game Options (Advanced)"
    },
    {
        // PCArgs
        .title = "PC Arguments Override",
        .name = "pcargs",
        .desc = "Command line arguments passed to the game.",
        .type = OptionType::Text,
        .setting_name = "",
        .game_name = "Polaris Chord",
        .category = "Game Options (Advanced)",
    },
    {
        // PCNoIO
        .title = "PC Disable IO Emulation",
        .name = "pcnoio",
        .desc = "Disables BI2X hooks for PC.",
        .type = OptionType::Bool,
        .setting_name = "",
        .game_name = "Polaris Chord",
        .category = "Game Options (Advanced)"
    },
    {
        // PCKnobMode
        .title = "PC Fader Knobs Mode",
        .name = "pcknobs",
        .desc = "Allows SDVX knobs and IIDX turntables to be bound as Polaris Chord faders.",
        .type = OptionType::Bool,
        .game_name = "Polaris Chord",
        .category = "Game Options",
    },
    {
        // spice2x_LightsOverallBrightness
        .title = "Lights Brightness Adjustment",
        .name = "sp2x-lights-brightness",
        .display_name = "lightsbrightness",
        .aliases= "lightsbrightness",
        .desc = "Modify HID output values, measured in %. Default is 100. "
                "Requires compatible hardware, not just any LED.",
        .type = OptionType::Integer,
        .setting_name = "(0-100)",
        .category = "I/O Options",
    },
    {
        // spice2x_WindowBorder
        .title = "Window Border Style",
        .name = "sp2x-windowborder",
        .display_name = "windowborder",
        .aliases= "windowborder",
        .desc = "For windowed mode: overrides window border style. "
            "Does not work for all games; can possibly make the game crash! "
            "Can also be changed in Screen Resize window (default F11).",
        .type = OptionType::Enum,
        .category = "Graphics (Windowed)",
        // match cfg::WindowDecorationMode
        .elements = {
            {"0", "default"},
            {"1", "borderless"},
            {"2", "resizable window"},
        },
    },
    {
        // spice2x_WindowSize
        .title = "Window Size",
        .name = "sp2x-windowsize",
        .display_name = "windowsize",
        .aliases= "windowsize",
        .desc = "For windowed mode: override for window size, width and height separated by comma. "
            "Can also be changed in Screen Resize window (default F11).",
        .type = OptionType::Text,
        .setting_name = "1280,720",
        .category = "Graphics (Windowed)",
    },
    {
        // spice2x_WindowPosition
        .title = "Window Position",
        .name = "sp2x-windowpos",
        .display_name = "windowpos",
        .aliases= "windowpos",
        .desc = "For windowed mode: override for window position, x and y separated by comma. "
            "Can also be changed in Screen Resize window (default F11).",
        .type = OptionType::Text,
        .setting_name = "120,240",
        .category = "Graphics (Windowed)",
    },
    {
        // spice2x_WindowAlwaysOnTop
        .title = "Window Always on Top",
        .name = "sp2x-windowalwaysontop",
        .display_name = "windowalwaysontop",
        .aliases= "windowalwaysontop",
        .desc = "For windowed mode: make window to be always on top of other windows. "
            "Can also be changed in Screen Resize window (default F11).",
        .type = OptionType::Bool,
        .category = "Graphics (Windowed)",
    },
    {
        // WindowForceScaling
        .title = "Window Forced Render Scaling",
        .name = "windowscale",
        .desc = "For windowed mode: forcibly set DX9 back buffer dimensions to match window size. "
            "Reduces pixelated scaling artifacts. Works great for some games, but can COMPLETELY BREAK other games - YMMV!",
        .type = OptionType::Bool,
        .category = "Graphics (Windowed)",
    },
    {
        // spice2x_IIDXWindowedSubscreenSize
        .title = "IIDX Windowed Subscreen Size",
        .name = "iidxwsubsize",
        .desc = "Size of the subscreen window. Defaults to (1280,720).",
        .type = OptionType::Text,
        .setting_name = "1280,720",
        .game_name = "Beatmania IIDX",
        .category = "Graphics (Windowed)",
    },
    {
        // spice2x_IIDXWindowedSubscreenPosition
        .title = "IIDX Windowed Subscreen Position",
        .name = "iidxwsubpos",
        .desc = "Initial position of the subscreen window. Defaults to (0,0).",
        .type = OptionType::Text,
        .setting_name = "0,0",
        .game_name = "Beatmania IIDX",
        .category = "Graphics (Windowed)",
    },
    {
        // IIDXWindowedSubscreenBorderless
        .title = "IIDX Windowed Subscreen Borderless",
        .name = "iidxwsubborderless",
        .desc = "Remove window decoration from windowed subscreen.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Graphics (Windowed)",
    },
    {
        // IIDXWindowedSubscreenAlwaysOnTop
        .title = "IIDX Windowed Subscreen Always On Top",
        .name = "iidxwsubtop",
        .desc = "Keep windowed subscreen on top.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Graphics (Windowed)",
    },
    {
        // spice2x_JubeatLegacyTouch
        .title = "JB Legacy Touch Targets",
        .name = "sp2x-jubeatlegacytouch",
        .display_name = "jubeatlegacytouch",
        .aliases= "jubeatlegacytouch",
        .desc = "For touch screen players - use the legacy & less accurate grid-based layout for touch recognition, "
            "instead of the new & more accurate touch targets. Default: off.",
        .type = OptionType::Bool,
        .game_name = "Jubeat",
        .category = "Game Options",
    },
    {
        // spice2x_RBTouchScale
        .title = "RB Scale Touch Input",
        .name = "sp2x-rbscaletouch",
        .display_name = "rbscaletouch",
        .aliases= "rbscaletouch",
        .desc = "Apply scaling to make touch area smaller than the screen. "
            "Specify a number out of 1000 (e.g., 800 means 80%). Default: 1000.",
        .type = OptionType::Integer,
        .game_name = "Reflec Beat",
        .category = "Game Options",
    },
    {
        // spice2x_AsioForceUnload
        .title = "ASIO Force Unload On Stop",
        .name = "sp2x-asioforceunload",
        .display_name = "asioforceunload",
        .aliases= "asioforceunload",
        .desc = "For certain buggy ASIO drivers, force unload of ASIO driver when audio stream stops. "
            "Used for working around ASIO drivers that lock up after force quitting games.",
        .type = OptionType::Bool,
        .category = "Audio (Hacks)",
    },
    {
        // spice2x_IIDXNoESpec
        .title = "IIDX Disable E-spec I/O",
        .name = "sp2x-iidxnoespec",
        .display_name = "iidxnoespec",
        .aliases= "iidxnoespec",
        .desc = "Disable I/O emulation for E-spec I/O upgrade (GELDJ-JX kit).",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // spice2x_IIDXWindowedTDJ
        .title = "IIDX TDJ Windowed Mode (DEPRECATED - just use -iidxtdj and -w together)",
        .name = "sp2x-iidxtdjw",
        .display_name = "iidxtdjw",
        .aliases= "iidxtdjw",
        .desc = "Enable TDJ, optimized for windowed mode. "
            "Press Toggle Subscreen button and use your mouse on the main window.",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "Beatmania IIDX",
        .category = "Game Options",
    },
    {
        // spice2x_DRSDisableTouch
        .title = "DRS Disable Touch Input",
        .name = "sp2x-drsdisabletouch",
        .display_name = "drsdisabletouch",
        .aliases= "drsdisabletouch",
        .desc = "Disables multitouch input.",
        .type = OptionType::Bool,
        .game_name = "DANCERUSH",
        .category = "Game Options",
    },
    {
        // spice2x_DRSTransposeTouch
        .title = "DRS Transpose Touch Input",
        .name = "sp2x-drstransposetouch",
        .display_name = "drstransposetouch",
        .aliases= "drstransposetouch",
        .desc = "Swap x and y values of touch input.",
        .type = OptionType::Bool,
        .game_name = "DANCERUSH",
        .category = "Game Options",
    },
    {
        // DRSRGBCameraHook
        .title = "DRS RGB Camera Hook",
        .name = "drsrgbcamhook",
        .desc = "Hook into the RGB camera detection and attempt to use your own non-official webcam. "
            "Not every webcam will be compatible with this; YMMV.\n\n"
            "Note: this is NOT for the motion camera - you still need a real RealSense camera for that!",
        .type = OptionType::Bool,
        .game_name = "DANCERUSH",
        .category = "Game Options",
    },
    {
        // spice2x_IIDXNativeTouch
        .title = "IIDX Native Touch Handling",
        .name = "sp2x-iidxnativetouch",
        .display_name = "iidxnativetouch",
        .aliases= "iidxnativetouch",
        .desc = "Disables touch hooks and lets the game access a touch screen directly. "
                "Requires a touch screen to be connected as a secondary monitor. "
                "Touch input must be routed to the primary screen via Windows Tablet PC settings. "
                "Enable this when you get duplicate touch inputs from an actual touch screen.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // spice2x_IIDXNoSub
        .title = "IIDX TDJ Subscreen Disable",
        .name = "sp2x-iidxnosub",
        .display_name = "iidxnosub",
        .aliases= "iidxnosub",
        .desc = "Prevents TDJ subscreen from launching a separate window.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options",
    },
    {
        // spice2x_IIDXEmulateSubscreenKeypadTouch
        .title = "IIDX TDJ Subscreen Keypad Touch Emulation",
        .name = "sp2x-iidxsubpoke",
        .display_name = "iidxsubpoke",
        .aliases = "iidxsubpoke",
        .desc = "Emulates touches on TDJ subscreen when keypad input is received. "
            "Should be used along with 'Unscramble Touch Screen Keypad in TDJ' patch. "
            "00 is mapped to erase button, and Decimal shows/hides the keypad.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // spice2x_AutoCard
        .title = "Auto Card Insert",
        .name = "sp2x-autocard",
        .display_name = "autocard",
        .aliases= "autocard",
        .desc =
            "Periodically present virtual card(s) to the reader, removing the need to press Insert Card button. "
            "Only applies to local virtual cards (-card0, -card1, or card0/1.txt), "
            "do not use with physical card reader or API.",
        .type = OptionType::Enum,
        .category = "Network",
        .elements = {
            {"off", ""},
            {"p1", ""},
            {"p2", ""},
            {"both", ""},
        },
    },
    {
        // spice2x_LowLatencySharedAudio
        .title = "Low Latency Shared Audio",
        .name = "sp2x-lowlatencysharedaudio",
        .display_name = "lowlatencysharedaudio",
        .aliases= "lowlatencysharedaudio",
        .desc = "Force the usage of smallest buffer size supported by the device when shared mode audio is used. "
            "Works for games using DirectSound or shared WASAPI; no effect for exclusive WASAPI and ASIO. " 
            "For best results (under 10ms), use the default Windows inbox audio driver instead of manufacturer supplied driver. " 
            "Requires Windows 10 and above.",
        .type = OptionType::Bool,
        .category = "Audio",
    },
    {
        // DefaultAudioDeviceOverride
        .title = "WASAPI Default Audio Device",
        .name = "wasapidefault",
        .desc = "Use a specific audio device as the default audio device instead of what is "
            "set as the Windows default device. Only works for WASAPI Exclusive or Shared games; "
            "for legacy DirectSound, use the Windows volume mixer, for ASIO, use ASIO options.\n\n"
            "You can use the name of the device or the ID. Partial match is allowed. "
            "If the name contains non-Latin characters, you must use the device ID instead.\n\n"
            "To obtain the device ID, launch the game once, and look for "
            "'active audio devices from EnumAudioEndpoints', and copy the ID of your device. "
            "If you don't see this, either the game does not use WASAPI, or you disabled audio hooks."
            "",
        .type = OptionType::Text,
        .category = "Audio",
    },
    {
        // spice2x_TapeLedAlgorithm
        .title = "Tape LED Avg Algorithm",
        .name = "sp2x-tapeledalgo",
        .display_name = "tapeledalgo",
        .aliases= "tapeledalgo",
        .desc = "For games with light arrays, determine the algorithm that is used to translate them into a single light binding in Lights tab. "
            "Default: mid.",
        .type = OptionType::Enum,
        .category = "I/O Options",
        .elements = {
            {"off", "Off"},
            {"first", "First LED"},
            {"mid", "Middle LED"},
            {"last", "Last LED"},
            {"avg", "Average color"},
        },
    },
    {
        // spice2x_NoNVAPI
        .title = "NVAPI Block",
        .name = "sp2x-nonvapi",
        .display_name = "nonvapi",
        .aliases= "nonvapi",
        .desc = "Completely block the game from accessing NVAPI. Can be used if NVAPI is returning undesirable "
            "results to the game (e.g., wrong values from NvDisplayConfig for SDVX). You may need to apply additional "
            "hex edits in order to boot the game correctly.",
        .type = OptionType::Bool,
        .category = "Graphics (Common)",
    },
    {
        // spice2x_NoD3D9DeviceHook
        .title = "Disable D3D9 Device Hook (DEPRECATED - no longer needed for TDJ recording)",
        .name = "sp2x-nod3d9devhook",
        .display_name = "nod3d9devhook",
        .aliases= "nod3d9devhook",
        .desc = "This was previously required for TDJ play recording, but as of 2024-08-04, it is no longer needed. "
            "Completely disable all DXD9 device hook, preventing features like Spice overlay, subscreen window, "
            "screenshots, screen resize, touch emulation, streaming to Companion, etc.",
        .type = OptionType::Bool,
        .hidden = true,
        .category = "Graphics (Common)",
    },
    {
        // spice2x_SDVXNoSub
        .title = "SDVX Subscreen Disable",
        .name = "sp2x-sdvxnosub",
        .display_name = "sdvxnosub",
        .aliases= "sdvxnosub",
        .desc = "Prevents VM subscreen from launching a separate window.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Game Options",
    },
    {
        // SDVXFullscreenLandscape
        .title = "(DISABLED) SDVX Landscape Mode (SDVX5+)",
        .name = "sdvxlandscape",
        .desc =
            "Option HIDDEN and DISABLED due to it affecting in-game camera angle for displaying lanes.\n\n"
            "Allows you to play in landscape by transposing resolution and applying image scaling.\n\n"
            "Works only for SDVX5 and above! This is identical to -forceresswap.\n\n"
            "Will launch at 1080p by default; strongly consider combining this with -forceres option to render at monitor native resolution.",
        .type = OptionType::Bool,
        .hidden = true,
        .game_name = "Sound Voltex",
        .category = "Game Options",
    },
    {
        // spice2x_EnableSMXStage
        .title = "StepManiaX Stage Lighting Support",
        .name = "sp2x-smx-stage",
        .display_name = "smxstage",
        .aliases= "smxstage",
        .desc = "StepmaniaX stage will show up as a device that can recieve lighting output. "
            "For configurator, restart spicecfg.exe after enabling this to have the device show up for binding.",
        .type = OptionType::Bool,
        .category = "I/O Options",
    },
    {
        // spice2x_EnableSMXDedicab
        .title = "StepManiaX Dedicated Cabinet Lighting Support",
        .name = "sp2x-smx-dedicab",
        .display_name = "smxdedicab",
        .aliases = "smxdedicab",
        .desc = "StepManiaX Dedicated Cabinet will show up as a device that can recieve lighting output. "
            "For configurator, restart spicecfg.exe after enabling this to have the device show up for binding.",
        .type = OptionType::Bool,
        .category = "I/O Options"
    },
    {
        // IIDXRecQuality
        .title = "IIDX Recording Quality",
        .name = "iidxreccqp",
        .desc = "WARNING: double check if your network allows this & your hardware supports it.\n\n"
            "For TDJ Play Record feature, specify ConstQP quality settings for NVENC. Lower is higher quality but bigger file.\n\n"
            "Two options: specify a single number (20) to set CQ Level, or specify comma-separated numbers (23,25,20) to set P, B, I levels.",
        .type = OptionType::Text,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // IIDXRecDisable
        .title = "IIDX Recording Force Disable",
        .name = "iidxnorec",
        .desc = "When enabled, this prevents the play record feature from being used.",
        .type = OptionType::Bool,
        .game_name = "Beatmania IIDX",
        .category = "Game Options (Advanced)",
    },
    {
        // MidiAlgoVer
        .title = "MIDI Note Input Algorithm",
        .name = "midialgo",
        .desc =
            "Remember to restart after changing this value.\n\n"
            "Switch algorithm used for MIDI note handling.\n\n"
            "auto (default): use v2 for most games, v2_drum for DrumMania and FutureTomTom\n\n"
            "v2: new delta-time logic in spice2x, timestamp every on/off events and apply min. sustain period to rapid on/off triggers, supports velocity threshold\n\n"
            "v2_drum: same as v2, but disable holds; better for some drum kits\n\n"
            "legacy: legacy poll-based spicetools logic, may behave incorrectly when the same input is mapped to multiple buttons, no support for velocity threshold.",
        .type = OptionType::Enum,
        .category = "I/O Options",
        .elements = {
            {"auto", ""},
            {"v2", ""},
            {"v2_drum", ""},
            {"legacy", ""},
        },
    },
    {
        // MidiNoteSustain
        .title = "MIDI Note Min. Sustain",
        .name = "midisustain",
        .desc =
            "Remember to restart after changing this value. Only affects v2 MIDI algorithm. Does not affect spicecfg.\n\n"
            "Set the minimum duration (in milliseconds) each note is sustained, so that the game's I/O can pick up the input when polled.\n\n"
            "If this is too short, when game polls for input, it may fail to observe quick input (such as drums). "
            "If this is too long, rapid hits may be dropped due to coalescing.\n\n"
            "Recommended to keep at default of 20ms unless you have issues.",
        .type = OptionType::Integer,
        .setting_name = "20",
        .category = "I/O Options",
    },
    {
        // DDRP4IOBufferMode
        .title = "DDR P4IO Buffer Algorithm",
        .name = "ddrp4iobuffer",
        .desc =
            "Remember to restart after changing this value.\n\n"
            "Sets the algorithm used to populate entries to the buffer of controller polls read by the game.\n\n"
            "auto (default): thread mode if <120Hz, backfill if >=120Hz\n\n"
            "thread: starts a thread to periodically insert polls into the buffer\n\n"
            "backfill: fills the buffer on each frame with last known state info at short regular intervals up to the current time, then writes the current state.\n\n"
            "Only has an effect when emulating P4IO (arkmdxp4.dll).",
        .type = OptionType::Enum,
        .game_name = "Dance Dance Revolution",
        .category = "Game Options (Advanced)",
        .elements = {
            {"auto", ""},
            {"thread", ""},
            {"backfill", ""},
        },
    },
    {
        // InputRequiresFocus
        .title = "Input Requires Focus",
        .name = "inputfocus",
        .desc =
            "Determine input behavior when game window is not in focus.\n\n"
            "never: allow input regardless of focus\n"
            "naive (default): only naive bindings check for window focus, rawinput binds are always allowed\n"
            "always: only process input when window is in focus.",
        .type = OptionType::Enum,
        .category = "I/O Options",
        .elements = {
            {"never", ""},
            {"naive", ""},
            {"always", ""},
        },
    },
    {
        // NostalgiaPoke
        .title = "Nost Screen Poke",
        .name = "nostpoke",
        .desc =
            "Enables emulation of touch gestures to make it easier to navigate the game without a touchscreen.\n\n"
            "This enables two things:\n"
            "  1. Swipe/Touch bindings in Buttons tab (Swipe Next Page, etc)\n"
            "  2. PIN pad bindings (make sure Unscramble Keypad patch is applied)).",
        .type = OptionType::Bool,
        .game_name = "Nostalgia",
        .category = "Game Options",
    },
    {
        // ForceBackBufferCount
        .title = "V-Sync Buffering",
        .name = "vsyncbuffer",
        .desc = "Using triple buffering may improve cases of micro-stuttering but increases input latency.",
        .type = OptionType::Enum,
        .category = "Graphics (Common)",
        .elements = {
            {"2", "double buffer"},
            {"3", "triple buffer"},
        },
    },
    {
        // SDVXWindowedSubscreenSize
        .title = "SDVX Windowed Subscreen Size",
        .name = "sdvxwsubsize",
        .desc = "Size of the subscreen window. Defaults to (1920,1080).",
        .type = OptionType::Text,
        .setting_name = "1920,1080",
        .game_name = "Sound Voltex",
        .category = "Graphics (Windowed)",
    },
    {
        // SDVXWindowedSubscreenPosition
        .title = "SDVX Windowed Subscreen Position",
        .name = "sdvxwsubpos",
        .desc = "Initial position of the subscreen window. Defaults to (0,0).",
        .type = OptionType::Text,
        .setting_name = "0,0",
        .game_name = "Sound Voltex",
        .category = "Graphics (Windowed)",
    },
    {
        // SDVXWindowedSubscreenBorderless
        .title = "SDVX Windowed Subscreen Borderless",
        .name = "sdvxwsubborderless",
        .desc = "Remove window decoration from windowed subscreen.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Graphics (Windowed)",
    },
    {
        // SDVXWindowedSubscreenAlwaysOnTop
        .title = "SDVX Windowed Subscreen Always On Top",
        .name = "sdvxwsubtop",
        .desc = "Keep windowed subscreen on top.",
        .type = OptionType::Bool,
        .game_name = "Sound Voltex",
        .category = "Graphics (Windowed)",
    },
    {
        // LovePlusCamEnable
        .title = "LovePlus Camera Enable",
        .name = "lovepluscam",
        .desc = "Allow game to access camera; camera must be compatible with game.",
        .type = OptionType::Bool,
        .game_name = "LovePlus",
        .category = "Game Options",
    },
    {
        // LovePlusPrinterOutputPath
        .title = "LovePlus Printer Output Path",
        .name = "lpprinterpath",
        .desc = "Path to folder where images will be stored.",
        .type = OptionType::Text,
        .game_name = "LovePlus",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "LovePlus Printer Output Clear",
        .name = "lpprinterclear",
        .desc = "Clean up saved images in the output directory on startup.",
        .type = OptionType::Bool,
        .game_name = "LovePlus",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "LovePlus Printer Output Overwrite",
        .name = "lpprinteroverwrite",
        .desc = "Always overwrite the same file in output directory.",
        .type = OptionType::Bool,
        .game_name = "LovePlus",
        .category = "Game Options (Advanced)",
    },
    {
        // LovePlusPrinterOutputFormat
        .title = "LovePlus Printer Output Format",
        .name = "lpprinterformat",
        .desc = "File format for printer output.",
        .type = OptionType::Text,
        .setting_name = "(png/bmp/tga/jpg)",
        .game_name = "LovePlus",
        .category = "Game Options (Advanced)",
    },
    {
        .title = "LovePlus Printer JPG Quality",
        .name = "lpprinterjpgquality",
        .desc = "Quality setting in percent if JPG format is used.",
        .type = OptionType::Integer,
        .setting_name = "(0-100)",
        .game_name = "LovePlus",
        .category = "Game Options (Advanced)",
    },
    {
        // OptionConflictResolution
        .title = "Command Line Args Override",
        .name = "cmdoverride",
        .desc = "By default, option values in spicecfg take precedence over command-line args, for legacy compat.\n"
            "When this is specified in command line, command-line args take precedence instead.",
        .type = OptionType::Bool,
        .category = "Development",
        .disabled = true,
    }
};

const std::vector<std::string> &launcher::get_categories(Options::OptionsCategory category) {
    switch (category) {
        case Options::OptionsCategory::API:
            return CATEGORY_ORDER_API;
        case Options::OptionsCategory::Basic:
            return CATEGORY_ORDER_BASIC;
        case Options::OptionsCategory::Advanced:
            return CATEGORY_ORDER_ADVANCED;
        case Options::OptionsCategory::Dev:
            return CATEGORY_ORDER_DEV;
        case Options::OptionsCategory::Everything:
        default:
            return CATEGORY_ORDER_NONE;
    }
}

const std::vector<OptionDefinition> &launcher::get_option_definitions() {
    return OPTION_DEFINITIONS;
}

std::unique_ptr<std::vector<Option>> launcher::parse_options(int argc, char *argv[]) {

    // generate options
    auto &definitions = get_option_definitions();
    auto options = std::make_unique<std::vector<Option>>();

    options->reserve(definitions.size());

    for (auto &definition : definitions) {

        // create aliases
        std::vector<std::string> aliases;
        aliases.push_back(definition.name);
        if (!definition.aliases.empty()) {
            strsplit(definition.aliases, aliases, '/');
        }

        // create option
        auto &option = options->emplace_back(definition, "");

        // check if enabled
        for (int i = 1; i < argc; i++) {

            // ignore leading '-' characters
            auto argument = argv[i];
            while (argument[0] == '-') {
                argument++;
            }

            // check aliases
            for (const auto &alias : aliases) {
                if (_stricmp(alias.c_str(), argument) == 0) {
                    switch (definition.type) {
                        case OptionType::Bool: {
                            option.value_add("/ENABLED");
                            break;
                        }
                        case OptionType::Integer: {
                            if (++i >= argc) {
                                log_fatal("options", "missing parameter for -{}", alias);
                            } else {
                                // validate it is an integer
                                char *p;
                                strtol(argv[i], &p, 10);
                                if (*p) {
                                    log_fatal("options", "parameter for -{} is not a number: {}", alias, argv[i]);
                                } else {
                                    option.value_add(argv[i]);
                                }
                            }
                            break;
                        }
                        case OptionType::Hex: {
                            if (++i >= argc) {
                                log_fatal("options", "missing parameter for -{}", alias);
                            } else {
                                // validate it is an integer
                                try {
                                    auto _ = std::stoull(argv[i], nullptr, 16);
                                } catch (const std::exception &ex) {
                                    log_fatal("options", "parameter for -{} is not a hex number: {}", alias, argv[i]);
                                }
                            }
                            break;
                        }
                        case OptionType::Enum:
                        case OptionType::Text: {
                            if (++i >= argc) {
                                log_fatal("options", "missing parameter for -{}", alias);
                            } else {
                                option.value_add(argv[i]);
                            }
                            break;
                        }
                        default: {
                            log_warning("options", "unknown option type: {} (-{})", definition.type, alias);
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    // positional arguments
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {

        // check if enabled
        bool found = false;
        for (auto &definition : definitions) {

            // create aliases
            std::vector<std::string> aliases;
            aliases.push_back(definition.name);
            if (!definition.aliases.empty()) {
                strsplit(definition.aliases, aliases, '/');
            }

            // ignore leading '-' characters
            auto argument = argv[i];
            while (argument[0] == '-') {
                argument++;
            }

            // check aliases
            for (const auto &alias : aliases) {
                if (_stricmp(alias.c_str(), argument) == 0) {
                    found = true;
                    switch (definition.type) {
                        case OptionType::Bool:
                            break;
                        case OptionType::Integer:
                        case OptionType::Hex:
                        case OptionType::Text:
                        case OptionType::Enum:
                            i++;
                            break;
                    }
                    break;
                }
            }

            // early quit
            if (found) {
                break;
            }
        }

        // positional argument
        if (!found && *argv[i] != '-') {
            positional.emplace_back(argv[i]);
        }
    }

    // game executable
    if (!positional.empty()) {
        options->at(launcher::Options::GameExecutable).value = positional[0];
    }

    // return vector
    return options;
}

std::vector<Option> launcher::merge_options(
        const std::vector<Option> &options,
        const std::vector<Option> &overrides)
{
    std::vector<Option> merged;

    for (const auto &option : options) {
        for (const auto &override : overrides) {
            if (option.get_definition().name == override.get_definition().name) {
                if (override.is_active()) {
                    if (option.is_active()) {
                        auto &new_option = merged.emplace_back(option.get_definition(), "");
                        new_option.disabled = true;

                        if (USE_CMD_OVERRIDE) {
                            // command-line arguments take precedence (opt-in)
                            for (auto &value : override.values()) {
                                new_option.value_add(value);
                            }
                            for (auto &value : option.values()) {
                                new_option.value_add(value);
                            }
                        } else {
                            // spicecfg options take precedence (default)
                            // this sucks, but it's the default for legacy spicetools compat
                            for (auto &value : option.values()) {
                                new_option.value_add(value);
                            }
                            for (auto &value : override.values()) {
                                new_option.value_add(value);
                            }
                        }
                        new_option.conflicting = true;
                    } else {
                        auto &new_option = merged.emplace_back(override.get_definition(), "");
                        new_option.disabled = true;

                        for (auto &value : override.values()) {
                            new_option.value_add(value);
                        }
                    }
                } else {
                    merged.push_back(option);
                }
                break;
            }
        }
    }
    return merged;
}

std::string launcher::detect_bootstrap_release_code(const std::string& bootstrap_user) {
    std::string bootstrap_path;

    if (!bootstrap_user.empty()) {
        bootstrap_path = bootstrap_user;
    } else if (fileutils::file_exists("prop/bootstrap.xml")) {
        bootstrap_path = "prop/bootstrap.xml";
    }

    if (bootstrap_path.empty()) {
        log_warning("options", "unable to detect bootstrap.xml file");
        return "";
    }

    // load XML
    tinyxml2::XMLDocument bootstrap;
    if (bootstrap.LoadFileA(bootstrap_path.c_str()) != tinyxml2::XML_SUCCESS) {
        log_warning("options", "unable to parse {}", bootstrap_path);
        return "";
    }

    // find release_code
    auto node_root = bootstrap.LastChild();
    if (node_root) {
        auto node_release_code = node_root->FirstChildElement("release_code");
        if (node_release_code) {
            return node_release_code->GetText();
        }
    }

    // failure
    log_warning("options", "no release_code found in {}", bootstrap_path);
    return "";
}

static launcher::GameVersion detect_gameversion_ident(const std::string& bootstrap_user) {

    // detect ea3-ident path
    std::string ident_path;
    if (fileutils::file_exists("prop/ea3-ident.xml")) {
        ident_path = "prop/ea3-ident.xml";
    }
    if (ident_path.empty()) {
        log_warning("options", "unable to detect ea3-ident.xml file");
        return launcher::GameVersion();
    }

    // load XML
    tinyxml2::XMLDocument ea3_ident;
    if (ea3_ident.LoadFileA(ident_path.c_str()) != tinyxml2::XML_SUCCESS) {
        log_warning("options", "unable to parse {}", ident_path);
        return launcher::GameVersion();
    }

    // find model string
    auto node_root = ea3_ident.LastChild();
    if (node_root) {
        auto node_soft = node_root->FirstChildElement("soft");
        if (node_soft) {
            launcher::GameVersion version;
            bool error = true;
            auto node_model = node_soft->FirstChildElement("model");
            if (node_model) {
                version.model = node_model->GetText();
                error = false;
            }
            auto node_dest = node_soft->FirstChildElement("dest");
            if (node_dest) {
                version.dest = node_dest->GetText();
            }
            auto node_spec = node_soft->FirstChildElement("spec");
            if (node_spec) {
                version.spec = node_spec->GetText();
            }
            auto node_rev = node_soft->FirstChildElement("rev");
            if (node_rev) {
                version.rev = node_rev->GetText();
            }
            auto node_ext = node_soft->FirstChildElement("ext");
            if (node_ext) {
                version.ext = node_ext->GetText();
                auto bootstrap_ext = launcher::detect_bootstrap_release_code(bootstrap_user);
                if (version.ext.size() != 10 && bootstrap_ext.size() == 10) {
                    version.ext = bootstrap_ext;
                } else if (bootstrap_ext.size() == 10) {
                    int ext_cur = 0;
                    int ext_new = 0;
                    try {
                        ext_cur = std::stoi(version.ext);
                        try {
                            ext_new = std::stoi(bootstrap_ext);
                            if (ext_new > ext_cur) {
                                version.ext = bootstrap_ext;
                            }
                        } catch (const std::exception &ex) {
                            log_warning("options", "unable to parse soft/ext: {}", bootstrap_ext);
                        }
                    } catch (const std::exception &ex) {
                        log_warning("options", "unable to parse soft/ext: {}", version.ext);
                        version.ext = bootstrap_ext;
                    }
                }
            }
            if (!error) {
                log_info("options", "using model {}:{}:{}:{}:{} from {}",
                         version.model, version.dest, version.spec, version.rev, version.ext,
                         ident_path);
                return version;
            }
        }
    }

    // error
    log_warning("options", "unable to find /ea3_conf/soft/model in {}", ident_path);
    return launcher::GameVersion();
}

launcher::GameVersion launcher::detect_gameversion(const std::string &ea3_user, const std::string& bootstrap_user) {
    // detect ea3-config path
    std::string ea3_path;
    if (!ea3_user.empty()) {
        ea3_path = ea3_user;
    } else if (fileutils::file_exists("prop/ea3-config.xml")) {
        ea3_path = "prop/ea3-config.xml";
    } else if (fileutils::file_exists("prop/ea3-cfg.xml")) {
        ea3_path = "prop/ea3-cfg.xml";
    } else if (fileutils::file_exists("prop/eamuse-config.xml")) {
        ea3_path = "prop/eamuse-config.xml";
    }
    if (ea3_path.empty()) {
        log_warning("options", "unable to detect EA3 configuration file");
        return detect_gameversion_ident(bootstrap_user);
    }

    // load XML
    tinyxml2::XMLDocument ea3_config;
    if (ea3_config.LoadFileA(ea3_path.c_str()) != tinyxml2::XML_SUCCESS) {
        log_warning("options", "unable to parse {}", ea3_path);
        log_warning("options", "trying again with improper XML header removed...");

        // below: dirty hack to deal with bad ea3 xml file with comments in the beginning
        // https://stackoverflow.com/questions/19100408/tinyxml-any-way-to-skip-problematic-doctype-tag

        // Open the file and read it into a vector
        std::ifstream ifs(ea3_path.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
        std::ifstream::pos_type fsize = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        std::vector<char> bytes(fsize);
        ifs.read(&bytes[0], fsize);

        // Create string from vector
        std::string xml_str(&bytes[0], fsize);

        // Skip unsupported statements
        size_t pos = 0;
        while (true) {
            pos = xml_str.find_first_of("<", pos);
            if (xml_str[pos + 1] == '?' || // <?xml...
                xml_str[pos + 1] == '!') { // <!DOCTYPE... or [<!ENTITY...
                // Skip this line
                pos = xml_str.find_first_of("\n", pos);
            } else
                break;
        }
        xml_str = xml_str.substr(pos);

        // replace with fixed one
        ea3_config.Clear();
        if (ea3_config.Parse(xml_str.c_str()) != tinyxml2::XML_SUCCESS) {
            // if we still failed, give up
            log_warning("options", "unable to parse fixed xml");
            return detect_gameversion_ident(bootstrap_user);
        }
    }

    // find model string
    auto node_root = ea3_config.LastChild();
    if (node_root) {
        auto node_soft = node_root->FirstChildElement("soft");
        if (node_soft) {
            GameVersion version;
            bool error = true;
            auto node_model = node_soft->FirstChildElement("model");
            if (node_model) {
                version.model = node_model->GetText();
                error = false;
            }
            auto node_dest = node_soft->FirstChildElement("dest");
            if (node_dest) {
                version.dest = node_dest->GetText();
            }
            auto node_spec = node_soft->FirstChildElement("spec");
            if (node_spec) {
                version.spec = node_spec->GetText();
            }
            auto node_rev = node_soft->FirstChildElement("rev");
            if (node_rev) {
                version.rev = node_rev->GetText();
            }
            auto node_ext = node_soft->FirstChildElement("ext");
            if (node_ext) {
                version.ext = node_ext->GetText();
                auto bootstrap_ext = launcher::detect_bootstrap_release_code(bootstrap_user);
                if (version.ext.size() != 10 && bootstrap_ext.size() == 10) {
                    version.ext = bootstrap_ext;
                } else if (bootstrap_ext.size() == 10) {
                    int ext_cur = 0;
                    int ext_new = 0;
                    try {
                        ext_cur = std::stoi(version.ext);
                        try {
                            ext_new = std::stoi(bootstrap_ext);
                            if (ext_new > ext_cur) {
                                version.ext = bootstrap_ext;
                            }
                        } catch (const std::exception &ex) {
                            log_warning("options", "unable to parse soft/ext: {}", bootstrap_ext);
                        }
                    } catch (const std::exception &ex) {
                        log_warning("options", "unable to parse soft/ext: {}", version.ext);
                        version.ext = bootstrap_ext;
                    }
                }
            }
            if (!error) {
                log_info("options", "using model {}:{}:{}:{}:{} from {}",
                         version.model, version.dest, version.spec, version.rev, version.ext,
                         ea3_path);
                return version;
            }
        }
    }

    // error
    log_warning("options", "unable to find /ea3/soft/model in {}", ea3_path);
    return detect_gameversion_ident(bootstrap_user);
}
