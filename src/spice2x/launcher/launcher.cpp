#include <condition_variable>
#include <iostream>
#include <memory>
#include <vector>

#include <shlwapi.h>
#include <cfg/configurator.h>

#include "acio/acio.h"
#include "acio/icca/icca.h"
#include "api/controller.h"
#include "avs/automap.h"
#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
#include "build/defs.h"
#include "cfg/spicecfg.h"
#include "cfg/config.h"
#include "cfg/screen_resize.h"
#include "easrv/easrv.h"
#include "external/cardio/cardio_runner.h"
#include "external/scard/scard.h"
#include "games/game.h"
#include "games/io.h"
#include "games/bbc/bbc.h"
#include "games/bs/bs.h"
#include "games/ddr/ddr.h"
#include "games/dea/dea.h"
#include "games/drs/drs.h"
#include "games/gitadora/gitadora.h"
#include "games/hpm/hpm.h"
#include "games/iidx/iidx.h"
#ifdef SPICE64
#include "games/iidx/camera.h"
#endif
#include "games/iidx/poke.h"
#include "games/jb/jb.h"
#include "games/mga/mga.h"
#include "games/nost/nost.h"
#include "games/nost/poke.h"
#include "games/popn/popn.h"
#include "games/qma/qma.h"
#include "games/rb/rb.h"
#include "games/rf3d/rf3d.h"
#include "games/sc/sc.h"
#include "games/scotto/scotto.h"
#include "games/sdvx/sdvx.h"
#include "games/shared/printer.h"
#include "games/silentscope/silentscope.h"
#include "games/mfc/mfc.h"
#include "games/ftt/ftt.h"
#include "games/loveplus/loveplus.h"
#include "games/we/we.h"
#include "games/otoca/otoca.h"
#include "games/shogikai/shogikai.h"
#include "games/pcm/pcm.h"
#include "games/onpara/onpara.h"
#include "games/bc/bc.h"
#include "games/ccj/ccj.h"
#include "games/ccj/trackball.h"
#include "games/qks/qks.h"
#include "games/mfg/mfg.h"
#include "games/museca/museca.h"
#include "hooks/avshook.h"
#include "hooks/audio/audio.h"
#include "hooks/debughook.h"
#include "hooks/devicehook.h"
#include "hooks/graphics/nvenc_hook.h"
#include "hooks/input/dinput8/hook.h"
#include "hooks/graphics/graphics.h"
#include "hooks/lang.h"
#include "hooks/networkhook.h"
#include "hooks/unisintrhook.h"
#include "launcher/launcher.h"
#include "launcher/logger.h"
#include "launcher/signal.h"
#include "launcher/superexit.h"
#include "launcher/richpresence.h"
#include "launcher/shutdown.h"
#include "launcher/options.h"
#include "misc/bt5api.h"
#include "misc/device.h"
#include "misc/eamuse.h"
#include "misc/extdev.h"
#include "misc/sciunit.h"
#include "misc/sde.h"
#include "misc/wintouchemu.h"
#include "overlay/overlay.h"
#include "overlay/windows/patch_manager.h"
#include "overlay/windows/iidx_seg.h"
#include "rawinput/rawinput.h"
#include "rawinput/touch.h"
#include "reader/reader.h"
#include "stubs/stubs.h"
#include "touch/touch.h"
#include "util/cpuutils.h"
#include "util/crypt.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/peb.h"
#include "util/sysutils.h"
#include "util/tapeled.h"
#include "util/time.h"
#include "util/utils.h"
#include "avs/ssl.h"
#include "nvapi/nvapi.h"
#include "hooks/graphics/nvapi_hook.h"

// std::max
#ifdef max
#undef max
#endif

// constants
static const char *STUBS[] = {"kbt.dll", "kld.dll"};

// general settings
static std::vector<std::string> game_hooks;
static std::vector<std::string> early_hooks;
std::filesystem::path MODULE_PATH;
HANDLE LOG_FILE = INVALID_HANDLE_VALUE;
std::string LOG_FILE_PATH = "";
int LAUNCHER_ARGC = 0;
char **LAUNCHER_ARGV = nullptr;
std::unique_ptr<std::vector<Option>> LAUNCHER_OPTIONS;
std::string CARD_OVERRIDES[2];

// sub-systems
std::unique_ptr<api::Controller> API_CONTROLLER;
std::unique_ptr<rawinput::RawInputManager> RI_MGR;

// trigger NVIDIA Optimus & AMD Enduro High Performance Graphics
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static bool CHECK_DLL_IGNORE_ARCH = false;
static bool check_dll(const std::string &model) {
    if (cfg::CONFIGURATOR_STANDALONE || CHECK_DLL_IGNORE_ARCH) {
        return fileutils::file_exists(MODULE_PATH / model);
    } else {
        return fileutils::verify_header_pe(MODULE_PATH / model);
    }
}

void update_msvcrt_args(int argc, char *argv[]);

