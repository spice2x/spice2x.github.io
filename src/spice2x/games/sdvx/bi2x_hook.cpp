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
#include "games/sdvx/sdvx.h"
#include "util/tapeled.h"
#include "acioemu/icca.h"

namespace games::sdvx {
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

    // confirmed in EG final in aioNMgrIob2_Create
    static_assert(sizeof(AIO_NMGR_IOB2) == 0x9F8);

    struct AIO_IOB2_BI2X_UFC {
        // who knows
        uint8_t data[0x39D8];
    };

    // confirmed in EG final in aioIob2Bi2xUFC_Create
    static_assert(sizeof(AIO_IOB2_BI2X_UFC) == 0x39D8);

    struct AIO_IOB2_BI2X_UFC__DEVSTATUS {
        // of course you could work with variables here
        uint8_t buffer[0x19E];
    };

    /*
     * typedefs
     */

    // libaio-iob2_video.dll
    typedef AIO_IOB2_BI2X_UFC* (__fastcall *aioIob2Bi2xUFC_Create_t)(AIO_NMGR_IOB2 *nmgr, int a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__GetDeviceStatus_t)(AIO_IOB2_BI2X_UFC *This,
            AIO_IOB2_BI2X_UFC__DEVSTATUS *a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__IoReset_t)(AIO_IOB2_BI2X_UFC *This,
            unsigned int a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__SetWatchDogTimer_t)(AIO_IOB2_BI2X_UFC *This, uint8_t a2);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__ControlCoinBlocker_t)(AIO_IOB2_BI2X_UFC *This,
            uint64_t index, uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__AddCounter_t)(AIO_IOB2_BI2X_UFC *This,
            unsigned int a2, unsigned int a3);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__SetIccrLed_t)(AIO_IOB2_BI2X_UFC *This, uint32_t color);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp_t)(AIO_IOB2_BI2X_UFC *This,
            int button, uint8_t state);
    typedef void (__fastcall *AIO_IOB2_BI2X_UFC__SetTapeLedData_t)(AIO_IOB2_BI2X_UFC *This,
            unsigned int index, uint8_t *data);

    // libaio-iob.dll
    typedef AC_HNDLIF* (__fastcall *aioIob2Bi2x_OpenSciUsbCdc_t)(uint8_t device_num);
    typedef int64_t (__fastcall *aioIob2Bi2x_WriteFirmGetState_t)(int64_t a1);
    typedef AIO_NMGR_IOB2* (__fastcall *aioNMgrIob2_Create_t)(AC_HNDLIF *a1, unsigned int a2);

    /*
     * function pointers
     */

    // libaio-iob2_video.dll
    static aioIob2Bi2xUFC_Create_t aioIob2Bi2xUFC_Create_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__GetDeviceStatus_t AIO_IOB2_BI2X_UFC__GetDeviceStatus_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__IoReset_t AIO_IOB2_BI2X_UFC__IoReset_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__SetWatchDogTimer_t AIO_IOB2_BI2X_UFC__SetWatchDogTimer_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__ControlCoinBlocker_t AIO_IOB2_BI2X_UFC__ControlCoinBlocker_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__AddCounter_t AIO_IOB2_BI2X_UFC__AddCounter_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__SetIccrLed_t AIO_IOB2_BI2X_UFC__SetIccrLed_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp_t AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp_orig = nullptr;
    static AIO_IOB2_BI2X_UFC__SetTapeLedData_t AIO_IOB2_BI2X_UFC__SetTapeLedData_orig = nullptr;

    // libaio-iob.dll
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;

    /*
     * variables
     */

    AIO_IOB2_BI2X_UFC *custom_node = nullptr;
    AC_HNDLIF *acHndlif = nullptr;
    AIO_NMGR_IOB2 *aioNmgrIob2 = nullptr;
    // state
    static uint8_t count = 0;
    static uint16_t VOL_L = 0;
    static uint16_t VOL_R = 0;

    /*
     * implementations
     */

    static AIO_IOB2_BI2X_UFC* __fastcall aioIob2Bi2xUFC_Create(AIO_NMGR_IOB2 *nmgr, int a2) {
        if (!BI2X_PASSTHROUGH) {
            custom_node = new AIO_IOB2_BI2X_UFC;
            memset(&custom_node->data, 0, sizeof(custom_node->data));
            return custom_node;
        } else {

            // call original
            return aioIob2Bi2xUFC_Create_orig(nmgr, a2);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__GetDeviceStatus(AIO_IOB2_BI2X_UFC *This,
            AIO_IOB2_BI2X_UFC__DEVSTATUS *status) {

        // flush raw input
        RI_MGR->devices_flush_output();

        // check handle
        if (This == custom_node) {

            // clear input data
            memset(status, 0x00, sizeof(AIO_IOB2_BI2X_UFC__DEVSTATUS));
        } else {

            // get data from real device
            AIO_IOB2_BI2X_UFC__GetDeviceStatus_orig(This, status);
        }

        status->buffer[0] = count;
        status->buffer[12] = count;
        count++;

        // get buttons
        auto &buttons = get_buttons();

        // control buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]))
            status->buffer[18] = 0x01;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]))
            status->buffer[19] = 0x01;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]))
            status->buffer[20] = 0x01;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Start]))
            status->buffer[316] |= 0x01;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::BT_A]))
            status->buffer[316] |= 0x02;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::BT_B]))
            status->buffer[316] |= 0x04;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::BT_C]))
            status->buffer[316] |= 0x08;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::BT_D]))
            status->buffer[316] |= 0x10;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::FX_L]))
            status->buffer[316] |= 0x20;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::FX_R]))
            status->buffer[316] |= 0x40;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Headphone]))
            status->buffer[22] = 0x01;

        // volume left
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::VOL_L_Left])) {
            VOL_L -= ((uint16_t)DIGITAL_KNOB_SENS * 4);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::VOL_L_Right])) {
            VOL_L += ((uint16_t)DIGITAL_KNOB_SENS * 4);
        }

        // volume right
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::VOL_R_Left])) {
            VOL_R -= ((uint16_t)DIGITAL_KNOB_SENS * 4);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::VOL_R_Right])) {
            VOL_R += ((uint16_t)DIGITAL_KNOB_SENS * 4);
        }

        // update volumes
        auto &analogs = get_analogs();
        auto vol_left = VOL_L;
        auto vol_right = VOL_R;
        if (analogs[0].isSet() || analogs[1].isSet()) {
            vol_left += (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::VOL_L]) * 65535);
            vol_right += (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::VOL_R]) * 65535);
        }

        *((uint16_t*) &status->buffer[312]) = vol_left;
        *((uint16_t*) &status->buffer[314]) = vol_right;
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__IoReset(AIO_IOB2_BI2X_UFC *This,
            unsigned int a2)
    {
        if (This != custom_node) {
            return AIO_IOB2_BI2X_UFC__IoReset_orig(This, a2);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__SetWatchDogTimer(AIO_IOB2_BI2X_UFC *This,
            uint8_t a2)
    {
        if (This != custom_node) {

            // comment this out if you want to disable the BI2X watchdog timer
            return AIO_IOB2_BI2X_UFC__SetWatchDogTimer_orig(This, a2);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__ControlCoinBlocker(AIO_IOB2_BI2X_UFC *This,
            uint64_t index, uint8_t state) {

        // coin blocker is closed when state is zero
        eamuse_coin_set_block(state == 0);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_UFC__ControlCoinBlocker_orig(This, index, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__AddCounter(AIO_IOB2_BI2X_UFC *This,
            unsigned int a2, unsigned int a3)
    {
        if (This != custom_node) {
            return AIO_IOB2_BI2X_UFC__AddCounter_orig(This, a2, a3);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__SetIccrLed(AIO_IOB2_BI2X_UFC *This, uint32_t color)
    {
        uint32_t col_r = (color & 0xFF0000) >> 16;
        uint32_t col_g = (color & 0x00FF00) >> 8;
        uint32_t col_b = (color & 0x0000FF) >> 0;

        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_R], col_r / 255.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_G], col_g / 255.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[Lights::ICCR_B], col_b / 255.f);

        if (This != custom_node) {
            return AIO_IOB2_BI2X_UFC__SetIccrLed_orig(This, color);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp(AIO_IOB2_BI2X_UFC *This,
            int button, uint8_t state)
    {
        auto &lights = get_lights();

        /**
         * button
         * 0: START, 1: BT_A, 2: BT_B, 3: BT_C, 4: BT_D, 5: FX_L, 6: FX_R
         *
         * state
         * 0: ON, 1: OFF
         */
        if (button == 0) {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::START], state ? 1.f : 0.f);
        } else {
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::BT_A + button - 1], state ? 1.f : 0.f);
        }

        if (This != custom_node) {
            return AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp_orig(This, button, state);
        }
    }

    static void __fastcall AIO_IOB2_BI2X_UFC__SetTapeLedData(AIO_IOB2_BI2X_UFC *This,
            unsigned int index, uint8_t *data)
    {
        /*
         * index mapping
         * 0 - title - 222 bytes - 74 colors
         * 1 - upper left speaker - 36 bytes - 12 colors
         * 2 - upper right speaker - 36 bytes - 12 colors
         * 3 - left wing - 168 bytes - 56 colors
         * 4 - right wing - 168 bytes - 56 colors
         * 5 - control panel - 282 bytes - 94 colors
         * 6 - lower left speaker - 36 bytes - 12 colors
         * 7 - lower right speaker - 36 bytes - 12 colors
         * 8 - woofer - 42 bytes - 14 colors
         * 9 - v unit - 258 bytes - 86 colors
         *
         * data is stored in RGB order, 3 bytes per color
         *
         * TODO: expose this data via API
         */

        // data mapping
        static struct TapeLedMapping {
            size_t data_size;
            int index_r, index_g, index_b;

            TapeLedMapping(size_t data_size, int index_r, int index_g, int index_b)
                    : data_size(data_size), index_r(index_r), index_g(index_g), index_b(index_b) {}

        } mapping[] = {
                { 74, Lights::TITLE_AVG_R, Lights::TITLE_AVG_G, Lights::TITLE_AVG_B },
                { 12, Lights::UPPER_LEFT_SPEAKER_AVG_R, Lights::UPPER_LEFT_SPEAKER_AVG_G, Lights::UPPER_LEFT_SPEAKER_AVG_B },
                { 12, Lights::UPPER_RIGHT_SPEAKER_AVG_R, Lights::UPPER_RIGHT_SPEAKER_AVG_G, Lights::UPPER_RIGHT_SPEAKER_AVG_B },
                { 56, Lights::LEFT_WING_AVG_R, Lights::LEFT_WING_AVG_G, Lights::LEFT_WING_AVG_B },
                { 56, Lights::RIGHT_WING_AVG_R, Lights::RIGHT_WING_AVG_G, Lights::RIGHT_WING_AVG_B },
                { 94, Lights::CONTROL_PANEL_AVG_R, Lights::CONTROL_PANEL_AVG_G, Lights::CONTROL_PANEL_AVG_B },
                { 12, Lights::LOWER_LEFT_SPEAKER_AVG_R, Lights::LOWER_LEFT_SPEAKER_AVG_G, Lights::LOWER_LEFT_SPEAKER_AVG_B },
                { 12, Lights::LOWER_RIGHT_SPEAKER_AVG_R, Lights::LOWER_RIGHT_SPEAKER_AVG_G, Lights::LOWER_RIGHT_SPEAKER_AVG_B },
                { 14, Lights::WOOFER_AVG_R, Lights::WOOFER_AVG_G, Lights::WOOFER_AVG_B },
                { 86, Lights::V_UNIT_AVG_R, Lights::V_UNIT_AVG_G, Lights::V_UNIT_AVG_B },
        };

        // check index bounds
        if (tapeledutils::is_enabled() && index < std::size(mapping)) {
            auto &map = mapping[index];

            // pick a color to use
            const auto rgb = tapeledutils::pick_color_from_led_tape(data, map.data_size);

            // program the lights into API
            auto &lights = get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_r], rgb.r);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_g], rgb.g);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_b], rgb.b);
        }

        if (This != custom_node) {
            // send tape data to real device
            return AIO_IOB2_BI2X_UFC__SetTapeLedData_orig(This, index, data);
        }
    }

    static AC_HNDLIF* __fastcall aioIob2Bi2X_OpenSciUsbCdc(uint8_t device_num) {
        if (acHndlif == nullptr) {
            acHndlif = new AC_HNDLIF;
            memset(acHndlif->data, 0x0, sizeof(acHndlif->data));
        }
        log_info("bi2x_hook", "aioIob2Bi2x_OpenSciUsbCdc");
        return acHndlif;
    }

    static void __fastcall AIO_NMGR_IOB_BeginManageStub(int64_t a1) {
        log_info("bi2x_hook", "AIO_NMGR_IOB::BeginManage");
    }

    static AIO_NMGR_IOB2* __fastcall aioNMgrIob2_Create(AC_HNDLIF *a1, unsigned int a2) {
        if (aioNmgrIob2 == nullptr) {
            aioNmgrIob2 = new AIO_NMGR_IOB2{};
            aioNmgrIob2->vptr = new AIO_NMGR_IOB2_VTABLE{};
            aioNmgrIob2->vptr->pAIO_NMGR_IOB_BeginManage = AIO_NMGR_IOB_BeginManageStub;
        }
        log_info("bi2x_hook", "aioNMgrIob2_Create returned {}, size=0x{:x}, vptr @ {}",
            fmt::ptr(aioNmgrIob2), sizeof(*aioNmgrIob2), fmt::ptr(aioNmgrIob2->vptr));

        // enable hack to make PIN pad work for KFC in BI2X mode
        // this explicit check in the I/O init path is necessary
        // (as opposed to just doing a check for "isValkyrieCabMode?")
        // because there are hex edits that allow you to use legacy (KFC/BIO2) IO while in Valk mode
        acioemu::ICCA_DEVICE_HACK = true;
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
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xUFC_Create",
                aioIob2Bi2xUFC_Create, &aioIob2Bi2xUFC_Create_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?GetDeviceStatus@AIO_IOB2_BI2X_UFC@@QEBAXAEAUDEVSTATUS@1@@Z",
                AIO_IOB2_BI2X_UFC__GetDeviceStatus, &AIO_IOB2_BI2X_UFC__GetDeviceStatus_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?IoReset@AIO_IOB2_BI2X_UFC@@QEAAXI@Z",
                AIO_IOB2_BI2X_UFC__IoReset, &AIO_IOB2_BI2X_UFC__IoReset_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetWatchDogTimer@AIO_IOB2_BI2X_UFC@@QEAAXE@Z",
                AIO_IOB2_BI2X_UFC__SetWatchDogTimer, &AIO_IOB2_BI2X_UFC__SetWatchDogTimer_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?ControlCoinBlocker@AIO_IOB2_BI2X_UFC@@QEAAXI_N@Z",
                AIO_IOB2_BI2X_UFC__ControlCoinBlocker, &AIO_IOB2_BI2X_UFC__ControlCoinBlocker_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?AddCounter@AIO_IOB2_BI2X_UFC@@QEAAXII@Z",
                AIO_IOB2_BI2X_UFC__AddCounter, &AIO_IOB2_BI2X_UFC__AddCounter_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetIccrLed@AIO_IOB2_BI2X_UFC@@QEAAXI@Z",
                AIO_IOB2_BI2X_UFC__SetIccrLed, &AIO_IOB2_BI2X_UFC__SetIccrLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetPlayerButtonLamp@AIO_IOB2_BI2X_UFC@@QEAAXI_N@Z",
                AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp, &AIO_IOB2_BI2X_UFC__SetPlayerButtonLamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?SetTapeLedData@AIO_IOB2_BI2X_UFC@@QEAAXIPEBX@Z",
                AIO_IOB2_BI2X_UFC__SetTapeLedData, &AIO_IOB2_BI2X_UFC__SetTapeLedData_orig);

        // hook IOB
        const auto libaioIobDll = "libaio-iob.dll";
        detour::trampoline_try(libaioIobDll, "aioIob2Bi2x_OpenSciUsbCdc",
                aioIob2Bi2X_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        detour::trampoline_try(libaioIobDll, "aioIob2Bi2x_WriteFirmGetState",
                aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        detour::trampoline_try(libaioIobDll, "aioNMgrIob2_Create",
                aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
    }
}

#endif