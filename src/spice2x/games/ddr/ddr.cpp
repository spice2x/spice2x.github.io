#include "ddr.h"

#include "acioemu/handle.h"
#include "avs/game.h"
#include "hooks/devicehook.h"
#include "hooks/setupapihook.h"
#include "hooks/sleephook.h"
#include "hooks/input/dinput8/hook.h"
#include "util/utils.h"
#include "util/libutils.h"
#include "util/fileutils.h"
#include "util/detour.h"
#include "cfg/configurator.h"

#include "io.h"

#include "p3io/foot.h"
#include "p3io/p3io.h"
#include "p3io/sate.h"
#include "p3io/usbmem.h"

#include "p4io/p4io.h"

using namespace acioemu;

namespace games::ddr {

    // settings
    bool SDMODE = false;
    bool NO_CODEC_REGISTRATION = false;

    uint8_t DDR_TAPELEDS[TAPELED_DEVICE_COUNT][50][3] {};

    static decltype(SendMessage) *SendMessage_real = nullptr;

    static SHORT WINAPI GetAsyncKeyState_hook(int vKey) {

        // disable debug keys
        return 0;
    }

    static LRESULT WINAPI SendMessage_hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {

        // ignore broadcasts
        if (hWnd == HWND_BROADCAST) {
            return 1;
        }

        // fallback
        return SendMessage_real(hWnd, Msg, wParam, lParam);
    }

    bool contains_only_ascii(const std::string& str) {
        for (auto c: str) {
            if (static_cast<unsigned char>(c) > 127) {
                return false;
            }
        }
        return true;
    }

    DDRGame::DDRGame() : Game("Dance Dance Revolution") {
    }

    void DDRGame::register_codecs() {
        // find where spice.exe / spice64.exe is located
        const auto &spice_bin_path = libutils::module_file_name(nullptr).parent_path();

        // find the com directory
        std::filesystem::path dir = "";
        if (MODULE_PATH == spice_bin_path) {
            // try: \com
            dir = spice_bin_path / "com";
        } else {
            // try: modules\..\com
            dir = MODULE_PATH / ".." / "com";
        }

        if (fileutils::dir_exists(dir)) {
            log_info("ddr", "looking for codecs in this directory: {}", dir.string());
        } else {
            log_info("ddr", "codecs directory not found: {}", dir.string());
            return;
        }

        for (const auto &file : std::filesystem::directory_iterator(dir)) {
            const auto &filename = file.path().filename();
            const auto extension = strtolower(filename.extension().string());

            if (extension != ".dll") {
                continue;
            }

            log_info("ddr", "found DLL: {}, size: {} bytes", filename.string(), file.file_size());
            if (filename == "k-clvsd.dll" || filename.string().find("xactengine") == 0) {
                const std::wstring wcmd = L"regsvr32.exe /s \"" + file.path().wstring() + L"\"";
                const std::string cmd = "regsvr32.exe /s \"" + file.path().string() + "\"";

                int result = 0;
                std::thread t([wcmd, &result]() {
                    result = _wsystem(wcmd.c_str());
                });
                t.join();

                if (result == 0) {
                    log_info("ddr", "`{}` returned {}", cmd, result);
                } else {
                    log_warning("ddr", "`{}` failed, returned {}", cmd, result);
                }

                if (!contains_only_ascii(file.path().string())) {
                    log_warning(
                        "ddr",
                        "BAD PATH ERROR\n\n\n"
                        "!!!                                                          !!!\n"
                        "!!! filesystem path to codec contains non-ASCII characters!  !!!\n"
                        "!!! this may cause the game to crash!                        !!!\n"
                        "!!!                                                          !!!\n"
                        );
                }
            }
        }
    }
    