int main_implementation(int argc, char *argv[]) {

    // remember argv, argv
    LAUNCHER_ARGC = argc;
    LAUNCHER_ARGV = argv;

    // register exception handler and control handler
    launcher::signal::init();

    // start logger
    logger::start();

    // get module path
    MODULE_PATH = libutils::module_file_name(nullptr).parent_path();

    // initialize crypt
    crypt::init();

    // initialize timer
    init_performance_counter();

    // api settings
    bool api_enable = false;
    bool api_pretty = false;
    bool api_debug = false;
    unsigned short api_port = 1337;
    std::string api_pass = "";
    std::vector<std::string> api_serial_port;
    std::vector<DWORD> api_serial_baud;

    // attach settings
    bool attach_io = false;
    bool attach_acio = false;
    bool attach_icca = false;
    bool attach_device = false;
    bool attach_extdev = false;
    bool attach_sciunit = false;
    bool attach_cpusbxpkm_printer = false;
    bool attach_iidx = false;
    bool attach_sdvx = false;
    bool attach_jb = false;
    bool attach_rb = false;
    bool attach_shogikai = false;
    bool attach_mga = false;
    bool attach_sc = false;
    bool attach_popn = false;
    bool attach_ddr = false;
    bool attach_gitadora = false;
    bool attach_nostalgia = false;
    bool attach_bbc = false;
    bool attach_hpm = false;
    bool attach_qma = false;
    bool attach_dea = false;
    bool attach_mfc = false;
    bool attach_ftt = false;
    bool attach_bs = false;
    bool attach_loveplus = false;
    bool attach_scotto = false;
    bool attach_rf3d = false;
    bool attach_drs = false;
    bool attach_we = false;
    bool attach_otoca = false;
    bool attach_silentscope = false;
    bool attach_pcm = false;
    bool attach_onpara = false;
    bool attach_bc = false;
    bool attach_ccj = false;
    bool attach_qks = false;
    bool attach_mfg = false;
    bool attach_museca = false;

    // misc settings
    size_t user_heap_size = 0;
    unsigned short easrv_port = 0;
    bool easrv_maint = true;
    bool easrv_smart = false;
    bool load_stubs = false;
    bool netfix_disable = false;
    bool lang_disable = false;
    std::string process_priority_str = "high";
    bool cardio_enabled = false;
    bool peb_print = false;
    bool cfg_run = false;
    bool rich_presence = false;
    bool automap = false;
    bool ssl_disable = false;
    bool dump_sysinfo = true;
    std::vector<std::string> sextet_devices;
    std::optional<rawinput::MidiNoteAlgorithm> midi_algo;

    // parse arguments
    LAUNCHER_OPTIONS = launcher::parse_options(argc, argv);

    // determine config file path - must be done before anything else
    const auto &cfg_path = LAUNCHER_OPTIONS->at(launcher::Options::ConfigurationPath);
    if (cfg_path.is_active()) {
        CONFIG_PATH_OVERRIDE = cfg_path.value_text();
    }

    // detect model used to load option overrides
    auto options_version = launcher::detect_gameversion(
            LAUNCHER_OPTIONS->at(launcher::Options::PathToEa3Config).value
    );
    if (!options_version.model.empty() && options_version.model.size() < 4) {
        if (options_version.dest.size() == 1) {
            avs::game::DEST[0] = options_version.dest[0];
        }
        if (options_version.spec.size() == 1) {
            avs::game::SPEC[0] = options_version.spec[0];
        }
        if (options_version.rev.size() == 1) {
            avs::game::REV[0] = options_version.rev[0];
        }
        if (options_version.ext.size() == 10) {
            strcpy(avs::game::EXT, options_version.ext.c_str());
        }
        strcpy(avs::game::MODEL, options_version.model.c_str());
        eamuse_autodetect_game();
    }

    // grab merged game options
    auto options_ptr = games::get_options(eamuse_get_game());
    if (!options_ptr) {
        options_ptr = LAUNCHER_OPTIONS.get();
    }
    auto &options = *options_ptr;

    // check options
    // TODO: get rid of some booleans here and make use of the options directly
    if (options[launcher::Options::OpenConfigurator].value_bool()) {
        CHECK_DLL_IGNORE_ARCH = true;
        cfg::CONFIGURATOR_TYPE = cfg::ConfigType::Config;
        cfg_run = true;
    }
    if (options[launcher::Options::EAmusementEmulation].value_bool() &&
        options[launcher::Options::ServiceURL].is_active() &&
        !cfg::CONFIGURATOR_STANDALONE) {
        log_fatal(
            "launcher",
            "BAD EAMUSE SETTINGS ERROR\n\n\n"
            "-------------------------------------------------------------------\n"
            "WARNING - WARNING - WARNING - WARNING - WARNING - WARNING - WARNING\n"
            "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
            "A service URL is set **AND** E-Amusement emulation is enabled.\n"
            "Either remove the service URL, or disable E-Amusement emulation.\n"
            "Otherwise you may experience problems logging in.\n"
            "-------------------------------------------------------------------\n\n\n"
            );
    }
    if (options[launcher::Options::EAmusementEmulation].value_bool()) {
        avs::ea3::URL_SLASH = 1;
        easrv_port = 8080;
    }
    if (options[launcher::Options::SmartEAmusement].value_bool()) {
        easrv_smart = true;
    }
    if (options[launcher::Options::EAmusementMaintenance].is_active()) {
        easrv_maint = options[launcher::Options::EAmusementMaintenance].value_uint32() > 0;
    }
    if (options[launcher::Options::spice2x_EAmusementMaintenance].value_bool()) {
        easrv_maint = true;
    }
    if (options[launcher::Options::WindowedMode].value_bool()) {
        GRAPHICS_WINDOWED = true;
    }
    if (options[launcher::Options::GraphicsForceRefresh].is_active()) {
        GRAPHICS_FORCE_REFRESH = options[launcher::Options::GraphicsForceRefresh].value_uint32();
    }
    if (options[launcher::Options::GraphicsForceSingleAdapter].value_bool()) {
        GRAPHICS_FORCE_SINGLE_ADAPTER = true;
    }
    if (options[launcher::Options::ForceBackBufferCount].is_active()) {
        const auto n = options[launcher::Options::ForceBackBufferCount].value_uint32();
        if (n == 2 || n == 3) {
            // user provided 2 => double buffering => 1 back buffer
            // user provided 3 => triple buffering => 2 back buffers
            GRAPHICS_FORCE_VSYNC_BUFFER = n - 1;
        } else {
            log_warning("graphics", "invalid parameter specified for -vsyncbuffer: {}", n);
        }
    }
    if (options[launcher::Options::spice2x_IIDXNoSub].value_bool()) {
        GRAPHICS_FORCE_SINGLE_ADAPTER = true;
    }
    if (options[launcher::Options::spice2x_SDVXNoSub].value_bool()) {
        GRAPHICS_FORCE_SINGLE_ADAPTER = true;
        GRAPHICS_PREVENT_SECONDARY_WINDOW = true;
    }
    if (options[launcher::Options::DisplayAdapter].is_active()) {
        D3D9_ADAPTER = options[launcher::Options::DisplayAdapter].value_uint32();
    }
    if (options[launcher::Options::CaptureCursor].value_bool()) {
        GRAPHICS_CAPTURE_CURSOR = true;
    }
    if (options[launcher::Options::ShowCursor].value_bool()) {
        GRAPHICS_SHOW_CURSOR = true;
    }
    if (options[launcher::Options::VerboseGraphicsLogging].value_bool()) {
        GRAPHICS_LOG_HRESULT = true;
    }
    if (options[launcher::Options::VerboseAVSLogging].value_bool()) {
        hooks::avs::config::LOG = true;
    }
    if (options[launcher::Options::spice2x_AutoOrientation].is_active()) {
        GRAPHICS_ADJUST_ORIENTATION =
            (graphics_orientation)options[launcher::Options::spice2x_AutoOrientation].value_uint32();
    } else if (options[launcher::Options::AdjustOrientation].value_bool()) {
        GRAPHICS_ADJUST_ORIENTATION = ORIENTATION_CW;
    }
    if (options[launcher::Options::spice2x_NoD3D9DeviceHook].value_bool()) {
        D3D9_DEVICE_HOOK_DISABLE = true;
        // touch emulation gets disabled, might as well turn these on
        games::iidx::NATIVE_TOUCH = true;
        games::sdvx::NATIVETOUCH = true;
        // not strictly necessary as it will fail to init anyway, but cleaner to just disable it now
        overlay::ENABLED = false;
        // leaving these on without dx9hooks result in torn state and therefore failure to draw
        GRAPHICS_FORCE_SINGLE_ADAPTER = false;
    }

    if (options[launcher::Options::FullscreenResolution].is_active()) {
        std::pair<uint32_t, uint32_t> result;
        if (parse_width_height(options[launcher::Options::FullscreenResolution].value_text(), result)) {
            GRAPHICS_FS_CUSTOM_RESOLUTION = result;
        } else {
            log_warning("launcher", "failed to parse -forceres");
        }
    }

    if (options[launcher::Options::spice2x_NvapiProfile].value_bool() && !cfg::CONFIGURATOR_STANDALONE) {
        nvapi::ADD_PROFILE = true;
    }
    if (options[launcher::Options::spice2x_NoNVAPI].value_bool()) {
        nvapi_hook::BYPASS_NVAPI = true;
    }

    if (options[launcher::Options::LogLevel].is_active()) {
        avs::core::LOG_LEVEL_CUSTOM = options[launcher::Options::LogLevel].value_text();
    }
    for (auto &hook : options[launcher::Options::InjectHook].values_text()) {
        std::string buffer;
        std::stringstream stream(hook);
        std::vector<std::string> tokens;
        while (stream >> buffer) {
            game_hooks.push_back(buffer);
        }
    }
    for (auto &hook : options[launcher::Options::EarlyInjectHook].values_text()) {
        std::string buffer;
        std::stringstream stream(hook);
        std::vector<std::string> tokens;
        while (stream >> buffer) {
            early_hooks.push_back(buffer);
        }
    }
    if (options[launcher::Options::LoadStubs].value_bool()) {
        load_stubs = true;
    }
    if (options[launcher::Options::EnableAllIOModules].value_bool()) {
        attach_io = true;
    }
    if (options[launcher::Options::EnableACIOModule].value_bool()) {
        attach_acio = true;
    }
    if (options[launcher::Options::EnableICCAModule].value_bool()) {
        attach_icca = true;
    }
    if (options[launcher::Options::EnableDEVICEModule].value_bool()) {
        attach_device = true;
    }
    if (options[launcher::Options::EnableEXTDEVModule].value_bool()) {
        attach_extdev = true;
    }
    if (options[launcher::Options::EnableSCIUNITModule].value_bool()) {
        attach_sciunit = true;
    }
    if (options[launcher::Options::EnableDevicePassthrough].value_bool()) {
        hooks::device::ENABLE = false;
    }
    if (options[launcher::Options::LoadSoundVoltexModule].value_bool()) {
        attach_sdvx = true;
    }
    if (options[launcher::Options::SDVXDisableCameras].value_bool()) {
        games::sdvx::DISABLECAMS = true;
    }
    if (options[launcher::Options::SDVXNativeTouch].value_bool()) {
        games::sdvx::NATIVETOUCH = true;
    }
    if (options[launcher::Options::spice2x_SDVXDigitalKnobSensitivity].is_active()) {
        games::sdvx::DIGITAL_KNOB_SENS = (uint8_t)
            options[launcher::Options::spice2x_SDVXDigitalKnobSensitivity].value_uint32();
    }
    if (options[launcher::Options::spice2x_SDVXAsioDriver].is_active()) {
        games::sdvx::ASIO_DRIVER = options[launcher::Options::spice2x_SDVXAsioDriver].value_text();
    }
    if (options[launcher::Options::spice2x_SDVXSubPos].is_active()) {
        auto txt = options[launcher::Options::spice2x_SDVXSubPos].value_text();
        if (txt == "top") {
            games::sdvx::OVERLAY_POS = games::sdvx::SDVX_OVERLAY_TOP;
        } else if (txt == "center") {
            games::sdvx::OVERLAY_POS = games::sdvx::SDVX_OVERLAY_MIDDLE;
        }
    }
    if (options[launcher::Options::spice2x_SDVXSubRedraw].value_bool()) {
        SUBSCREEN_FORCE_REDRAW = true;
    }
    if (options[launcher::Options::LoadIIDXModule].value_bool()) {
        attach_iidx = true;
    }
    if (options[launcher::Options::IIDXCameraOrderFlip].value_bool()) {
        games::iidx::FLIP_CAMS = true;
    }
    if (options[launcher::Options::IIDXDisableCameras].value_bool()) {
        games::iidx::DISABLE_CAMS = true;
    }
    if (options[launcher::Options::IIDXCamHook].value_bool()) {
        games::iidx::TDJ_CAMERA = true;
        // Disable legacy behaviour to avoid conflict
        games::iidx::DISABLE_CAMS = true;
    }
    if (options[launcher::Options::IIDXCamHookOverride].is_active()) {
        games::iidx::TDJ_CAMERA_OVERRIDE = options[launcher::Options::IIDXCamHookOverride].value_text();
    }
    if (options[launcher::Options::IIDXCamHookRatio].is_active() &&
        options[launcher::Options::IIDXCamHookRatio].value_text() == "169") {
        games::iidx::TDJ_CAMERA_PREFER_16_9 = true;
    }
    if (options[launcher::Options::IIDXSoundOutputDevice].is_active()) {
        games::iidx::SOUND_OUTPUT_DEVICE = options[launcher::Options::IIDXSoundOutputDevice].value_text();
    }
    if (options[launcher::Options::IIDXAsioDriver].is_active()) {
        games::iidx::ASIO_DRIVER = options[launcher::Options::IIDXAsioDriver].value_text();
    }
    if (options[launcher::Options::IIDXTDJMode].value_bool()) {
        games::iidx::TDJ_MODE = true;
    }
    if (options[launcher::Options::spice2x_IIDXDigitalTTSensitivity].is_active()) {
        games::iidx::DIGITAL_TT_SENS = (uint8_t)
            options[launcher::Options::spice2x_IIDXDigitalTTSensitivity].value_uint32();
    }
    if (options[launcher::Options::spice2x_IIDXLDJForce720p].value_bool()) {
        games::iidx::FORCE_720P = true;
    }
    if (options[launcher::Options::spice2x_IIDXTDJSubSize].is_active()) {
        games::iidx::SUBSCREEN_OVERLAY_SIZE =
            options[launcher::Options::spice2x_IIDXTDJSubSize].value_text();
    }
    if (options[launcher::Options::spice2x_IIDXLEDFontSize].is_active()) {
        overlay::windows::IIDX_SEGMENT_FONT_SIZE =
            options[launcher::Options::spice2x_IIDXLEDFontSize].value_uint32();
    }
    if (options[launcher::Options::spice2x_IIDXLEDColor].is_active()) {
        overlay::windows::IIDX_SEGMENT_FONT_COLOR =
            options[launcher::Options::spice2x_IIDXLEDColor].value_hex64();
    }
    if (options[launcher::Options::spice2x_IIDXLEDPos].is_active()) {
        overlay::windows::IIDX_SEGMENT_LOCATION =
            options[launcher::Options::spice2x_IIDXLEDPos].value_text();
    }
    if (options[launcher::Options::spice2x_IIDXNoESpec].value_bool()) {
        games::iidx::DISABLE_ESPEC_IO = true;
    }
    if (options[launcher::Options::spice2x_IIDXNativeTouch].value_bool()) {
        games::iidx::NATIVE_TOUCH = true;
    }
    // should come later since this will override a few settings
    if (options[launcher::Options::spice2x_IIDXWindowedTDJ].value_bool() ||
        (options[launcher::Options::IIDXTDJMode].value_bool() && GRAPHICS_WINDOWED)) {
        games::iidx::TDJ_MODE = true;
        GRAPHICS_WINDOWED = true;
        games::iidx::SCREEN_MODE = "2";
    }
#ifdef SPICE64
    if (options[launcher::Options::IIDXRecQuality].is_active()) {
        nvenc_hook::VIDEO_CQP_STRING_OVERRIDE = options[launcher::Options::IIDXRecQuality].value_text();
    }
#endif
    if (options[launcher::Options::LoadJubeatModule].value_bool()) {
        attach_jb = true;
    }
    if (options[launcher::Options::LoadBeatstreamModule].value_bool()) {
        attach_bs = true;
    }
    if (options[launcher::Options::LoadReflecBeatModule].value_bool()) {
        attach_rb = true;
    }
    if (options[launcher::Options::LoadShogikaiModule].value_bool()) {
        attach_shogikai = true;
    }
    if (options[launcher::Options::LoadPopnMusicModule].value_bool()) {
        attach_popn = true;
    }
    if (options[launcher::Options::PopnMusicForceHDMode].value_bool()) {
        avs::ea3::PCB_TYPE = 1;
    }
    if (options[launcher::Options::PopnMusicForceSDMode].value_bool()) {
        avs::ea3::PCB_TYPE = 0;
    }
    if (options[launcher::Options::LoadMetalGearArcadeModule].value_bool()) {
        attach_mga = true;
    }
    if (options[launcher::Options::LoadGitaDoraModule].value_bool()) {
        attach_gitadora = true;
    }
    if (options[launcher::Options::GitaDoraCabinetType].is_active()) {
        games::gitadora::CAB_TYPE = options[launcher::Options::GitaDoraCabinetType].value_uint32();
    }
    if (options[launcher::Options::LoadNostalgiaModule].value_bool()) {
        attach_nostalgia = true;
    }
    if (options[launcher::Options::LoadBBCModule].value_bool()) {
        attach_bbc = true;
    }
    if (options[launcher::Options::LoadHelloPopnMusicModule].value_bool()) {
        attach_hpm = true;
    }
    if (options[launcher::Options::LoadQuizMagicAcademyModule].value_bool()) {
        attach_qma = true;
    }
    if (options[launcher::Options::LoadDanceEvolutionModule].value_bool()) {
        attach_dea = true;
    }
    if (options[launcher::Options::LoadLovePlusModule].value_bool()) {
        attach_loveplus = true;
    }
    if (options[launcher::Options::GitaDoraTwoChannelAudio].value_bool()) {
        games::gitadora::TWOCHANNEL = true;
    }
    if (options[launcher::Options::LoadDDRModule].value_bool()) {
        attach_ddr = true;
    }
    if (options[launcher::Options::LoadMahjongFightClubModule].value_bool()) {
        attach_mfc = true;
    }
    if (options[launcher::Options::LoadFutureTomTomModule].value_bool()) {
        attach_ftt = true;
    }
    if (options[launcher::Options::LoadScottoModule].value_bool()) {
        attach_scotto = true;
    }
    if (options[launcher::Options::LoadRoadFighters3DModule].value_bool()) {
        attach_rf3d = true;
    }
    if (options[launcher::Options::LoadDanceRushModule].value_bool()) {
        attach_drs = true;
    }
    if (options[launcher::Options::LoadWinningElevenModule].value_bool()) {
        attach_we = true;
    }
    if (options[launcher::Options::LoadOtocaModule].value_bool()) {
        attach_otoca = true;
    }
    if (options[launcher::Options::LoadChargeMachineModule].value_bool()) {
        attach_pcm = true;
    }
    if (options[launcher::Options::LoadOngakuParadiseModule].value_bool()) {
        attach_onpara = true;
    }
    if (options[launcher::Options::LoadBusouShinkiModule].value_bool()) {
        attach_bc = true;
    }
    if (options[launcher::Options::LoadCCJModule].value_bool()) {
        attach_ccj = true;
    }
    if (options[launcher::Options::LoadQKSModule].value_bool()) {
        attach_qks = true;
    }
    if (options[launcher::Options::LoadMFGModule].value_bool()) {
        attach_mfg = true;
    }
    if (options[launcher::Options::LoadMusecaModule].value_bool()) {
        attach_museca = true;
    }
    if (options[launcher::Options::DDR43Mode].value_bool()) {
        games::ddr::SDMODE = true;
    }
    if (options[launcher::Options::LoadSteelChronicleModule].value_bool()) {
        attach_sc = true;
    }
    if (options[launcher::Options::DisableNetworkFixes].value_bool()) {
        netfix_disable = true;
    }
    if (options[launcher::Options::DisableACPHook].value_bool()) {
        lang_disable = true;
    }
    if (options[launcher::Options::DisableSignalHandling].value_bool()) {
        launcher::signal::DISABLE = true;
    }
    if (options[launcher::Options::DebugCreateFile].value_bool()) {
        DEVICE_CREATEFILE_DEBUG = true;
    }
    if (options[launcher::Options::BlockingLogger].value_bool()) {
        logger::BLOCKING = true;
    }
    if (options[launcher::Options::OutputPEB].value_bool()) {
        peb_print = true;
    }
    if (options[launcher::Options::DumpSystemInfo].is_active()) {
        if (options[launcher::Options::DumpSystemInfo].value_text() == "all") {
            rawinput::DUMP_HID_DEVICES_TO_LOG = true;
        } else if (options[launcher::Options::DumpSystemInfo].value_text() == "basic") {
            dump_sysinfo = true;
        } else {
            dump_sysinfo = false;
        }
    }
    if (options[launcher::Options::EnableBemaniTools5API].value_bool()) {
        BT5API_ENABLED = true;
    }
    if (options[launcher::Options::CardIOHIDReaderSupport].value_bool()) {
        cardio_enabled = true;
    }
    if (options[launcher::Options::SDVXPrinterEmulation].value_bool()) {
        attach_cpusbxpkm_printer = true;
    }
    if (options[launcher::Options::SDVXPrinterOutputOverwrite].value_bool()) {
        games::shared::PRINTER_OVERWRITE_FILE = true;
    }
    for (auto &path : options[launcher::Options::SDVXPrinterOutputPath].values_text()) {
        games::shared::PRINTER_PATH.push_back(path);
    }
    for (auto &path : options[launcher::Options::SDVXPrinterOutputFormat].values_text()) {
        games::shared::PRINTER_FORMAT.push_back(path);
    }
    if (options[launcher::Options::SDVXPrinterJPGQuality].is_active()) {
        games::shared::PRINTER_JPG_QUALITY = options[launcher::Options::SDVXPrinterJPGQuality].value_uint32();
    }
    if (options[launcher::Options::SDVXPrinterOutputClear].value_bool()) {
        games::shared::PRINTER_CLEAR = true;
    }
    if (options[launcher::Options::HTTP11].is_active()) {
        avs::ea3::HTTP11 = options[launcher::Options::HTTP11].value_uint32();
    }
    if (options[launcher::Options::DisableSSL].value_bool()) {
        ssl_disable = true;
    }
    if (options[launcher::Options::URLSlash].is_active()) {
        avs::ea3::URL_SLASH = options[launcher::Options::URLSlash].value_uint32();
    }
    if (options[launcher::Options::spice2x_ProcessPriority].is_active()) {
        process_priority_str = options[launcher::Options::spice2x_ProcessPriority].value_text();
    } else if (options[launcher::Options::RealtimeProcessPriority].value_bool()) {
        process_priority_str = "realtime";
    }
    if (options[launcher::Options::HeapSize].is_active()) {
        user_heap_size = options[launcher::Options::HeapSize].value_uint32();
    }
    if (options[launcher::Options::DisableAvsVfsDriveMountRedirection].is_active()) {
        hooks::avs::config::DISABLE_VFS_DRIVE_REDIRECTION = true;
    }
    if (options[launcher::Options::ScreenResizeConfigPath].is_active()) {
        cfg::SCREEN_RESIZE_CFG_PATH_OVERRIDE =
            options[launcher::Options::ScreenResizeConfigPath].value_text();
    }
    if (options[launcher::Options::PatchManagerConfigPath].is_active()) {
        overlay::windows::PATCH_MANAGER_CFG_PATH_OVERRIDE =
            options[launcher::Options::PatchManagerConfigPath].value_text();
    }
    if (options[launcher::Options::PathToAppConfig].is_active()) {
        avs::ea3::APP_PATH = options[launcher::Options::PathToAppConfig].value_text();
    }
    if (options[launcher::Options::PathToAvsConfig].is_active()) {
        avs::core::CFG_PATH = options[launcher::Options::PathToAvsConfig].value_text();
    }
    if (options[launcher::Options::PathToEa3Config].is_active()) {
        avs::ea3::CFG_PATH = options[launcher::Options::PathToEa3Config].value_text();
    }
    if (options[launcher::Options::PathToBootstrap].is_active()) {
        avs::ea3::BOOTSTRAP_PATH = options[launcher::Options::PathToBootstrap].value_text();
    }
    if (options[launcher::Options::PathToLog].is_active()) {
        avs::core::LOG_PATH = options[launcher::Options::PathToLog].value_text();
    }
    if (options[launcher::Options::PCBID].is_active()) {
        avs::ea3::PCBID_CUSTOM = options[launcher::Options::PCBID].value_text();
    }
    if (options[launcher::Options::SOFTID].is_active()) {
        avs::ea3::SOFTID_CUSTOM = options[launcher::Options::SOFTID].value_text();
    }
    if (options[launcher::Options::ServiceURL].is_active()) {
        avs::ea3::URL_CUSTOM = options[launcher::Options::ServiceURL].value_text();
    }
    if (options[launcher::Options::PathToModules].is_active()) {
        std::error_code err;

        auto &path = options[launcher::Options::PathToModules].value_text();
        auto module_path = std::filesystem::absolute(path, err);

        if (err) {
            if (cfg::CONFIGURATOR_STANDALONE) {
                log_warning("launcher", "failed to resolve module path '{}': {}", path, err.message());
            } else {
                log_fatal("launcher", "failed to resolve module path '{}': {}", path, err.message());
            }
        }

        MODULE_PATH = std::move(module_path);
        SetDllDirectoryW(MODULE_PATH.c_str());
    }
    if (options[launcher::Options::Player1Card].is_active()) {
        CARD_OVERRIDES[0] = options[launcher::Options::Player1Card].value_text();
    }
    if (options[launcher::Options::Player2Card].is_active()) {
        CARD_OVERRIDES[1] = options[launcher::Options::Player2Card].value_text();
    }
    if (options[launcher::Options::Player1PinMacro].is_active()) {
        PIN_MACRO_ENABLED = true;
        PIN_MACRO_VALUES[0] = options[launcher::Options::Player1PinMacro].value_text();
    }
    if (options[launcher::Options::Player2PinMacro].is_active()) {
        PIN_MACRO_ENABLED = true;
        PIN_MACRO_VALUES[1] = options[launcher::Options::Player2PinMacro].value_text();
    }

    for (auto &reader : options[launcher::Options::ICCAReaderPort].values_text()) {
        static int reader_id = 0;
        if (reader_id < 2) {
            start_reader_thread(reader, reader_id++);
        } else {
            if (!cfg::CONFIGURATOR_STANDALONE) {
                log_fatal("launcher", "too many readers specified (maximum is 2)");
            }
            break;
        }
    }
    for (auto &sextet : options[launcher::Options::SextetStreamPort].values_text()) {
        sextet_devices.emplace_back(sextet);
    }
    if (options[launcher::Options::HIDSmartCard].value_bool()) {
        WINSCARD_CONFIG.cardinfo_callback = eamuse_scard_callback;
        scard_threadstart();
    }
    if (options[launcher::Options::HIDSmartCardOrderFlip].value_bool()) {
        WINSCARD_CONFIG.flip_order = true;
    }
    if (options[launcher::Options::HIDSmartCardOrderToggle].value_bool()) {
        WINSCARD_CONFIG.toggle_order = true;
    }
    if (options[launcher::Options::HIDSmartCardIdConvert].is_active()) {
        const auto text = options[launcher::Options::HIDSmartCardIdConvert].value_text();
        if (text == "fix") {
            WINSCARD_CONFIG.add_padding_to_old_cards = true;
        } else if (text == "all") {
            WINSCARD_CONFIG.add_padding_to_old_cards = true;
            WINSCARD_CONFIG.add_padding_to_felica = true;
        }
    }
    if (options[launcher::Options::CardIOHIDReaderOrderFlip].value_bool()) {
        CARDIO_RUNNER_FLIP = true;
    }
    if (options[launcher::Options::CardIOHIDReaderOrderToggle].value_bool()) {
        CARDIO_RUNNER_TOGGLE = true;
    }
    if (options[launcher::Options::ICCAReaderPortToggle].is_active()) {
        start_reader_thread(options[launcher::Options::ICCAReaderPortToggle].value_text(), -1);
    }
    if (options[launcher::Options::IntelSDEFolder].is_active()) {
        sde_init(options[launcher::Options::IntelSDEFolder].value_text());
    }
    if (options[launcher::Options::AdapterNetwork].is_active()) {
        NETWORK_ADDRESS = options[launcher::Options::AdapterNetwork].value_text();
    }
    if (options[launcher::Options::AdapterSubnet].is_active()) {
        NETWORK_SUBNET = options[launcher::Options::AdapterSubnet].value_text();
    }
    if (options[launcher::Options::APITCPPort].is_active()) {
        api_enable = true;
        api_port = options[launcher::Options::APITCPPort].value_uint32();
    }
    if (options[launcher::Options::APIPassword].is_active()) {
        api_pass = options[launcher::Options::APIPassword].value_text();
    }
    if (options[launcher::Options::APISerialPort].is_active()) {
        api_serial_port.push_back(options[launcher::Options::APISerialPort].value_text());
    }
    if (options[launcher::Options::APISerialBaud].is_active()) {
        api_serial_baud.push_back(options[launcher::Options::APISerialBaud].value_uint32());
    }
    if (options[launcher::Options::APIPretty].value_bool()) {
        api_pretty = true;
    }
    if (options[launcher::Options::APIVerboseLogging].value_bool()) {
        api::LOGGING = true;
    }
    if (options[launcher::Options::APIDebugMode].value_bool()) {
        api_debug = true;
    }
    if (options[launcher::Options::DisableDebugHooks].value_bool()) {
        debughook::DEBUGHOOK_LOGGING = false;
    }
    if (options[launcher::Options::ForceWinTouch].value_bool()) {
        rawinput::touch::DISABLED = true;
    }
    if (options[launcher::Options::SDVXForce720p].value_bool()) {
        GRAPHICS_SDVX_FORCE_720 = true;
    }
    if (options[launcher::Options::InvertTouchCoordinates].value_bool()) {
        rawinput::touch::INVERTED = true;
    }
    // DisableTouchCardInsert is no longer honored in spice2x
    // if (options[launcher::Options::DisableTouchCardInsert].value_bool()) {
    //     SPICETOUCH_CARD_DISABLE = true;
    // }
    if (options[launcher::Options::spice2x_TouchCardInsert].value_bool()) {
        SPICETOUCH_CARD_DISABLE = false;
    }
    if (options[launcher::Options::ForceTouchEmulation].value_bool()) {
        wintouchemu::FORCE = true;
    }
    if (options[launcher::Options::Graphics9On12].value_bool()) {
        GRAPHICS_9_ON_12_STATE = DX9ON12_FORCE_ON;
    }
    if (options[launcher::Options::spice2x_Dx9On12].is_active()) {
        auto &name = options[launcher::Options::spice2x_Dx9On12].value_text();
        if (name == "0") {
            GRAPHICS_9_ON_12_STATE = DX9ON12_FORCE_OFF;
        } else if (name == "1") {
            GRAPHICS_9_ON_12_STATE = DX9ON12_FORCE_ON;
        }
        // do not explicitly set GRAPHICS_9_ON_12_STATE to default here; must respect
        // legacy Graphics9On12 option from above if set
    }
    if (options[launcher::Options::NoLegacy].value_bool() && !cfg::CONFIGURATOR_STANDALONE) {
        rawinput::NOLEGACY = true;
    }
    if (options[launcher::Options::RichPresence].value_bool()) {
        rich_presence = true;
    }
    if (options[launcher::Options::DiscordAppID].is_active()) {
        richpresence::discord::APPID_OVERRIDE = options[launcher::Options::DiscordAppID].value_text();
    }
    if (options[launcher::Options::ScreenshotFolder].is_active()) {
        GRAPHICS_SCREENSHOT_DIR = options[launcher::Options::ScreenshotFolder].value_text();
    }
    if (options[launcher::Options::DisableColoredOutput].value_bool()) {
        logger::COLOR = false;
    }
    if (options[launcher::Options::DisableOverlay].value_bool()) {
        overlay::ENABLED = false;
    }
    if (options[launcher::Options::DisableAudioHooks].value_bool()) {
        hooks::audio::ENABLED = false;
    }
    if (options[launcher::Options::spice2x_DisableVolumeHook].value_bool()) {
        hooks::audio::VOLUME_HOOK_ENABLED = false;
    }
    if (options[launcher::Options::AudioBackend].is_active()) {
        auto &name = options[launcher::Options::AudioBackend].value_text();
        auto backend = hooks::audio::name_to_backend(name.c_str());
        if (!backend.has_value() && !cfg::CONFIGURATOR_STANDALONE) {
            log_fatal("launcher", "invalid audio backend: {}", name);
        }

        hooks::audio::BACKEND = backend;
    }
    if (options[launcher::Options::AsioDriverId].is_active()) {
        hooks::audio::ASIO_DRIVER_ID = options[launcher::Options::AsioDriverId].value_uint32();
    }
    if (options[launcher::Options::AudioDummy].value_bool()) {
        hooks::audio::USE_DUMMY = true;
    }
    if (options[launcher::Options::EAAutomap].value_bool()) {
        easrv_maint = false;
        automap = true;
        avs::automap::ENABLED = true;
        avs::automap::PATCH = true;
        avs::automap::RESTRICT_NETWORK = true;
        avs::automap::DUMP = true;
    }
    if (options[launcher::Options::EANetdump].value_bool()) {
        easrv_maint = false;
        automap = true;
        avs::automap::ENABLED = true;
        avs::automap::PATCH = false;
        avs::automap::RESTRICT_NETWORK = true;
        avs::automap::DUMP = true;
    }
    if (options[launcher::Options::GameExecutable].is_active()) {
        avs::game::DLL_NAME = options[launcher::Options::GameExecutable].value_text();
    }
    if (options[launcher::Options::spice2x_LightsOverallBrightness].is_active()) {
        rawinput::HID_LIGHT_BRIGHTNESS =
            (uint8_t)options[launcher::Options::spice2x_LightsOverallBrightness].value_uint32();
    }
    if (options[launcher::Options::spice2x_FpsAutoShow].value_bool()) {
        overlay::AUTO_SHOW_FPS = true;
    }
    if (options[launcher::Options::spice2x_FpsOpposite].value_bool()) {
        overlay::FPS_SHOULD_FLIP = true;
    }
    if (options[launcher::Options::spice2x_SubScreenAutoShow].value_bool()) {
        overlay::AUTO_SHOW_SUBSCREEN = true;
    }
    if (options[launcher::Options::spice2x_IOPanelAutoShow].value_bool()) {
        overlay::AUTO_SHOW_IOPANEL = true;
    }
    if (options[launcher::Options::spice2x_KeypadAutoShow].is_active()) {
        auto s = options[launcher::Options::spice2x_KeypadAutoShow].value_uint32();
        switch (s) {
            case 1:
                overlay::AUTO_SHOW_KEYPAD_P1 = true;
                break;
            case 2:
                overlay::AUTO_SHOW_KEYPAD_P2 = true;
                break;
            case 3:
                overlay::AUTO_SHOW_KEYPAD_P1 = true;
                overlay::AUTO_SHOW_KEYPAD_P2 = true;
                break;
        }
    }
    if (options[launcher::Options::spice2x_WindowBorder].is_active()) {
        GRAPHICS_WINDOW_STYLE = options[launcher::Options::spice2x_WindowBorder].value_uint32();
    }
    if (options[launcher::Options::spice2x_WindowSize].is_active()) {
        std::pair<uint32_t, uint32_t> result;
        if (parse_width_height(options[launcher::Options::spice2x_WindowSize].value_text(), result)) {
            GRAPHICS_WINDOW_SIZE = result;
        } else {
            log_warning("launcher", "failed to parse -windowsize");
        }
    }
    if (options[launcher::Options::spice2x_WindowPosition].is_active()) {
        GRAPHICS_WINDOW_POS = options[launcher::Options::spice2x_WindowPosition].value_text();
    }
    GRAPHICS_WINDOW_ALWAYS_ON_TOP = options[launcher::Options::spice2x_WindowAlwaysOnTop].value_bool();
    GRAPHICS_WINDOW_BACKBUFFER_SCALE = options[launcher::Options::WindowForceScaling].value_bool();

    // IIDX Windowed Subscreen
    if (options[launcher::Options::spice2x_IIDXWindowedSubscreenSize].is_active()) {
        GRAPHICS_IIDX_WSUB_SIZE = options[launcher::Options::spice2x_IIDXWindowedSubscreenSize].value_text();
    }
    if (options[launcher::Options::spice2x_IIDXWindowedSubscreenPosition].is_active()) {
        GRAPHICS_IIDX_WSUB_POS = options[launcher::Options::spice2x_IIDXWindowedSubscreenPosition].value_text();
    }

    if (options[launcher::Options::spice2x_JubeatLegacyTouch].value_bool()) {
        games::jb::TOUCH_LEGACY_BOX = true;
    }
    if (options[launcher::Options::spice2x_RBTouchScale].is_active()) {
        games::rb::TOUCH_SCALING = options[launcher::Options::spice2x_RBTouchScale].value_uint32();
    }
    if (options[launcher::Options::spice2x_AsioForceUnload].value_bool()) {
        hooks::audio::ASIO_FORCE_UNLOAD_ON_STOP = true;
    }
    if (options[launcher::Options::spice2x_DRSDisableTouch].value_bool()) {
        games::drs::DISABLE_TOUCH = true;
    }
    if (options[launcher::Options::spice2x_DRSTransposeTouch].value_bool()) {
        games::drs::TRANSPOSE_TOUCH = true;
    }
    if (options[launcher::Options::spice2x_AutoCard].is_active()) {
        const auto text = options[launcher::Options::spice2x_AutoCard].value_text();
        if (text == "p1") {
            AUTO_INSERT_CARD[0] = true;
        } else if (text == "p2") {
            AUTO_INSERT_CARD[1] = true;
        } else if (text == "both") {
            AUTO_INSERT_CARD[0] = true;
            AUTO_INSERT_CARD[1] = true;
        }
    }
    if (options[launcher::Options::spice2x_LowLatencySharedAudio].value_bool()) {
        hooks::audio::LOW_LATENCY_SHARED_WASAPI = true;
    }
    if (options[launcher::Options::spice2x_TapeLedAlgorithm].is_active()) {
        const auto text = options[launcher::Options::spice2x_TapeLedAlgorithm].value_text();
        if (text == "off") {
            tapeledutils::TAPE_LED_ALGORITHM = tapeledutils::TAPE_LED_USE_NONE;
        } else if (text == "avg") {
            tapeledutils::TAPE_LED_ALGORITHM = tapeledutils::TAPE_LED_USE_AVERAGE;
        } else if (text == "first") {
            tapeledutils::TAPE_LED_ALGORITHM = tapeledutils::TAPE_LED_USE_FIRST;
        } else if (text == "last") {
            tapeledutils::TAPE_LED_ALGORITHM = tapeledutils::TAPE_LED_USE_LAST;
        }
    }
    if (options[launcher::Options::CCJTrackballSensitivity].is_active()) {
        games::ccj::TRACKBALL_SENSITIVITY = (uint8_t)
                options[launcher::Options::CCJTrackballSensitivity].value_uint32();
    }
    if (options[launcher::Options::CCJMouseTrackball].value_bool()) {
        games::ccj::MOUSE_TRACKBALL = true;
    }
    if (options[launcher::Options::CCJMouseTrackballWithToggle].value_bool()) {
        games::ccj::MOUSE_TRACKBALL_USE_TOGGLE = true;
    }
    if (options[launcher::Options::CCJArgs].is_active()) {
        games::ccj::CCJ_INJECT_ARGS = options[launcher::Options::CCJArgs].value_text();
    }
    if (options[launcher::Options::QKSArgs].is_active()) {
        games::qks::QKS_INJECT_ARGS = options[launcher::Options::QKSArgs].value_text();
    }
    if (options[launcher::Options::MFGArgs].is_active()) {
        games::mfg::MFG_INJECT_ARGS = options[launcher::Options::MFGArgs].value_text();
    }
    if (options[launcher::Options::MFGCabType].is_active()) {
        games::mfg::MFG_CABINET_TYPE = options[launcher::Options::MFGCabType].value_text();
    }
    if (options[launcher::Options::MFGNoIO].is_active()) {
        games::mfg::MFG_NO_IO = options[launcher::Options::MFGNoIO].value_bool();
    }
    if (options[launcher::Options::spice2x_EnableSMXStage].value_bool()) {
        rawinput::ENABLE_SMX_STAGE = true;
    }
    if (options[launcher::Options::spice2x_EnableSMXDedicab].value_bool()) {
        rawinput::ENABLE_SMX_DEDICAB = true;
    }
    if (options[launcher::Options::MidiAlgoVer].is_active()) {
        if (options[launcher::Options::MidiAlgoVer].value_text() == "legacy") {
            midi_algo = rawinput::MidiNoteAlgorithm::LEGACY;
        } else if (options[launcher::Options::MidiAlgoVer].value_text() == "v2") {
            midi_algo = rawinput::MidiNoteAlgorithm::V2;
        } else if (options[launcher::Options::MidiAlgoVer].value_text() == "v2_drum") {
            midi_algo = rawinput::MidiNoteAlgorithm::V2_DRUM;
        }
        // else - automatic (no value)
    }
    if (cfg::CONFIGURATOR_STANDALONE) {
        // three reasons for this hardcoded value
        //   1) poll rate is tied to framerate in spicecfg, and the software ImGui renderer has
        //      inconsistent framerate, often failing to meet 60fps
        //   2) the default is 20ms which is 1 frame, which is too quick for the user to see
        //   3) to help with binding process
        rawinput::MIDI_NOTE_SUSTAIN = 100;
    } else if (options[launcher::Options::MidiNoteSustain].is_active()) {
        rawinput::MIDI_NOTE_SUSTAIN = options[launcher::Options::MidiNoteSustain].value_uint32();
    }

    // API debugging
    if (api_debug && !cfg::CONFIGURATOR_STANDALONE) {
        API_CONTROLLER = std::make_unique<api::Controller>(api_port, api_pass, api_pretty);
        for (size_t i = 0; i < std::min(api_serial_port.size(), api_serial_baud.size()); i++) {
            API_CONTROLLER->listen_serial(api_serial_port[i], api_serial_baud[i]);
        }
        if (cfg_run) {
            exit(spicecfg_run(sextet_devices));
        } else {
            while (API_CONTROLLER->server_running) {
                Sleep(100);
            }
        }
        log_fatal("launcher", "API server stopped");
    }

    // delay
    if (!cfg::CONFIGURATOR_STANDALONE) {
        DWORD delayInSeconds = 0;
        if (options[launcher::Options::spice2x_DelayByNSeconds].is_active()) {
            delayInSeconds = (DWORD)options[launcher::Options::spice2x_DelayByNSeconds].value_uint32();
        } else if (options[launcher::Options::DelayBy5Seconds].value_bool()) {
            delayInSeconds = 5;
        }

        if (0 < delayInSeconds) {
            log_info("launcher", "delay by {}ms...", delayInSeconds * 1000);
            Sleep(delayInSeconds * 1000);
        }
    }

    // create log file
    // configurator does not write a log file because it tends to cause the
    // config file to be corrupt...
    if (!cfg::CONFIGURATOR_STANDALONE) {
        avs::core::create_log();
    }

    // log
#ifdef SPICE64
    log_info("launcher", "SpiceTools Bootstrap (x64) (spice2x)");
#else
    log_info("launcher", "SpiceTools Bootstrap (x32) (spice2x)");
#endif
    log_info("launcher", "{}", VERSION_STRING);

    // log command line arguments
    std::ostringstream arguments;
    for (auto &root_option : options) {
        std::vector<Option> options_all { root_option };

        options_all.insert(options_all.end(),
                root_option.alternatives.begin(),
                root_option.alternatives.end());

        for (const auto &option : options_all) {
            if (option.is_active()) {
                auto &definition = option.get_definition();

                arguments << "                                      ";
                arguments << "-";

                if (definition.display_name.empty()) {
                    arguments << definition.name;
                } else {
                    arguments << definition.display_name;
                }

                if (definition.type != OptionType::Bool) {
                    arguments << " ";

                    if (definition.sensitive) {
                        arguments << std::string(5, '*');
                    } else {
                        arguments << option.value;
                    }
                }

                arguments << "\n";
            }
        }
    }
    log_info("launcher", "arguments:\n{}", arguments.str());

    // deleted options
    if (options[launcher::Options::OpenKFControl].value_bool()) {
        log_fatal("launcher", "KFControl has been removed from spice2x; please use an older version.");
    }
    if (options[launcher::Options::VREnable].value_bool()) {
        log_warning("launcher", "-vr has been removed from spice2x; ignoring");
    }
    if (options[launcher::Options::ExecuteScript].is_active()) {
        log_warning("launcher", "-script has been removed from spice2x; ignoring");
    }

    // disable automatic system/monitor sleep
    if (!SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)) {
        log_warning("launcher", "could not set thread execution state: {}", GetLastError());
    }

    if (!cfg::CONFIGURATOR_STANDALONE) {

        // set process priority
        cpuutils::set_processor_priority(process_priority_str);

        // set process affinity
        if (options[launcher::Options::spice2x_ProcessAffinity].is_active()) {
            uint64_t affinity = options[launcher::Options::spice2x_ProcessAffinity].value_hex64();
            cpuutils::set_processor_affinity(affinity, true);

        // efficiency class (but only if no affinity is set)
        } else if (options[launcher::Options::spice2x_ProcessorEfficiencyClass].is_active()) {
            const auto eff_class_str =
                options[launcher::Options::spice2x_ProcessorEfficiencyClass].value_text();
            if (eff_class_str == "ecores") {
                cpuutils::set_processor_affinity(cpuutils::CpuEfficiencyClass::PreferECores);
            } else if (eff_class_str == "pcores") {
                cpuutils::set_processor_affinity(cpuutils::CpuEfficiencyClass::PreferPCores);
            }
        }

        // initialize nvapi if available
        nvapi::initialize();
        // add application profile to nvcp
        nvapi::set_profile_settings();
        // enable super exit
        superexit::enable();

        // enable subscreen touch emulation
        if (options[launcher::Options::spice2x_IIDXEmulateSubscreenKeypadTouch].is_active()) {
            games::iidx::poke::enable();
        }
        if (options[launcher::Options::NostalgiaPoke].is_active()) {
            games::nost::ENABLE_POKE = TRUE;
        }
    }

    // early hooks
    for (auto &hook : early_hooks) {
        log_info("launcher", "loading early hook DLL {}", hook);
        HMODULE module;
        if (!(module = libutils::try_library(hook))) {
            log_warning("launcher", "failed to load early hook {}", hook);
        }
    }

    // auto detect game if not specified
    if (avs::game::DLL_NAME.empty()) {
        bool module_path_tried = false;
        do {

            // IIDX
            if (check_dll("bm2dx.dll")) {
                avs::game::DLL_NAME = "bm2dx.dll";
                attach_io = true;
                attach_iidx = true;
                // automatically show cursor in windowed mode to interact with second window (sub)
                if (GRAPHICS_WINDOWED) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // SDVX
            if (check_dll("soundvoltex.dll")) {
                avs::game::DLL_NAME = "soundvoltex.dll";
                attach_io = true;
                attach_sdvx = true;
#ifdef SPICE64
                debughook::DEBUGHOOK_LOGGING = false;
#endif
                // automatically show cursor in windowed mode to interact with second window (sub)
                if (GRAPHICS_WINDOWED) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // JB
            if (check_dll("jubeat.dll")) {
                avs::game::DLL_NAME = "jubeat.dll";
                attach_io = true;
                attach_jb = true;
                break;
            }

            // RB
            if (check_dll("reflecbeat.dll")) {
                avs::game::DLL_NAME = "reflecbeat.dll";
                attach_io = true;
                attach_rb = true;
                break;
            }

            // Shogikai
            if (check_dll("shogi_engine.dll")) {
                avs::game::DLL_NAME = "system.dll";
                attach_io = true;
                attach_shogikai = true;

                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // PCM & MGA
            if (check_dll("launch.dll")) {
                avs::game::DLL_NAME = "launch.dll";
                attach_io = true;

                // MGA uses ESS while PCM does not
                if (check_dll("ess.dll")) {
                    attach_mga = true;
                } else {
                    attach_pcm = true;
                }

                break;
            }

            // DEA
            if (check_dll("arkkdm.dll")) {
                avs::game::DLL_NAME = "arkkdm.dll";
                attach_io = true;
                attach_dea = true;

                // the game is windowed by default unless we set the env
                if (GRAPHICS_WINDOWED) {
                    GRAPHICS_WINDOWED = false;
                } else {
                    SetEnvironmentVariable("DAMAC_VIEWER_FULLSCREEN", "0");
                }

                break;
            }

            // BS
            if (check_dll("beatstream.dll")) {
                avs::game::DLL_NAME = "beatstream.dll";
                attach_io = true;
                attach_bs = true;

                // game crash fix
                easrv_maint = false;

                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }

                break;
            }

            // RF3D
            if (check_dll("jgt.dll")) {
                avs::game::DLL_NAME = "jgt.dll";
                attach_io = true;
                attach_rf3d = true;
                break;
            }

            // MUSECA
            if (check_dll("museca.dll")) {
                avs::game::DLL_NAME = "museca.dll";
                attach_io = true;
                attach_museca = true;
                break;
            }

            // pop'n Lapistoria/eclale/UsaNeko/peace
            if (check_dll("popn22.dll")) {
                avs::game::DLL_NAME = "popn22.dll";
                attach_io = true;
                attach_popn = true;
                break;
            }

            // pop'n Sunny Park
            if (check_dll("popn21.dll")) {
                avs::game::DLL_NAME = "popn21.dll";
                attach_io = true;
                attach_popn = true;
                break;
            }

            // pop'n Fantasia
            if (check_dll("popn20.dll")) {
                avs::game::DLL_NAME = "popn20.dll";
                attach_io = true;
                attach_popn = true;
                break;
            }

            // pop'n Tune Street
            if (check_dll("popn19.dll")) {
                avs::game::DLL_NAME = "popn19.dll";
                attach_io = true;
                attach_popn = true;
                break;
            }

            // DDR Ace/A20 (bio2)
            if (check_dll("arkmdxbio2.dll")) {
                avs::game::DLL_NAME = "arkmdxbio2.dll";
                attach_io = true;
                attach_ddr = true;
                break;
            }

            // DDR Ace/A20 (p3io)
            if (check_dll("arkmdxp3.dll")) {
                avs::game::DLL_NAME = "arkmdxp3.dll";
                attach_io = true;
                attach_ddr = true;
                break;
            }

            // DDR Ace/A20 (p4io)
            if (check_dll("arkmdxp4.dll")) {
                avs::game::DLL_NAME = "arkmdxp4.dll";
                attach_io = true;
                attach_ddr = true;
                break;
            }

            // DDR 2013/2014 (old cabinets)
            if (check_dll("mdxja_945.dll")) {
                avs::game::DLL_NAME = "mdxja_945.dll";
                attach_io = true;
                attach_ddr = true;
                break;
            }

            // DDR 2013/2014 (white cabinet)
            if (check_dll("mdxja_hm65.dll")) {
                avs::game::DLL_NAME = "mdxja_hm65.dll";
                attach_io = true;
                attach_ddr = true;
                break;
            }

            // DDR X2/X3
            if (check_dll("ddr.dll")) {
                avs::game::DLL_NAME = "ddr.dll";
                attach_io = true;
                attach_ddr = true;
                break;
            }

            // GitaDora
            if (check_dll("gdxg.dll")) {
                avs::game::DLL_NAME = "gdxg.dll";
                attach_io = true;
                attach_device = true;
                attach_extdev = true;
                attach_gitadora = true;
                break;
            }

            // Nostalgia
            if (check_dll("nostalgia.dll")) {
                avs::game::DLL_NAME = "nostalgia.dll";
                attach_io = true;
                attach_nostalgia = true;

                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }

                break;
            }

            // Bishi Bashi Channel
            if (check_dll("bsch.dll")) {
                avs::game::DLL_NAME = "bsch.dll";
                attach_io = true;
                attach_bbc = true;
                break;
            }

            // HELLO! Pop'n Music
            if (check_dll("popn.dll")) {
                avs::game::DLL_NAME = "popn.dll";
                attach_io = true;
                attach_hpm = true;
                break;
            }

            // Quiz Magic Academy
            if (check_dll("client.dll")) {
                avs::game::DLL_NAME = "client.dll";
                attach_io = true;
                attach_qma = true;
                break;
            }

            // LovePlus
            if (check_dll("arkklp.dll")) {
                avs::game::DLL_NAME = "arkklp.dll";
                attach_io = true;
                attach_loveplus = true;
                attach_cpusbxpkm_printer = true;
                break;
            }

            // Steel Chronicle
            if (check_dll("arkkgg.dll")) {
                avs::game::DLL_NAME = "arkkgg.dll";
                attach_io = true;
                attach_sc = true;
                easrv_maint = false;
                break;
            }

            // Mahjong Fight Club
            if (check_dll("allinone.dll")) {
                avs::game::DLL_NAME = "system.dll";
                attach_io = true;
                attach_mfc = true;
                break;
            }

            // FutureTomTom
            if (check_dll("arkmmd.dll")) {
                avs::game::DLL_NAME = "arkmmd.dll";
                attach_io = true;
                attach_ftt = true;

                // the game is windowed by default unless we set the env
                if (GRAPHICS_WINDOWED) {
                    GRAPHICS_WINDOWED = false;
                } else {
                    SetEnvironmentVariable("DAMAC_VIEWER_FULLSCREEN", "0");
                }

                break;
            }

            // Scotto
            if (check_dll("scotto.dll")) {
                avs::game::DLL_NAME = "scotto.dll";
                attach_io = true;
                attach_scotto = true;
                break;
            }

            // TsumTsum
            if (check_dll("arko26.dll")) {
                avs::game::DLL_NAME = "arko26.dll";
                attach_io = true;
                break;
            }

            // DANCERUSH
            if (check_dll("superstep.dll")) {
                avs::game::DLL_NAME = "superstep.dll";
                attach_io = true;
                attach_drs = true;
                break;
            }

            // Winning Eleven 2012
            if (check_dll("weac12_bootstrap_release.dll")) {
                avs::game::DLL_NAME = "weac12_bootstrap_release.dll";
                attach_io = true;
                attach_we = true;
                break;
            }

            // Winning Eleven 2014
            if (check_dll("arknck.dll")) {
                avs::game::DLL_NAME = "arknck.dll";
                attach_io = true;
                attach_we = true;

                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }

                break;
            }

            // Otoca D'or
            if (check_dll("arkkep.dll")) {
                avs::game::DLL_NAME = "arkkep.dll";
                attach_io = true;
                attach_otoca = true;
                break;
            }

            // Silent Scope: Bone Eater
            if (check_dll("arkndd.dll")) {
                avs::game::DLL_NAME = "arkndd.dll";
                attach_io = true;
                attach_silentscope = true;
                break;
            }

            // Ongaku Paradise
            if (check_dll("arkjc9.dll")) {
                avs::game::DLL_NAME = "arkjc9.dll";
                attach_io = true;
                attach_onpara = true;
                break;
            }

            // Chase Chase Jokers
            if (check_dll("kamunity.dll") && fileutils::file_exists("game/chaseproject.exe")) {
                avs::game::DLL_NAME = "kamunity.dll";
                attach_io = true;
                attach_ccj = true;
                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // QuizKnock STADIUM
            if (check_dll("kamunity.dll") && fileutils::file_exists("game/uks.exe")) {
                avs::game::DLL_NAME = "kamunity.dll";
                attach_io = true;
                attach_qks = true;
                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // Mahjong Fight Girl
            if (check_dll("kamunity.dll") && fileutils::dir_exists("game/MFGClient_Data")) {
                avs::game::DLL_NAME = "kamunity.dll";
                attach_io = true;
                attach_mfg = true;
                launcher::signal::USE_VEH_WORKAROUND = true;
                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // Busou Shinki: Armored Princess Battle Conductor
            if (check_dll("kamunity.dll") && fileutils::file_exists("game/bsac_app.exe")) {
                avs::game::DLL_NAME = "kamunity.dll";
                attach_io = true;
                attach_bc = true;
                // automatically show cursor when no touchscreen is available
                if (!is_touch_available()) {
                    GRAPHICS_SHOW_CURSOR = true;
                }
                break;
            }

            // try module path
            if (!module_path_tried) {
                module_path_tried = true;

                MODULE_PATH /= "modules";
                SetDllDirectoryW(MODULE_PATH.c_str());

                MODULE_PATH /= "";

                continue;
            }

            // usage error
            if (!cfg::CONFIGURATOR_STANDALONE
            && (!CHECK_DLL_IGNORE_ARCH)) {
                log_fatal("launcher", "module auto detection failed.");
            }
            break;

        } while (true);
    }

    // set error mode to show all errors
    SetErrorMode(0);

    // set the AVS heap size to a default value varying by game
    avs::core::set_default_heap_size(avs::game::DLL_NAME);

    // load the games
    std::vector<games::Game *> games;
    if (attach_popn) {
        games.push_back(new games::popn::POPNGame());
    }
    if (attach_bbc) {
        avs::core::set_default_heap_size("bsch.dll");
        games.push_back(new games::bbc::BBCGame());
    }
    if (attach_ddr) {
        avs::core::set_default_heap_size("arkmdxp3.dll");
        games.push_back(new games::ddr::DDRGame());
    }
    if (attach_iidx) {
        avs::core::set_default_heap_size("bm2dx.dll");
        games.push_back(new games::iidx::IIDXGame());
    }
    if (attach_sdvx) {
        avs::core::set_default_heap_size("soundvoltex.dll");
        games.push_back(new games::sdvx::SDVXGame());
    }
    if (attach_jb) {
        avs::core::set_default_heap_size("jubeat.dll");
        games.push_back(new games::jb::JBGame());
    }
    if (attach_nostalgia) {
        avs::core::set_default_heap_size("nostalgia.dll");
        games.push_back(new games::nost::NostGame());
    }
    if (attach_gitadora) {
        games.push_back(new games::gitadora::GitaDoraGame());
    }
    if (attach_hpm) {
        avs::core::set_default_heap_size("popn.dll");
        games.push_back(new games::hpm::HPMGame());
    }
    if (attach_mga) {
        games.push_back(new games::mga::MGAGame());
    }
    if (attach_sc) {
        games.push_back(new games::sc::SCGame());
    }
    if (attach_rb) {
        games.push_back(new games::rb::RBGame());
    }
    if (attach_shogikai) {
        games.push_back(new games::shogikai::ShogikaiGame());
    }
    if (attach_qma) {
        avs::core::set_default_heap_size("client.dll");
        games.push_back(new games::qma::QMAGame());
    }
    if (attach_dea) {
        games.push_back(new games::dea::DEAGame());
    }
    if (attach_mfc) {
        avs::core::set_default_heap_size("system.dll");
        games.push_back(new games::mfc::MFCGame());
    }
    if (attach_ftt) {
        avs::core::set_default_heap_size("arkmmd.dll");
        games.push_back(new games::ftt::FTTGame());
    }
    if (attach_bs) {
        games.push_back(new games::bs::BSGame());
    }
    if (attach_loveplus) {
        games.push_back(new games::loveplus::LovePlusGame());
    }
    if (attach_scotto) {
        avs::core::set_default_heap_size("scotto.dll");
        games.push_back(new games::scotto::ScottoGame());
    }
    if (attach_rf3d) {
        games.push_back(new games::rf3d::RF3DGame());
    }
    if (attach_museca) {
        games.push_back(new games::museca::MusecaGame());
    }
    if (attach_drs) {
        avs::core::set_default_heap_size("superstep.dll");
        games.push_back(new games::drs::DRSGame());
    }
    if (attach_we) {
        games.push_back(new games::we::WEGame());
    }
    if (attach_otoca) {
        games.push_back(new games::otoca::OtocaGame());
    }
    if (attach_silentscope) {
        games.push_back(new games::silentscope::SilentScopeGame());
    }
    if (attach_pcm) {
        games.push_back(new games::pcm::PCMGame());
    }
    if (attach_onpara) {
        avs::core::set_default_heap_size("arkjc9.dll");
        games.push_back(new games::onpara::OnparaGame());
    }
    if (attach_bc) {
        avs::core::set_default_heap_size("kamunity.dll");
        games.push_back(new games::bc::BCGame());
    }
    if (attach_ccj) {
        avs::core::set_default_heap_size("kamunity.dll");
        games.push_back(new games::ccj::CCJGame());
    }
    if (attach_qks) {
        avs::core::set_default_heap_size("kamunity.dll");
        games.push_back(new games::qks::QKSGame());
    }
    if (attach_mfg) {
        avs::core::set_default_heap_size("kamunity.dll");
        games.push_back(new games::mfg::MFGGame());
    }

    // apply user heap size, if defined
    if (user_heap_size > 0) {
        avs::core::HEAP_SIZE = user_heap_size;
    }

    // call pre-attach
    for (auto game : games) {
        game->pre_attach();
    }

    // run configuration utility
    if (cfg_run || cfg::CONFIGURATOR_STANDALONE) {
        // cardio thread for cfg
        if (cardio_enabled) {
            cardio_runner_start(true);
        }

        if (!midi_algo.has_value()) {
            midi_algo = rawinput::MidiNoteAlgorithm::V2;
        }
        rawinput::set_midi_algorithm(midi_algo.value());

        if (api_enable || api_debug) {
            API_CONTROLLER = std::make_unique<api::Controller>(api_port, api_pass, api_pretty);
            for (size_t i = 0; i < std::min(api_serial_port.size(), api_serial_baud.size()); i++) {
                API_CONTROLLER->listen_serial(api_serial_port[i], api_serial_baud[i]);
            }
        }
        exit(spicecfg_run(sextet_devices));
    }

    // print cpu features
    if (!cfg::CONFIGURATOR_STANDALONE && dump_sysinfo) {
        cpuutils::print_cpu_features();
        sysutils::print_os();
        sysutils::print_smbios();
        sysutils::print_gpus();
    }

    // initialize raw input
    RI_MGR = std::make_unique<rawinput::RawInputManager>();
    for (const auto &device : sextet_devices) {
        RI_MGR->sextet_register(device);
    }

    // print devices
    RI_MGR->devices_print();

    // cardio
    if (cardio_enabled) {
        cardio_runner_start(true);
    }

    // load stubs
    if (load_stubs) {
        for (const auto &stub : STUBS) {
            if (fileutils::verify_header_pe(MODULE_PATH / stub)) {
                libutils::load_library(MODULE_PATH / stub);
            } else {
                log_warning("launcher", "failed to load stubs!");
                load_stubs = false;
            }
        }
    }

    // locale hook
    if (!lang_disable) {
        hooks::lang::early_init();
    }

    // load DLLs
    avs::core::load_dll();
    avs::ea3::load_dll();

    // net fix
    if (!netfix_disable) {
        networkhook_init();
    }

    // boot AVS
    avs::core::boot();

    // initialize SSL
    if (!ssl_disable) {
        avs::ssl::init();
    }

    // copy defaults to nvram
    avs::core::copy_defaults();

    // prepare patches
    {
        overlay::windows::PatchManager patch_manager(nullptr, false);
    }

    // load game
    avs::game::load_dll();

    // attach games
    for (auto game : games) {
        game->attach();
    }

#ifdef SPICE64
    if (!cfg::CONFIGURATOR_STANDALONE) {
        if (games::iidx::TDJ_CAMERA) {
            games::iidx::init_camera_hooks();
        }
        if (!D3D9_DEVICE_HOOK_DISABLE) {
            nvenc_hook::initialize();
        }
    }
#endif

    // attach stub functions
    if (!load_stubs) {
        stubs::attach();
    }

    // locale hook
    if (!lang_disable) {
        hooks::lang::init();
    }

    // exception/signal hook
    launcher::signal::attach();

    // audio hook
    hooks::audio::init();

    // DirectInput 8 hook
    hooks::input::dinput8::init();

    // D3D9 hook
    graphics_init();

    // debug hook
    debughook::attach();

    // device debug
    if (DEVICE_CREATEFILE_DEBUG) {
        devicehook_init();
    }

    // server
    if (easrv_port != 0u && !easrv_smart) {
        easrv_start(easrv_port, easrv_maint, 4, 8);
    }

    // acio attach
    if (attach_io || attach_acio) {
        acio::attach();
    }

    // acio icca attach
    if (attach_icca) {
        acio::attach_icca();
    }

    // device attach
    if (attach_io || attach_device) {
        spicedevice_attach();
    }

    // ext dev attach
    if (attach_io || attach_extdev) {
        extdev_attach();
    }

    // sci unit attach
    if (attach_io || attach_sciunit) {
        sciunit_attach();
    }

    // SDVX printer attach
    if (attach_cpusbxpkm_printer) {
        games::shared::printer_attach();
    }

    // net fix
    if (!netfix_disable) {
        networkhook_init();
    }

    // layeredfs
    if (fileutils::dir_exists("data_mods") &&
        !fileutils::file_exists("ifs_hook.dll") &&
        !fileutils::file_exists(MODULE_PATH / "ifs_hook.dll")) {
        log_warning("launcher", "data_mods directory was found, but ifs_hook.dll is not present; your mods will not load");
        log_warning("launcher", "to fix this, download ifs_layeredfs and add it as a DLL hook (-k)");
        log_warning("launcher", "https://github.com/mon/ifs_layeredfs");
    }

    update_msvcrt_args(argc, argv);

    // load hooks
    for (auto &hook : game_hooks) {
        log_info("launcher", "loading hook DLL {}", hook);
        HMODULE module;
        if (!(module = libutils::try_library(hook))) {
            log_warning("launcher", "failed to load hook {}", hook);
        } else {
            bt5api_hook(module);
        }
    }

    // apply patches
    {
        overlay::windows::PatchManager patch_manager(nullptr, true);
    }

    // load AVS-EA3
    avs::ea3::boot(easrv_port, easrv_maint, easrv_smart);

    // eamuse init
    eamuse_autodetect_game();

    // unis device hook
    unisintrhook_init();

    // BT5API
    if (BT5API_ENABLED) {
        bt5api_init();
    }

    // API
    if (api_enable || std::min(api_serial_port.size(), api_serial_baud.size()) > 0) {
        API_CONTROLLER = std::make_unique<api::Controller>(api_port, api_pass, api_pretty);
    }
    for (size_t i = 0; i < std::min(api_serial_port.size(), api_serial_baud.size()); i++) {
        API_CONTROLLER->listen_serial(api_serial_port[i], api_serial_baud[i]);
    }

    // start coin input thread
    eamuse_coin_start_thread();

    // pin macro
    if (!cfg::CONFIGURATOR_STANDALONE && PIN_MACRO_ENABLED) {
        eamuse_pin_macro_start_thread();
    }

    // print PEB
    if (peb_print) {
        peb::peb_print();
    }

    // enable automap
    if (automap) {
        avs::automap::enable();
    }

    // initialize rich presence
    if (rich_presence) {
        richpresence::init();
    }

    // turn off controlled game lights
    auto lights = games::get_lights(eamuse_get_game());
    if (lights) {
        for (auto &light : *lights) {
            GameAPI::Lights::writeLight(RI_MGR, light, 0.f);
        }
        RI_MGR->devices_flush_output();
    }

    // update rawinput midi algorithm, which is game-dependent
    if (!midi_algo.has_value()) {
        if (games::gitadora::is_drum() || avs::game::is_model("MMD")) {
            // GITADORA DrumMania, FutureTomTom
            midi_algo = rawinput::MidiNoteAlgorithm::V2_DRUM;
        } else {
            midi_algo = rawinput::MidiNoteAlgorithm::V2;
        }
    }
    rawinput::set_midi_algorithm(midi_algo.value());

    // attach games
    for (auto game : games) {
        game->post_attach();
    }

    // game start
    log_info("launcher", "calling game entry");
    avs::game::entry_main();

    // clear presence
    richpresence::shutdown();

    // detach games
    for (auto game : games) {
        game->detach();
    }

    // sci unit detach
    if (attach_io || attach_sciunit) {
        sciunit_detach();
    }

    // ext dev detach
    if (attach_io || attach_extdev) {
        extdev_detach();
    }

    // device detach
    if (attach_io || attach_device) {
        spicedevice_detach();
    }

    // acio detach
    if (attach_io || attach_acio) {
        acio::detach();
    }

    // delete games
    while (!games.empty()) {
        delete games.back();
        games.pop_back();
    }

    // free api controller
    API_CONTROLLER.reset();

    // stop coin input thread
    eamuse_coin_stop_thread();

    eamuse_pin_macro_stop_thread();

    // BT5API
    if (BT5API_ENABLED) {
        bt5api_dispose();
    }

    // stop raw input
    RI_MGR.reset();

    // debug hook
    debughook::detach();

    // scard
    scard_fini();

    // stop reader thread in case it was running
    stop_reader_thread();

    // cardio
    if (cardio_enabled) {
        cardio_runner_stop();
    }

    // server
    if (easrv_port != 0u) {
        easrv_shutdown();
    }

    // AVS EA3 shutdown
    avs::ea3::shutdown();

    // AVS cleanup
    avs::core::shutdown();

    // dispose crypt
    crypt::dispose();

    // disable super exit
    superexit::disable();

    // disable poke
    games::iidx::poke::disable();

#ifdef SPICE64
    games::iidx::camera_release();
#endif

    // shutdown
    log_warning("launcher", "end");
    launcher::stop_subsystems();

    return 0;
}

// https://github.com/spice2x/spice2x.github.io/issues/264
// huge ugly hack to work around things that broke when MinGW switched from msvcrt to ucrt
// this is done to ensure that any DLL hooks that rely on msvcrt continue to work
void update_msvcrt_args(int argc, char *argv[]) {
#if defined(_UCRT)
    auto msvc = LoadLibraryA("msvcrt.dll");
    if (!msvc) {
        log_warning("launcher", "failed to load msvcrt.dll");
        return;
    }

    // get __argc
    PINT32 argc_addr = (PINT32)GetProcAddress(msvc, "__argc");
    if (!argc_addr) {
        log_warning("launcher", "failed to find msvcrt!__argc");
        return;
    }
    try {
        if (*argc_addr == argc) {
            log_warning("launcher", "msvcrt!__argc is already set");
            return;
        }
    } catch (const std::exception &e) {
        log_warning("launcher", "exception while reading msvcrt!_argc: {}", e.what());
    }

    // get __argv
    PCHAR **argv_addr = (PCHAR **)GetProcAddress(msvc, "__argv");
    if (!argv_addr) {
        log_warning("launcher", "failed to find msvcrt!__argv");
        return;
    }

    // update them
    try {
        log_info("launcher", "msvcrt!__argc value before: {}", *argc_addr);
        *argc_addr = argc;
        log_info("launcher", "msvcrt!__argc value after: {}", *argc_addr);
        *argv_addr = argv;
    } catch (const std::exception &e) {
        log_warning("launcher", "exception while messing with msvcrt!_argc and _argv: {}", e.what());
    }

#else
    log_misc("launcher", "not UCRT, skipping msvcrt!_argc / _argv hacks");
#endif
}

#ifndef SPICETOOLS_SPICECFG_STANDALONE
int main(int argc, char *argv[]) {
    return main_implementation(argc, argv);
}
#endif
