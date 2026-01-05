#include "bi2x_hook.h"

#if SPICE64

#include <cstdint>
#include "util/detour.h"
#include "util/logging.h"
#include "util/utils.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/io.h"
#include "launcher/options.h"
#include "io.h"
#include "iidx.h"
#include "util/tapeled.h"

namespace games::iidx {
    constexpr bool BI2X_PASSTHROUGH = false;

    /*
     * class definitions
     */

    struct AC_HNDLIF {
        // dummy
        uint8_t data[0x10];
    };

    struct AIO_NMGR_IOB2_VTABLE {
        // other functions here, but they never get called
        uint8_t dummy[0x50];
        void (__fastcall *pAIO_NMGR_IOB_BeginManage)(int64_t a1);
    };

    struct AIO_NMGR_IOB2 {
        AIO_NMGR_IOB2_VTABLE *vptr;
        uint8_t dummy[0x9F0];
    };

    // confirmed 0x9F8 in iidx27, iidx32 aioNMgrIob2_Create
    static_assert(sizeof(AIO_NMGR_IOB2) == 0x9F8);

    struct AIO_IOB2_BI2X_TDJ {
        // who knows
        uint8_t data[0x13F8];
    };

    // confirmed 0x13F8 in iidx27, iidx32 in aioIob2Bi2xTDJ_Create
    static_assert(sizeof(AIO_IOB2_BI2X_TDJ) == 0x13F8);

    struct AIO_IOB2_BI2X_TDJ__DEVSTATUS {
        // of course you could work with variables here
        uint8_t buffer[0xCA];
    };

    /*
     * typedefs
     */

