#include "bi2x_hook.h"

#include <cstdint>
#include "util/detour.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/io.h"
#include "io.h"
#include "util/tapeled.h"

namespace games::ccj {

    /*
     * class definitions
     */

    struct AIO_SCI_COMM {
    };

    struct AIO_NMGR_IOB2 {
    };

    struct AIO_IOB2_BI2X_TBS {
    };

    struct AIO_IOB2_BI2X_WRFIRM {
    };

    struct AIO_NMGR_IOB__NODEINFO {
        uint8_t data[0xA3];
    };

    struct AIO_IOB2_BI2X_AC1__INPUTDATA {
        uint8_t data[247];
    };

    struct AIO_IOB2_BI2X_AC1__OUTPUTDATA {
        uint8_t data[48];
    };

    struct AIO_IOB2_BI2X_TBS__INPUT {
        uint8_t DevIoCounter;
        uint8_t bExIoAErr;
        uint8_t bExIoBErr;
        uint8_t bPcPowerOn;
        uint8_t bPcPowerCheck;
        uint8_t CoinCount;
        uint8_t bTest;
        uint8_t bService;
        uint8_t bCoinSw;
        uint8_t bCoinJam;
        uint8_t bHPDetect;
        uint16_t StickY;
        uint16_t StickX;
        uint8_t bStickBtn;
        uint8_t bTrigger1;
        uint8_t bTrigger2;
        uint8_t bButton0;
        uint8_t bButton1;
        uint8_t bButton2;
        uint8_t bButton3;
    };