    void DDRGame::pre_attach() {
        if (!cfg::CONFIGURATOR_STANDALONE && avs::game::is_model("TDX")) {
            log_fatal(
                "ddr",
                "BAD MODEL NAME ERROR\n\n\n"
                "!!! model name set to TDX, this is WRONG and will break your game !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!! spice2x does not yet support TDX, use MDX instead.            !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!! model name set to TDX, this is WRONG and will break your game !!!\n\n\n"
                );
        }

        if (!cfg::CONFIGURATOR_STANDALONE && avs::game::DEST[0] == 'U') {
            log_warning(
                "ddr",
                "U-REGION WARNING\n\n\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!! <dest> is set to U region                                     !!!\n"
                "!!!                                                               !!!\n"
                "!!! While this is legal, unless you have compatible data, this    !!!\n"
                "!!! will most likely crash your game or fail to boot.             !!!\n"
                "!!!                                                               !!!\n"
                "!!! It is recommended that you stick with J region.               !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!!                                                               !!!\n\n\n"
                );
        }

        if (!cfg::CONFIGURATOR_STANDALONE) {
            if (!NO_CODEC_REGISTRATION) {
                this->register_codecs();
            } else {
                log_warning(
                    "ddr",
                    "skipping codec registration (-ddrnocodec), "
                    "game may crash if you didn't register codecs before launching the game");
            }
        }
    }

    void DDRGame::attach() {
        Game::attach();

        // dinput hook on this dll since the game dll doesn't load it
        auto game_mdx = libutils::try_library(MODULE_PATH / "gamemdx.dll");
        hooks::input::dinput8::init(game_mdx);

        // init device hook
        devicehook_init();

        // add fake devices
        if (avs::game::DLL_NAME == "arkmdxbio2.dll") {
            devicehook_add(new acioemu::ACIOHandle(L"COM1"));
        } else if(avs::game::DLL_NAME == "arkmdxp4.dll") {
            devicehook_add(new DDRP4IOHandle());
        } else {
            devicehook_add(new DDRFOOTHandle());
            devicehook_add(new DDRSATEHandle());
            devicehook_add(new DDRUSBMEMHandle());
            devicehook_add(new DDRP3IOHandle());
        }

        // has nothing to do with P3IO, but is enough to trick the game into SD/HD mode
        const char *settings_property = ddr::SDMODE ? "Generic Television" : "Generic Monitor";
        const char settings_detail[] = R"(\\.\P3IO)";

        // settings 1
        SETUPAPI_SETTINGS settings1 {};
        settings1.class_guid[0] = 0x1FA4A480;
        settings1.class_guid[1] = 0x40C7AC60;
        settings1.class_guid[2] = 0x7952ACA7;
        settings1.class_guid[3] = 0x5A57340F;
        memcpy(settings1.property_devicedesc, settings_property, strlen(settings_property) + 1);
        memcpy(settings1.interface_detail, settings_detail, sizeof(settings_detail));

        // settings 2
        SETUPAPI_SETTINGS settings2 {};
        settings2.class_guid[0] = 0x4D36E96E;
        settings2.class_guid[1] = 0x11CEE325;
        settings2.class_guid[2] = 0x8C1BF;
        settings2.class_guid[3] = 0x1803E12B;
        memcpy(settings2.property_devicedesc, settings_property, strlen(settings_property) + 1);
        memcpy(settings2.interface_detail, settings_detail, sizeof(settings_detail));

        const char settings_detail_p4io[] = R"(\\.\P4IO)";

        // settings p4io
        SETUPAPI_SETTINGS settingsp4io {};
        settingsp4io.class_guid[0] = 0x8B7250A5;
        settingsp4io.class_guid[1] = 0x46C94F61;
        settingsp4io.class_guid[2] = 0x68E63A84;
        settingsp4io.class_guid[3] = 0x206A4706;
        memcpy(settingsp4io.property_devicedesc, settings_property, strlen(settings_property) + 1);
        memcpy(settingsp4io.interface_detail, settings_detail_p4io, sizeof(settings_detail_p4io));

        // init SETUP API
        setupapihook_init(avs::game::DLL_INSTANCE);

        // DDR ACE actually uses another DLL for things
        if (game_mdx != nullptr) {
            setupapihook_init(game_mdx);
        }

        // add settings
        setupapihook_add(settings1);
        setupapihook_add(settings2);
        setupapihook_add(settingsp4io);

        // misc hooks
        detour::iat_try("GetAsyncKeyState", GetAsyncKeyState_hook, avs::game::DLL_INSTANCE);
        detour::iat_try("GetKeyState", GetAsyncKeyState_hook, avs::game::DLL_INSTANCE);
        SendMessage_real = detour::iat_try("SendMessageW", SendMessage_hook, avs::game::DLL_INSTANCE);
        detour::iat_try("SendMessageA", SendMessage_hook, avs::game::DLL_INSTANCE);
    }

    void DDRGame::detach() {
        Game::detach();

        // dispose device hook
        devicehook_dispose();
    }
}