    // libaio-iob2_video.dll
    typedef AIO_IOB2_BI2X_TDJ* (__fastcall *aioIob2Bi2xTDJ_Create_t)(AIO_NMGR_IOB2 *nmgr, int a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__GetDeviceStatus_t)(AIO_IOB2_BI2X_TDJ *This,
            AIO_IOB2_BI2X_TDJ__DEVSTATUS *a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__IoReset_t)(AIO_IOB2_BI2X_TDJ *This,
            unsigned int a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetWatchDogTimer_t)(AIO_IOB2_BI2X_TDJ *This,
            uint8_t a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__ControlCoinBlocker_t)(AIO_IOB2_BI2X_TDJ *This,
            int index, uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__AddCounter_t)(AIO_IOB2_BI2X_TDJ *This,
            int a2, int a3);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetStartLamp_t)(AIO_IOB2_BI2X_TDJ *This,
            int player, uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp_t)(AIO_IOB2_BI2X_TDJ *This,
            uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp_t)(AIO_IOB2_BI2X_TDJ *This,
            uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp_t)(AIO_IOB2_BI2X_TDJ *This,
            int player, int button, uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetWooferLED_t)(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint32_t color);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetIccrLed_t)(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint32_t color);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetTurnTableLed_t)(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint32_t color);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetTurnTableResist_t)(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint8_t resistance);
    typedef void (__fastcall *AIO_IOB2_BI2X_TDJ__SetTapeLedData_t)(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint8_t *data);

    // libaio-iob.dll
    typedef AC_HNDLIF* (__fastcall *aioIob2Bi2x_OpenSciUsbCdc_t)(uint8_t device_num);
    typedef int64_t (__fastcall *aioIob2Bi2x_WriteFirmGetState_t)(int64_t a1);
    typedef AIO_NMGR_IOB2* (__fastcall *aioNMgrIob2_Create_t)(AC_HNDLIF *a1, unsigned int a2);

    /*
     * function pointers
     */

    // libaio-iob2_video.dll
    static aioIob2Bi2xTDJ_Create_t aioIob2Bi2xTDJ_Create_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__GetDeviceStatus_t AIO_IOB2_BI2X_TDJ__GetDeviceStatus_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__IoReset_t AIO_IOB2_BI2X_TDJ__IoReset_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetWatchDogTimer_t AIO_IOB2_BI2X_TDJ__SetWatchDogTimer_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__ControlCoinBlocker_t AIO_IOB2_BI2X_TDJ__ControlCoinBlocker_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__AddCounter_t AIO_IOB2_BI2X_TDJ__AddCounter_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetStartLamp_t AIO_IOB2_BI2X_TDJ__SetStartLamp_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp_t AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp_t AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp_t AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetWooferLED_t AIO_IOB2_BI2X_TDJ__SetWooferLED_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetIccrLed_t AIO_IOB2_BI2X_TDJ__SetIccrLed_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetTurnTableLed_t AIO_IOB2_BI2X_TDJ__SetTurnTableLed_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetTurnTableResist_t AIO_IOB2_BI2X_TDJ__SetTurnTableResist_orig = nullptr;
    static AIO_IOB2_BI2X_TDJ__SetTapeLedData_t AIO_IOB2_BI2X_TDJ__SetTapeLedData_orig = nullptr;

    // libaio-iob.dll
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;

    /*
     * variables
     */

    AIO_IOB2_BI2X_TDJ *custom_node = nullptr;
    AC_HNDLIF *acHndlif = nullptr;
    AIO_NMGR_IOB2 *aioNmgrIob2 = nullptr;

    /*
     * implementations
     */

    static AIO_IOB2_BI2X_TDJ* __fastcall aioIob2Bi2xTDJ_Create(AIO_NMGR_IOB2 *nmgr, int a2) {
        if (!BI2X_PASSTHROUGH) {

            log_info("bi2x_hook", "aioIob2Bi2xTDJ_Create called (TDJ I/O emulation active)");
            games::iidx::update_io_emulation_state(
                games::iidx::iidx_aio_emulation_state::bi2x_hook);

            custom_node = new AIO_IOB2_BI2X_TDJ;
            memset(&custom_node->data, 0, sizeof(custom_node->data));

            return custom_node;
        } else {

            // call original
            return aioIob2Bi2xTDJ_Create_orig(nmgr, a2);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__GetDeviceStatus(AIO_IOB2_BI2X_TDJ *This,
            AIO_IOB2_BI2X_TDJ__DEVSTATUS *status) {

        // flush raw input
        RI_MGR->devices_flush_output();

        // check handle
        if (This == custom_node) {

            // clear input data
            memset(status, 0x00, sizeof(AIO_IOB2_BI2X_TDJ__DEVSTATUS));
        } else {

            // get data from real device
            AIO_IOB2_BI2X_TDJ__GetDeviceStatus_orig(This, status);
        }

        // get buttons
        auto &buttons = get_buttons();

        // control buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]))
            status->buffer[4] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]))
            status->buffer[5] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]))
            status->buffer[6] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::VEFX]))
            status->buffer[10] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Effect]))
            status->buffer[11] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_Headphone]))
            status->buffer[12] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_Headphone]))
            status->buffer[13] = 0xFF;

        // coin stock
        status->buffer[22] += eamuse_coin_get_stock();

        // player 1 buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_Start]))
            status->buffer[8] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_1]))
            status->buffer[27] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_2]))
            status->buffer[28] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_3]))
            status->buffer[29] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_4]))
            status->buffer[30] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_5]))
            status->buffer[31] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_6]))
            status->buffer[32] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P1_7]))
            status->buffer[33] = 0xFF;

        // player 2 buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_Start]))
            status->buffer[9] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_1]))
            status->buffer[34] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_2]))
            status->buffer[35] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_3]))
            status->buffer[36] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_4]))
            status->buffer[37] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_5]))
            status->buffer[38] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_6]))
            status->buffer[39] = 0xFF;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::P2_7]))
            status->buffer[40] = 0xFF;

        // turntables
        status->buffer[20] += get_tt(0, false);
        status->buffer[21] += get_tt(1, false);

        return;
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__IoReset(AIO_IOB2_BI2X_TDJ *This,
            unsigned int a2)
    {
        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__IoReset_orig(This, a2);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetWatchDogTimer(AIO_IOB2_BI2X_TDJ *This,
            uint8_t a2)
    {
        if (This != custom_node) {

            // comment this out if you want to disable the BI2X watchdog timer
            return AIO_IOB2_BI2X_TDJ__SetWatchDogTimer_orig(This, a2);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__ControlCoinBlocker(AIO_IOB2_BI2X_TDJ *This,
            int index, uint8_t state) {

        // coin blocker is closed when state is zero
        eamuse_coin_set_block(state == 0);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__ControlCoinBlocker_orig(This, index, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__AddCounter(AIO_IOB2_BI2X_TDJ *This,
            int a2, int a3)
    {
        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__AddCounter_orig(This, a2, a3);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetStartLamp(AIO_IOB2_BI2X_TDJ *This,
            int player, uint8_t state)
    {
        auto &lights = get_lights();

        if (player == 0) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::P1_Start], state ? 1.f : 0.f);
        } else if (player == 1) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::P2_Start], state ? 1.f : 0.f);
        }

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetStartLamp_orig(This, player, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp(AIO_IOB2_BI2X_TDJ *This,
            uint8_t state)
    {
        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::VEFX], state ? 1.f : 0.f);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp_orig(This, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp(AIO_IOB2_BI2X_TDJ *This,
            uint8_t state)
    {
        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::Effect], state ? 1.f : 0.f);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp_orig(This, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp(AIO_IOB2_BI2X_TDJ *This,
            int player, int button, uint8_t state)
    {
        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::P1_1 + player * 7 + button], state ? 1.f : 0.f);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp_orig(This, player, button, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetWooferLED(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint32_t color)
    {
        uint32_t col_r = (color & 0xFF0000) >> 16;
        uint32_t col_g = (color & 0x00FF00) >> 8;
        uint32_t col_b = (color & 0x0000FF) >> 0;

        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::WooferR], col_r / 255.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::WooferG], col_g / 255.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::WooferB], col_b / 255.f);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetWooferLED_orig(This, index, color);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetIccrLed(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint32_t color)
    {
        uint32_t col_r = (color & 0xFF0000) >> 16;
        uint32_t col_g = (color & 0x00FF00) >> 8;
        uint32_t col_b = (color & 0x0000FF) >> 0;

        auto &lights = get_lights();

        if (index == 0) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_P1_R], col_r / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_P1_G], col_g / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_P1_B], col_b / 255.f);
        } else if (index == 1) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_P2_R], col_r / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_P2_G], col_g / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_P2_B], col_b / 255.f);
        }

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetIccrLed_orig(This, index, color);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetTurnTableLed(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint32_t color)
    {
        uint32_t col_r = (color & 0xFF0000) >> 16;
        uint32_t col_g = (color & 0x00FF00) >> 8;
        uint32_t col_b = (color & 0x0000FF) >> 0;

        auto &lights = get_lights();

        if (index == 0) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P1_R], col_r / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P1_G], col_g / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P1_B], col_b / 255.f);
        } else if (index == 1) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P2_R], col_r / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P2_G], col_g / 255.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P2_B], col_b / 255.f);
        }

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetTurnTableLed_orig(This, index, color);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetTurnTableResist(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint8_t resistance)
    {
        auto &lights = get_lights();

        if (index == 0) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P1_Resistance], resistance / 255.f);
        } else if (index == 1) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::TT_P2_Resistance], resistance / 255.f);
        }

        if (This != custom_node) {
            return AIO_IOB2_BI2X_TDJ__SetTurnTableResist_orig(This, index, resistance);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_TDJ__SetTapeLedData(AIO_IOB2_BI2X_TDJ *This,
            unsigned int index, uint8_t *data)
    {
        /*
         * index mapping
         *
         * 0 - stage left - 57 bytes - 19 colors
         * 1 - stage right - 57 bytes - 19 colors
         * 2 - cabinet left - 135 bytes - 45 colors
         * 3 - cabinet right - 135 bytes - 45 colors
         * 4 - control panel under - 63 bytes - 21 colors
         * 5 - ceiling left - 162 bytes - 54 colors
         * 6 - title left - 33 bytes - 11 colors
         * 7 - title right - 33 bytes - 11 colors
         * 8 - ceiling right - 162 bytes - 54 colors
         * 9 - touch panel left - 51 bytes - 17 colors
         * 10 - touch panel right - 51 bytes - 17 colors
         * 11 - side panel left inner - 204 bytes - 68 colors
         * 12 - side panel left outer - 204 bytes - 68 colors
         * 13 - side panel left - 183 bytes - 61 colors
         * 14 - side panel right outer - 204 bytes - 68 colors
         * 15 - side panel right inner - 204 bytes - 68 colors
         * 16 - side panel right - 183 bytes - 61 colors
         *
         * data is stored in RGB order, 3 bytes per color
         */

        // check index bounds
        if (tapeledutils::is_enabled() && index < std::size(TAPELED_MAPPING)) {
            auto &map = TAPELED_MAPPING[index];
            const auto data_size = map.data.capacity();

            // pick a color to use
            const auto rgb = tapeledutils::pick_color_from_led_tape(data, data_size);

            // program the lights into API
            auto &lights = get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_r], rgb.r);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_g], rgb.g);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_b], rgb.b);

            for (unsigned int i = 0; i < data_size; ++i) {
                map.data[i].r = data[i * 3];
                map.data[i].g = data[i * 3 + 1];
                map.data[i].b = data[i * 3 + 2];
            }
        }

        if (This != custom_node) {
            // send tape data to real device
            return AIO_IOB2_BI2X_TDJ__SetTapeLedData_orig(This, index, data);
        }
    }

    static AC_HNDLIF* __fastcall aioIob2Bi2x_OpenSciUsbCdc(uint8_t device_num) {
        if (acHndlif == nullptr) {
            acHndlif = new AC_HNDLIF;
            memset(acHndlif->data, 0x0, sizeof(acHndlif->data));
        }
        log_info("bi2x_hook", "aioIob2Bi2x_OpenSciUsbCdc");
        return acHndlif;
    }

    static void __fastcall AIO_NMGR_IOB_BeginManageStub(int64_t a1) {
        log_info("bi2x_hook", "AIO_NMGR_IOB::BeginManage");
    };

    static AIO_NMGR_IOB2* __fastcall aioNMgrIob2_Create(AC_HNDLIF *a1, unsigned int a2) {
        if (aioNmgrIob2 == nullptr) {
            aioNmgrIob2 = new AIO_NMGR_IOB2{};
            aioNmgrIob2->vptr = new AIO_NMGR_IOB2_VTABLE{};
            aioNmgrIob2->vptr->pAIO_NMGR_IOB_BeginManage = AIO_NMGR_IOB_BeginManageStub;
        }
        log_info("bi2x_hook", "aioNMgrIob2_Create returned {}, size=0x{:x}, vptr @ {}",
            fmt::ptr(aioNmgrIob2), sizeof(*aioNmgrIob2), fmt::ptr(aioNmgrIob2->vptr));

        return aioNmgrIob2;
    }

    static int64_t __fastcall aioIob2Bi2x_WriteFirmGetState(int64_t a1) {
        log_info("bi2x_hook", "aioIob2Bi2x_WriteFirmGetState");
        return 8;
    }

    void bi2x_hook_init() {

        // avoid double init
        static bool initialized = false;
        if (initialized) {
            return;
        } else {
            initialized = true;
        }

        // announce
        log_info("bi2x_hook", "init");

        // hook IOB2 video
        const auto libaioIob2VideoDll = "libaio-iob2_video.dll";
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTDJ_Create",
                aioIob2Bi2xTDJ_Create, &aioIob2Bi2xTDJ_Create_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?GetDeviceStatus@AIO_IOB2_BI2X_TDJ@@QEBAXAEAUDEVSTATUS@1@@Z",
                AIO_IOB2_BI2X_TDJ__GetDeviceStatus, &AIO_IOB2_BI2X_TDJ__GetDeviceStatus_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?IoReset@AIO_IOB2_BI2X_TDJ@@QEAAXI@Z",
                AIO_IOB2_BI2X_TDJ__IoReset, &AIO_IOB2_BI2X_TDJ__IoReset_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetWatchDogTimer@AIO_IOB2_BI2X_TDJ@@QEAAXE@Z",
                AIO_IOB2_BI2X_TDJ__SetWatchDogTimer, &AIO_IOB2_BI2X_TDJ__SetWatchDogTimer_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?ControlCoinBlocker@AIO_IOB2_BI2X_TDJ@@QEAAXI_N@Z",
                AIO_IOB2_BI2X_TDJ__ControlCoinBlocker, &AIO_IOB2_BI2X_TDJ__ControlCoinBlocker_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?AddCounter@AIO_IOB2_BI2X_TDJ@@QEAAXII@Z",
                AIO_IOB2_BI2X_TDJ__AddCounter, &AIO_IOB2_BI2X_TDJ__AddCounter_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetStartLamp@AIO_IOB2_BI2X_TDJ@@QEAAXI_N@Z",
                AIO_IOB2_BI2X_TDJ__SetStartLamp, &AIO_IOB2_BI2X_TDJ__SetStartLamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetVefxButtonLamp@AIO_IOB2_BI2X_TDJ@@QEAAX_N@Z",
                AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp, &AIO_IOB2_BI2X_TDJ__SetVefxButtonLamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetEffectButtonLamp@AIO_IOB2_BI2X_TDJ@@QEAAX_N@Z",
                AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp, &AIO_IOB2_BI2X_TDJ__SetEffectButtonLamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetPlayerButtonLamp@AIO_IOB2_BI2X_TDJ@@QEAAXII_N@Z",
                AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp, &AIO_IOB2_BI2X_TDJ__SetPlayerButtonLamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetWooferLed@AIO_IOB2_BI2X_TDJ@@QEAAXI@Z",
                AIO_IOB2_BI2X_TDJ__SetWooferLED, &AIO_IOB2_BI2X_TDJ__SetWooferLED_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetIccrLed@AIO_IOB2_BI2X_TDJ@@QEAAXII@Z",
                AIO_IOB2_BI2X_TDJ__SetIccrLed, &AIO_IOB2_BI2X_TDJ__SetIccrLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetTurnTableLed@AIO_IOB2_BI2X_TDJ@@QEAAXII@Z",
                AIO_IOB2_BI2X_TDJ__SetTurnTableLed, &AIO_IOB2_BI2X_TDJ__SetTurnTableLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetTurnTableResist@AIO_IOB2_BI2X_TDJ@@QEAAXIE@Z",
                AIO_IOB2_BI2X_TDJ__SetTurnTableResist, &AIO_IOB2_BI2X_TDJ__SetTurnTableResist_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetTapeLedData@AIO_IOB2_BI2X_TDJ@@QEAAXIPEBX@Z",
                AIO_IOB2_BI2X_TDJ__SetTapeLedData, &AIO_IOB2_BI2X_TDJ__SetTapeLedData_orig);

        // hook IOB
        const auto libaioIobDll = "libaio-iob.dll";
        detour::trampoline_try(libaioIobDll, "aioIob2Bi2x_OpenSciUsbCdc",
                aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        detour::trampoline_try(libaioIobDll, "aioIob2Bi2x_WriteFirmGetState",
                aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        detour::trampoline_try(libaioIobDll, "aioNMgrIob2_Create",
                aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
    }
}

#endif