    struct AIO_IOB2_BI2X_TBS__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        uint8_t IoResetCounter;
        uint8_t TapeLedCounter;
        AIO_IOB2_BI2X_TBS__INPUT Input;
        AIO_IOB2_BI2X_AC1__INPUTDATA InputData;
        AIO_IOB2_BI2X_AC1__OUTPUTDATA OutputData;
    };

    static void write_iccr_led(Lights::ccj_lights_t light, uint8_t value);

    /*
     * typedefs
     */

    // libaio-iob2_video.dll
    typedef AIO_IOB2_BI2X_TBS* (__fastcall *aioIob2Bi2xTBS_Create_t)(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId);
    typedef void (__fastcall *aioIob2Bi2xTBS_GetDeviceStatus_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl,
                                                                AIO_IOB2_BI2X_TBS__DEVSTATUS *o_DevStatus);
    typedef void (__fastcall *aioIob2Bi2xTBS_IoReset_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_bfIoReset);
    typedef void (__fastcall *aioIob2Bi2xAC1_IoReset_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_bfIoReset);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetWatchDogTimer_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint8_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetWatchDogTimer_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint8_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xTBS_ControlCoinBlocker_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_Slot,
                                                                   bool i_bOpen);
    typedef void (__fastcall *aioIob2Bi2xTBS_AddCounter_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_Counter,
                                                           uint32_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetAmpVolume_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_Amp,
                                                             uint32_t i_Volume);
    typedef void (__fastcall *aioIob2Bi2xTBS_EnableUsbCharger_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bEnable);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetIrLed_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetButton0Lamp_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetIccrLed_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetStickLed_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetTapeLedData_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_TapeLed, uint8_t *i_pData);
    typedef AIO_SCI_COMM* (__fastcall *aioIob2Bi2x_OpenSciUsbCdc_t)(uint32_t i_SerialNumber);
    typedef AIO_IOB2_BI2X_WRFIRM* (__fastcall *aioIob2Bi2x_CreateWriteFirmContext_t)(uint32_t i_SerialNumber,
                                                                                     uint32_t i_bfIob);
    typedef void (__fastcall *aioIob2Bi2x_DestroyWriteFirmContext_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef int32_t (__fastcall *aioIob2Bi2x_WriteFirmGetState_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef bool (__fastcall *aioIob2Bi2x_WriteFirmIsCompleted_t)(int32_t i_State);
    typedef bool (__fastcall *aioIob2Bi2x_WriteFirmIsError_t)(int32_t i_State);

    // libaio-iob.dll
    typedef AIO_NMGR_IOB2* (__fastcall *aioNMgrIob2_Create_t)(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode);
    typedef void (__fastcall *aioNMgrIob_BeginManage_t)(AIO_NMGR_IOB2 *i_pNodeMgr);
    typedef void (__fastcall *aioNCtlIob_GetNodeInfo_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl,
                                                        AIO_NMGR_IOB__NODEINFO *o_NodeInfo);

    // libaio.dll
    typedef void (__fastcall *aioNodeMgr_Destroy_t)(AIO_NMGR_IOB2 *i_pNodeMgr);
    typedef int32_t (__fastcall *aioNodeMgr_GetState_t)(AIO_NMGR_IOB2 *i_pNodeMgr);
    typedef bool (__fastcall *aioNodeMgr_IsReady_t)(AIO_NMGR_IOB2 *i_pNodeMgr, int32_t i_State);
    typedef bool (__fastcall *aioNodeMgr_IsError_t)(AIO_NMGR_IOB2 *i_pNodeMgr, int32_t i_State);
    typedef void (__fastcall *aioNodeCtl_Destroy_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl);
    typedef int32_t (__fastcall *aioNodeCtl_GetState_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl);
    typedef bool (__fastcall *aioNodeCtl_IsReady_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, int32_t i_State);
    typedef bool (__fastcall *aioNodeCtl_IsError_t)(AIO_IOB2_BI2X_TBS *i_pNodeCtl, int32_t i_State);

    /*
     * function pointers
     */

    // libaio-iob2_video.dll
    static aioIob2Bi2xTBS_Create_t aioIob2Bi2xTBS_Create_orig = nullptr;
    static aioIob2Bi2xTBS_GetDeviceStatus_t aioIob2Bi2xTBS_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xTBS_IoReset_t aioIob2Bi2xTBS_IoReset_orig = nullptr;
    static aioIob2Bi2xAC1_IoReset_t aioIob2Bi2xAC1_IoReset_orig = nullptr;
    static aioIob2Bi2xTBS_SetWatchDogTimer_t aioIob2Bi2xTBS_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xAC1_SetWatchDogTimer_t aioIob2Bi2xAC1_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xTBS_ControlCoinBlocker_t aioIob2Bi2xTBS_ControlCoinBlocker_orig = nullptr;
    static aioIob2Bi2xTBS_AddCounter_t aioIob2Bi2xTBS_AddCounter_orig = nullptr;
    static aioIob2Bi2xTBS_SetAmpVolume_t aioIob2Bi2xTBS_SetAmpVolume_orig = nullptr;
    static aioIob2Bi2xTBS_EnableUsbCharger_t aioIob2Bi2xTBS_EnableUsbCharger_orig = nullptr;
    static aioIob2Bi2xTBS_SetIrLed_t aioIob2Bi2xTBS_SetIrLed_orig = nullptr;
    static aioIob2Bi2xTBS_SetButton0Lamp_t aioIob2Bi2xTBS_SetButton0Lamp_orig = nullptr;
    static aioIob2Bi2xTBS_SetIccrLed_t aioIob2Bi2xTBS_SetIccrLed_orig = nullptr;
    static aioIob2Bi2xTBS_SetStickLed_t aioIob2Bi2xTBS_SetStickLed_orig = nullptr;
    static aioIob2Bi2xTBS_SetTapeLedData_t aioIob2Bi2xTBS_SetTapeLedData_orig = nullptr;
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_DestroyWriteFirmContext_t aioIob2Bi2x_DestroyWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsCompleted_t aioIob2Bi2x_WriteFirmIsCompleted_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsError_t aioIob2Bi2x_WriteFirmIsError_orig = nullptr;

    // libaio-iob.dll
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;
    static aioNMgrIob_BeginManage_t aioNMgrIob_BeginManage_orig = nullptr;
    static aioNCtlIob_GetNodeInfo_t aioNCtlIob_GetNodeInfo_orig = nullptr;

    // libaio.dll
    static aioNodeMgr_Destroy_t aioNodeMgr_Destroy_orig = nullptr;
    static aioNodeMgr_GetState_t aioNodeMgr_GetState_orig = nullptr;
    static aioNodeMgr_IsReady_t aioNodeMgr_IsReady_orig = nullptr;
    static aioNodeMgr_IsError_t aioNodeMgr_IsError_orig = nullptr;
    static aioNodeCtl_Destroy_t aioNodeCtl_Destroy_orig = nullptr;
    static aioNodeCtl_GetState_t aioNodeCtl_GetState_orig = nullptr;
    static aioNodeCtl_IsReady_t aioNodeCtl_IsReady_orig = nullptr;
    static aioNodeCtl_IsError_t aioNodeCtl_IsError_orig = nullptr;

    /*
     * variables
     */

    static AIO_SCI_COMM *aioSciComm;
    static AIO_NMGR_IOB2 *aioNmgrIob2;
    static AIO_IOB2_BI2X_TBS *aioIob2Bi2xTbs;
    static AIO_IOB2_BI2X_WRFIRM *aioIob2Bi2xWrfirm;

    static uint8_t count = 0;

    /*
     * implementations
     */

    static AIO_IOB2_BI2X_TBS* __fastcall aioIob2Bi2xTBS_Create(
        AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId) {

        if (i_pNodeMgr == aioNmgrIob2) {
            log_info("bi2x_hook", "node created");
            aioIob2Bi2xTbs = new AIO_IOB2_BI2X_TBS;
            return aioIob2Bi2xTbs;
        } else {
            return aioIob2Bi2xTBS_Create_orig(i_pNodeMgr, i_DevId);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_GetDeviceStatus(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, AIO_IOB2_BI2X_TBS__DEVSTATUS *o_DevStatus) {

        RI_MGR->devices_flush_output();

        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus);
        }

        memset(o_DevStatus, 0x00, sizeof(AIO_IOB2_BI2X_TBS__DEVSTATUS));

        o_DevStatus->Input.DevIoCounter = count;
        count++;

        o_DevStatus->Input.StickX = 32768;
        o_DevStatus->Input.StickY = 32768;

        auto &analogs = get_analogs();
        if (analogs[Analogs::Joystick_X].isSet() || analogs[Analogs::Joystick_Y].isSet()) {
            o_DevStatus->Input.StickX =
                65535 - (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::Joystick_X]) * 65535);
            o_DevStatus->Input.StickY =
                65535 - (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::Joystick_Y]) * 65535);
        }

        auto &buttons = get_buttons();
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]))
            o_DevStatus->Input.bTest = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]))
            o_DevStatus->Input.bService = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]))
            o_DevStatus->Input.bCoinSw = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Joystick_Up]))
            o_DevStatus->Input.StickY = 65535;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Joystick_Down]))
            o_DevStatus->Input.StickY = 0;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Joystick_Left]))
            o_DevStatus->Input.StickX = 65535;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Joystick_Right]))
            o_DevStatus->Input.StickX = 0;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button_Dash]))
            o_DevStatus->Input.bTrigger1 = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button_Special]))
            o_DevStatus->Input.bButton0 = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button_Action]))
            o_DevStatus->Input.bButton1 = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button_Jump]))
            o_DevStatus->Input.bButton2 = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button_Slide]))
            o_DevStatus->Input.bButton3 = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Headphones]))
            o_DevStatus->Input.bHPDetect = 1;

        o_DevStatus->Input.CoinCount = eamuse_coin_get_stock();
    }

    static void __fastcall aioIob2Bi2xAC1_IoReset(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_bfIoReset) {

        if (i_pNodeCtl == aioIob2Bi2xTbs) {
        } else {
            return aioIob2Bi2xAC1_IoReset_orig(i_pNodeCtl, i_bfIoReset);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_IoReset(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_bfIoReset) {

        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            return aioIob2Bi2xAC1_IoReset(i_pNodeCtl, i_bfIoReset);
        } else {
            return aioIob2Bi2xTBS_IoReset_orig(i_pNodeCtl, i_bfIoReset);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_SetWatchDogTimer(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint8_t i_Count) {

        if (i_pNodeCtl == aioIob2Bi2xTbs) {
        } else {
            return aioIob2Bi2xAC1_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetWatchDogTimer(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint8_t i_Count) {

        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            return aioIob2Bi2xAC1_SetWatchDogTimer(i_pNodeCtl, i_Count);
        } else {
            return aioIob2Bi2xTBS_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_ControlCoinBlocker(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_Slot, bool i_bOpen) {

        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            eamuse_coin_set_block(!i_bOpen);
        } else {
            return aioIob2Bi2xTBS_ControlCoinBlocker_orig(i_pNodeCtl, i_Slot, i_bOpen);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_AddCounter(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {

        if (i_pNodeCtl == aioIob2Bi2xTbs && i_Count == 0) {
            eamuse_coin_set_stock((uint16_t) i_Count);
        } else {
            return aioIob2Bi2xTBS_AddCounter_orig(i_pNodeCtl, i_Counter, i_Count);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetAmpVolume(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_Amp, uint32_t i_Volume) {

        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_SetAmpVolume_orig(i_pNodeCtl, i_Amp, i_Volume);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_EnableUsbCharger(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bEnable) {

        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_EnableUsbCharger_orig(i_pNodeCtl, i_bEnable);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetIrLed(AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn) {
        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_SetIrLed_orig(i_pNodeCtl, i_bOn);
        }

        // handle ir led
    }

    static void __fastcall aioIob2Bi2xTBS_SetButton0Lamp(AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn) {
        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_SetButton0Lamp_orig(i_pNodeCtl, i_bOn);
        }

        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::SpecialButton), (i_bOn ? 1.f : 0.f));
    }

    static void write_iccr_led(Lights::ccj_lights_t light, uint8_t value) {
        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(light), value / 255);
    }

    static void __fastcall aioIob2Bi2xTBS_SetIccrLed(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB) {
        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_SetIccrLed_orig(i_pNodeCtl, i_RGB);
        }

        write_iccr_led(Lights::CardReader_B, i_RGB);
        write_iccr_led(Lights::CardReader_G, i_RGB >> 8);
        write_iccr_led(Lights::CardReader_R, i_RGB >> 16);
    }

    static void __fastcall aioIob2Bi2xTBS_SetStickLed(AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB) {
        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_SetStickLed_orig(i_pNodeCtl, i_RGB);
        }

        // handle stick led
    }

    static void __fastcall aioIob2Bi2xTBS_SetTapeLedData(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_TapeLed, uint8_t *i_pData) {

        if (i_pNodeCtl != aioIob2Bi2xTbs) {
            return aioIob2Bi2xTBS_SetTapeLedData_orig(i_pNodeCtl, i_TapeLed, i_pData);
        }

        /*
         * index mapping
         * 0 - title panel - 144 bytes - 48 colors
         * 1 - side panel - 147 bytes - 49 colors
         *
         * data is stored in RGB order, 3 bytes per color
         *
         * TODO: expose this data via API
         */
        static struct TapeLedMapping {
            size_t data_size;
            int r, g, b;

            TapeLedMapping(size_t data_size, int r, int g, int b)
                : data_size(data_size), r(r), g(g), b(b) {}

        } mapping[] = {
            { 48, Lights::TitlePanel_R, Lights::TitlePanel_G, Lights::TitlePanel_B },
            { 49, Lights::SidePanel_R, Lights::SidePanel_G, Lights::SidePanel_B },
        };

        if (tapeledutils::is_enabled() && i_TapeLed < std::size(mapping)) {
            auto &map = mapping[i_TapeLed];

            // pick a color to use
            const auto rgb = tapeledutils::pick_color_from_led_tape(i_pData, map.data_size);

            // program the lights into API
            auto &lights = get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights[map.r], rgb.r);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.g], rgb.g);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.b], rgb.b);
        }
    }

    static AIO_SCI_COMM* __fastcall aioIob2Bi2x_OpenSciUsbCdc(uint32_t i_SerialNumber) {
        aioSciComm = new AIO_SCI_COMM;
        return aioSciComm;
    }

    static AIO_IOB2_BI2X_WRFIRM* __fastcall aioIob2Bi2x_CreateWriteFirmContext(
        uint32_t i_SerialNumber, uint32_t i_bfIob) {

        aioIob2Bi2xWrfirm = new AIO_IOB2_BI2X_WRFIRM;
        return aioIob2Bi2xWrfirm;
    }

    static void __fastcall aioIob2Bi2x_DestroyWriteFirmContext(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm) {
        if (i_pWrFirm == aioIob2Bi2xWrfirm) {
            delete aioIob2Bi2xWrfirm;
            aioIob2Bi2xWrfirm = nullptr;
        } else {
            return aioIob2Bi2x_DestroyWriteFirmContext_orig(i_pWrFirm);
        }
    }

    static int32_t __fastcall aioIob2Bi2x_WriteFirmGetState(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm) {
        if (i_pWrFirm == aioIob2Bi2xWrfirm) {
            return 8;
        } else {
            return aioIob2Bi2x_WriteFirmGetState_orig(i_pWrFirm);
        }
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsCompleted(int32_t i_State) {
        if (aioIob2Bi2xWrfirm != nullptr)
            return true;
        return aioIob2Bi2x_WriteFirmIsCompleted_orig(i_State);
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsError(int32_t i_State) {
        if (aioIob2Bi2xWrfirm != nullptr)
            return false;
        return aioIob2Bi2x_WriteFirmIsError_orig(i_State);
    }

    static AIO_NMGR_IOB2* __fastcall aioNMgrIob2_Create(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode) {
        if (i_pSci == aioSciComm) {
            aioNmgrIob2 = new AIO_NMGR_IOB2;
            return aioNmgrIob2;
        } else {
            return aioNMgrIob2_Create_orig(i_pSci, i_bfMode);
        }
    }

    static void __fastcall aioNMgrIob_BeginManage(AIO_NMGR_IOB2 *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob2) {
        } else {
            return aioNMgrIob_BeginManage_orig(i_pNodeMgr);
        }
    }

    static void __fastcall aioNCtlIob_GetNodeInfo(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo) {

        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            memset(o_NodeInfo, 0, sizeof(AIO_NMGR_IOB__NODEINFO));
        } else {
            return aioNCtlIob_GetNodeInfo_orig(i_pNodeCtl, o_NodeInfo);
        }
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB2 *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob2) {
            delete aioNmgrIob2;
            aioNmgrIob2 = nullptr;
        } else {
            return aioNodeMgr_Destroy_orig(i_pNodeMgr);
        }
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB2 *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob2) {
            return 1;
        } else {
            return aioNodeMgr_GetState_orig(i_pNodeMgr);
        }
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB2 *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob2) {
            return true;
        } else {
            return aioNodeMgr_IsReady_orig(i_pNodeMgr, i_State);
        }
    }

    static bool __fastcall aioNodeMgr_IsError(AIO_NMGR_IOB2 *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob2) {
            return false;
        } else {
            return aioNodeMgr_IsError_orig(i_pNodeMgr, i_State);
        }
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB2_BI2X_TBS *i_pNodeCtl) {
        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            delete aioIob2Bi2xTbs;
            aioIob2Bi2xTbs = nullptr;
        } else {
            return aioNodeCtl_Destroy_orig(i_pNodeCtl);
        }
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB2_BI2X_TBS *i_pNodeCtl) {
        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            return 1;
        } else {
            return aioNodeCtl_GetState_orig(i_pNodeCtl);
        }
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB2_BI2X_TBS *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            return true;
        } else {
            return aioNodeCtl_IsReady_orig(i_pNodeCtl, i_State);
        }
    }

    static bool __fastcall aioNodeCtl_IsError(AIO_IOB2_BI2X_TBS *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob2Bi2xTbs) {
            return false;
        } else {
            return aioNodeCtl_IsError_orig(i_pNodeCtl, i_State);
        }
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

        // libaio-iob2_video.dll
        const auto libaioIob2VideoDll = "libaio-iob2_video.dll";
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_Create",
                               aioIob2Bi2xTBS_Create, &aioIob2Bi2xTBS_Create_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_GetDeviceStatus",
                               aioIob2Bi2xTBS_GetDeviceStatus, &aioIob2Bi2xTBS_GetDeviceStatus_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_IoReset",
                               aioIob2Bi2xTBS_IoReset, &aioIob2Bi2xTBS_IoReset_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_IoReset",
                               aioIob2Bi2xAC1_IoReset, &aioIob2Bi2xAC1_IoReset_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetWatchDogTimer",
                               aioIob2Bi2xTBS_SetWatchDogTimer, &aioIob2Bi2xTBS_SetWatchDogTimer_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetWatchDogTimer",
                               aioIob2Bi2xAC1_SetWatchDogTimer, &aioIob2Bi2xAC1_SetWatchDogTimer_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_ControlCoinBlocker",
                               aioIob2Bi2xTBS_ControlCoinBlocker, &aioIob2Bi2xTBS_ControlCoinBlocker_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_AddCounter",
                               aioIob2Bi2xTBS_AddCounter, &aioIob2Bi2xTBS_AddCounter_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetAmpVolume",
                               aioIob2Bi2xTBS_SetAmpVolume, &aioIob2Bi2xTBS_SetAmpVolume_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_EnableUsbCharger",
                               aioIob2Bi2xTBS_EnableUsbCharger, &aioIob2Bi2xTBS_EnableUsbCharger_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetIrLed",
                               aioIob2Bi2xTBS_SetIrLed, &aioIob2Bi2xTBS_SetIrLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetButton0Lamp",
                               aioIob2Bi2xTBS_SetButton0Lamp, &aioIob2Bi2xTBS_SetButton0Lamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetIccrLed",
                               aioIob2Bi2xTBS_SetIccrLed, &aioIob2Bi2xTBS_SetIccrLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetStickLed",
                               aioIob2Bi2xTBS_SetStickLed, &aioIob2Bi2xTBS_SetStickLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetTapeLedData",
                               aioIob2Bi2xTBS_SetTapeLedData, &aioIob2Bi2xTBS_SetTapeLedData_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_OpenSciUsbCdc",
                               aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_CreateWriteFirmContext",
                               aioIob2Bi2x_CreateWriteFirmContext, &aioIob2Bi2x_CreateWriteFirmContext_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_DestroyWriteFirmContext",
                               aioIob2Bi2x_DestroyWriteFirmContext, &aioIob2Bi2x_DestroyWriteFirmContext_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_WriteFirmGetState",
                               aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_WriteFirmIsCompleted",
                               aioIob2Bi2x_WriteFirmIsCompleted, &aioIob2Bi2x_WriteFirmIsCompleted_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_WriteFirmIsError",
                               aioIob2Bi2x_WriteFirmIsError, &aioIob2Bi2x_WriteFirmIsError_orig);

        // libaio-iob.dll
        const auto libaioIobDll = "libaio-iob.dll";
        detour::trampoline_try(libaioIobDll, "aioNMgrIob2_Create",
                               aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
        detour::trampoline_try(libaioIobDll, "aioNMgrIob_BeginManage",
                               aioNMgrIob_BeginManage, &aioNMgrIob_BeginManage_orig);
        detour::trampoline_try(libaioIobDll, "aioNCtlIob_GetNodeInfo",
                               aioNCtlIob_GetNodeInfo, &aioNCtlIob_GetNodeInfo_orig);

        // libaio.dll
        const auto libaioDll = "libaio.dll";
        detour::trampoline_try(libaioDll, "aioNodeMgr_Destroy",
                               aioNodeMgr_Destroy, &aioNodeMgr_Destroy_orig);
        detour::trampoline_try(libaioDll, "aioNodeMgr_GetState",
                               aioNodeMgr_GetState, &aioNodeMgr_GetState_orig);
        detour::trampoline_try(libaioDll, "aioNodeMgr_IsReady",
                               aioNodeMgr_IsReady, &aioNodeMgr_IsReady_orig);
        detour::trampoline_try(libaioDll, "aioNodeMgr_IsError",
                               aioNodeMgr_IsError, &aioNodeMgr_IsError_orig);
        detour::trampoline_try(libaioDll, "aioNodeCtl_Destroy",
                               aioNodeCtl_Destroy, &aioNodeCtl_Destroy_orig);
        detour::trampoline_try(libaioDll, "aioNodeCtl_GetState",
                               aioNodeCtl_GetState, &aioNodeCtl_GetState_orig);
        detour::trampoline_try(libaioDll, "aioNodeCtl_IsReady",
                               aioNodeCtl_IsReady, &aioNodeCtl_IsReady_orig);
        detour::trampoline_try(libaioDll, "aioNodeCtl_IsError",
                               aioNodeCtl_IsError, &aioNodeCtl_IsError_orig);
    }
}