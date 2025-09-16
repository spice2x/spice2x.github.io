#include "bi2x_hook.h"

#include "util/execexe.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/io.h"
#include "io.h"
#include "util/tapeled.h"

namespace games::xif {

    /*
     * class definitions
     */

    struct AIO_SCI_COMM {
    };

    struct AIO_NMGR_IOB {
    };

    struct AIO_IOB2_BI2X_AC1 {
    };

    struct AIO_IOB2_BI2X_WRFIRM {
    };

    struct AIO_NMGR_IOB__NODEINFO {
        uint8_t data[0xA3];
    };

    struct AIO_IOB2_BI2X_AC1__INPUT {
        uint8_t DevIoCounter;
        uint8_t bExIoAErr;
        uint8_t bExIoBErr;
        uint8_t bPcPowerOn;
        uint8_t bPcPowerCheck;
        uint8_t bCoin1Jam;
        uint8_t bCoin2Jam;
        uint8_t bCoin3Jam;
        uint8_t bCoin4Jam;
        uint8_t Coin1Count;
        uint8_t Coin2Count;
        uint8_t Coin3Count;
        uint8_t Coin4Count;
        uint16_t AnalogCh1;
        uint16_t AnalogCh2;
        uint16_t AnalogCh3;
        uint16_t AnalogCh4;
        uint8_t CN8_8;
        uint8_t CN8_9;
        uint8_t CN8_10;
        uint8_t CN9_8;
        uint8_t CN9_9;
        uint8_t CN9_10;
        uint8_t CN11_11;
        uint8_t CN11_12;
        uint8_t CN11_13;
        uint8_t CN11_14;
        uint8_t CN11_15;
        uint8_t CN11_16;
        uint8_t CN11_17;
        uint8_t CN11_18;
        uint8_t CN11_19;
        uint8_t CN11_20;
        uint8_t CN12_11;
        uint8_t CN12_12;
        uint8_t CN12_13;
        uint8_t CN12_14;
        uint8_t CN12_15;
        uint8_t CN12_16;
        uint8_t CN12_17;
        uint8_t CN12_18;
        uint8_t CN12_19;
        uint8_t CN12_20;
        uint8_t CN12_21;
        uint8_t CN12_22;
        uint8_t CN12_23;
        uint8_t CN12_24;
        uint8_t CN15_3;
        uint8_t CN15_4;
        uint8_t CN15_5;
        uint8_t CN15_6;
        uint8_t CN15_7;
        uint8_t CN15_8;
        uint8_t CN15_9;
        uint8_t CN15_10;
        uint8_t CN15_11;
        uint8_t CN15_12;
        uint8_t CN15_13;
        uint8_t CN15_14;
        uint8_t CN15_15;
        uint8_t CN15_16;
        uint8_t CN15_17;
        uint8_t CN15_18;
        uint8_t CN15_19;
        uint8_t CN15_20;
        uint8_t CN19_8;
        uint8_t CN19_9;
        uint8_t CN19_10;
        uint8_t CN19_11;
        uint8_t CN19_12;
        uint8_t CN19_13;
        uint8_t CN19_14;
        uint8_t CN19_15;
    };

    struct AIO_IOB2_BI2X_AC1__INPUTDATA {
        uint8_t Data[228];
    };

    struct AIO_IOB2_BI2X_AC1__OUTPUTDATA {
        uint8_t Data[48];
    };

    struct AIO_IOB2_BI2X_AC1__IORESETDATA {
        uint8_t Data[4];
    };

    struct AIO_IOB2_BI2X_AC1__ICNPIN {
        uint16_t Ain[4];
        uint64_t CnPin;
    };

    struct AIO_IOB2_BI2X_AC1__SETTING_AMPVOL {
        uint8_t m_CnDown;
        uint8_t m_PinDown;
        uint8_t m_CnUp;
        uint8_t m_PinUp;
        uint8_t m_CnMute;
        uint8_t m_PinMute;
        uint16_t m_OnTime;
        uint16_t m_OffTime;
    };

    struct AIO_IOB2_BI2X_AC1__SETTING_COUNTER {
        uint8_t m_Connector;
        uint8_t m_Pin;
        uint16_t m_OnTime;
        uint16_t m_OffTime;
    };

    struct AIO_IOB2_BI2X_AC1__SETTING_COIN {
        uint8_t m_Connector;
        uint8_t m_Pin;
        uint8_t m_OnTime;
        uint8_t m_OffTime;
        uint16_t m_JamTimeout;
    };

    struct AIO_IOB2_BI2X_AC1__SETTING {
        AIO_IOB2_BI2X_AC1__SETTING_COIN m_aCoin[4];
        AIO_IOB2_BI2X_AC1__SETTING_COUNTER m_aCounter[4];
        AIO_IOB2_BI2X_AC1__SETTING_AMPVOL m_aAmpVol[4];

        uint16_t m_aTapeLed[8];
        uint8_t m_TestFirm;
        uint8_t m_bEnableDbgMon;
        uint8_t m_bDisableSciCmd;
        uint8_t m_bDisablePcPwrCtl;
    };

    struct AIO_IOB2_BI2X_AC1__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        uint8_t TapeLedCounter;
        uint8_t TapeLedRate[8];
        AIO_IOB2_BI2X_AC1__INPUT Input;
        AIO_IOB2_BI2X_AC1__INPUTDATA InputData;
        AIO_IOB2_BI2X_AC1__OUTPUTDATA OutputData;
        AIO_IOB2_BI2X_AC1__IORESETDATA IoResetData;
        AIO_IOB2_BI2X_AC1__ICNPIN ICnPinHist[17];
        AIO_IOB2_BI2X_AC1__SETTING Setting;
    };

    /*
     * typedefs
     */

    // libaio-iob2_video.dll
    // (Konami.Aio.aioIob2Bi2xAC1)
    typedef AIO_IOB2_BI2X_AC1* (__fastcall *aioIob2Bi2xAC1_Create_t)(AIO_NMGR_IOB *i_pNodeMgr, uint32_t i_DevId, AIO_IOB2_BI2X_AC1__SETTING *i_pSetting, uint32_t i_cbSetting);
    typedef void (__fastcall *aioIob2Bi2xAC1_GetDeviceStatus_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_IOB2_BI2X_AC1__DEVSTATUS *o_DevStatus, uint32_t i_cbDevStatus);
    typedef void (__fastcall *aioIob2Bi2xAC1_IoReset_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_bfIoReset);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetWatchDogTimer_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xAC1_AddCounter_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetAmpVolume_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Amp, uint32_t i_Volume);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetOutputData_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_CnPin, uint8_t i_Data);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetTapeLedDataPart_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLedCh, uint32_t i_Offset, uint8_t i_pData[], uint32_t i_cntTapeLed, bool i_bReverse);

    // libaio-iob.dll
    // (Konami.Aio.aioNMgrIob)
    typedef AIO_NMGR_IOB* (__fastcall *aioNMgrIob2_Create_t)(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode);
    typedef uint32_t (__fastcall *aioNMgrIob_GetCommStatus_t)(AIO_NMGR_IOB *i_pNodeMgr, AIO_NMGR_IOB__NODEINFO o_pCommStatus, uint32_t i_cbCommStatus);
    typedef void (__fastcall *aioNMgrIob_BeginManage_t)(AIO_NMGR_IOB *i_pNodeMgr);
    // (Konami.Aio.aioNCtlIob)
    typedef void (__fastcall *aioNCtlIob_GetNodeInfo_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo, uint32_t i_cbNodeInfo);
    // (Konami.Aio.aioIob2Bi2x)
    typedef AIO_SCI_COMM* (__fastcall *aioIob2Bi2x_OpenSciUsbCdc_t)(uint32_t i_SerialNumber);
    typedef AIO_SCI_COMM* (__fastcall *aioIob2Bi2x_SciOpenB8PNS1_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Port, uint32_t i_BaudRate);
    typedef void (__fastcall *aioIob2Bi2x_SetTapeLedDataGroup_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_bfGroup);
    typedef void (__fastcall *aioIob2Bi2x_SetTapeLedDataLimit_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Channel, uint8_t i_Scale, uint8_t i_Limit);
    typedef AIO_IOB2_BI2X_WRFIRM* (__fastcall *aioIob2Bi2x_CreateWriteFirmContext_t)(uint32_t i_SerialNumber, uint32_t i_bfIob);
    typedef void (__fastcall *aioIob2Bi2x_DestroyWriteFirmContext_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef int32_t (__fastcall *aioIob2Bi2x_WriteFirmGetState_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef bool (__fastcall *aioIob2Bi2x_WriteFirmIsCompleted_t)(int32_t i_State);
    typedef bool (__fastcall *aioIob2Bi2x_WriteFirmIsError_t)(int32_t i_State);

    // libaio.dll
    // (Konami.Aio.aioNodeMgr)
    typedef void (__fastcall *aioNodeMgr_Destroy_t)(AIO_NMGR_IOB *i_pNodeMgr);
    typedef int32_t (__fastcall *aioNodeMgr_GetState_t)(AIO_NMGR_IOB *i_pNodeMgr);
    typedef bool (__fastcall *aioNodeMgr_IsReady_t)(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State);
    typedef bool (__fastcall *aioNodeMgr_IsError_t)(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State);
    // (Konami.Aio.aioNodeCtl)
    typedef void (__fastcall *aioNodeCtl_Destroy_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl);
    typedef int32_t (__fastcall *aioNodeCtl_GetState_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl);
    typedef bool (__fastcall *aioNodeCtl_IsReady_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State);
    typedef bool (__fastcall *aioNodeCtl_IsError_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State);

    /*
     * function pointers
     */

    // libaio-iob2_video.dll
    // (Konami.Aio.aioIob2Bi2xAC1)
    static aioIob2Bi2xAC1_Create_t aioIob2Bi2xAC1_Create_orig = nullptr;
    static aioIob2Bi2xAC1_GetDeviceStatus_t aioIob2Bi2xAC1_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xAC1_IoReset_t aioIob2Bi2xAC1_IoReset_orig = nullptr;
    static aioIob2Bi2xAC1_SetWatchDogTimer_t aioIob2Bi2xAC1_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xAC1_AddCounter_t aioIob2Bi2xAC1_AddCounter_orig = nullptr;
    static aioIob2Bi2xAC1_SetAmpVolume_t aioIob2Bi2xAC1_SetAmpVolume_orig = nullptr;
    static aioIob2Bi2xAC1_SetOutputData_t aioIob2Bi2xAC1_SetOutputData_orig = nullptr;
    static aioIob2Bi2xAC1_SetTapeLedDataPart_t aioIob2Bi2xAC1_SetTapeLedDataPart_orig = nullptr;

    // libaio-iob.dll
    // (Konami.Aio.aioNMgrIob)
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;
    static aioNMgrIob_GetCommStatus_t aioNMgrIob_GetCommStatus_orig = nullptr;
    static aioNMgrIob_BeginManage_t aioNMgrIob_BeginManage_orig = nullptr;
    // (Konami.Aio.aioNCtlIob)
    static aioNCtlIob_GetNodeInfo_t aioNCtlIob_GetNodeInfo_orig = nullptr;
    // (Konami.Aio.aioIob2Bi2x)
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2x_SciOpenB8PNS1_t aioIob2Bi2x_SciOpenB8PNS1_orig = nullptr;
    static aioIob2Bi2x_SetTapeLedDataGroup_t aioIob2Bi2x_SetTapeLedDataGroup_orig = nullptr;
    static aioIob2Bi2x_SetTapeLedDataLimit_t aioIob2Bi2x_SetTapeLedDataLimit_orig = nullptr;
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_DestroyWriteFirmContext_t aioIob2Bi2x_DestroyWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsCompleted_t aioIob2Bi2x_WriteFirmIsCompleted_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsError_t aioIob2Bi2x_WriteFirmIsError_orig = nullptr;

    // libaio.dll
    // (Konami.Aio.aioNodeMgr)
    static aioNodeMgr_Destroy_t aioNodeMgr_Destroy_orig = nullptr;
    static aioNodeMgr_GetState_t aioNodeMgr_GetState_orig = nullptr;
    static aioNodeMgr_IsReady_t aioNodeMgr_IsReady_orig = nullptr;
    static aioNodeMgr_IsError_t aioNodeMgr_IsError_orig = nullptr;
    // (Konami.Aio.aioNodeCtl)
    static aioNodeCtl_Destroy_t aioNodeCtl_Destroy_orig = nullptr;
    static aioNodeCtl_GetState_t aioNodeCtl_GetState_orig = nullptr;
    static aioNodeCtl_IsReady_t aioNodeCtl_IsReady_orig = nullptr;
    static aioNodeCtl_IsError_t aioNodeCtl_IsError_orig = nullptr;

    /*
     * variables
     */

    static AIO_SCI_COMM *aioSciComm;
    static AIO_NMGR_IOB *aioNmgrIob;
    static AIO_IOB2_BI2X_AC1 *aioIob2Bi2xAc1;
    static AIO_IOB2_BI2X_WRFIRM *aioIob2Bi2xWrfirm;
    static uint8_t count = 0;

    /*
     * implementations
     */

    static AIO_IOB2_BI2X_AC1* __fastcall aioIob2Bi2xAC1_Create(AIO_NMGR_IOB *i_pNodeMgr, uint32_t i_DevId, AIO_IOB2_BI2X_AC1__SETTING *i_pSetting, uint32_t i_cbSetting) {
        if (i_pNodeMgr == aioNmgrIob) {
            log_info("bi2x_hook", "node created");
            aioIob2Bi2xAc1 = new AIO_IOB2_BI2X_AC1;
            return aioIob2Bi2xAc1;
        }
        return aioIob2Bi2xAC1_Create_orig(i_pNodeMgr, i_DevId, i_pSetting, i_cbSetting);
    }

    static void __fastcall aioIob2Bi2xAC1_GetDeviceStatus(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_IOB2_BI2X_AC1__DEVSTATUS *o_DevStatus, uint32_t i_cbDevStatus) {
        RI_MGR->devices_flush_output();

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus, i_cbDevStatus);
        }

        memset(o_DevStatus, 0x00, sizeof(AIO_IOB2_BI2X_AC1__DEVSTATUS));

        auto &buttons = get_buttons();
        auto &analogs = get_analogs();

        o_DevStatus->Input.DevIoCounter = count;
        o_DevStatus->InputCounter = count;

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test])) {
            o_DevStatus->Input.CN8_8 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service])) {
            o_DevStatus->Input.CN8_9 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Coin])) {
            o_DevStatus->Input.CN8_10 = 1;
            eamuse_coin_add();
        }

        auto left = analogs[Analogs::L_Fader].isSet() ? 
            static_cast<size_t>(round(GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::L_Fader]) * 5)) : 2;
        auto right = analogs[Analogs::R_Fader].isSet() ? 
            static_cast<size_t>(round(GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::R_Fader]) * 5)) : 2;

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::L_FaderLeft])) {
            left = 0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::L_FaderRight])) {
            left = 5;
        }

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::R_FaderLeft]))
        {
            right = 0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::R_FaderRight]))
        {
            right = 5;
        }

        uint8_t* left_map[] = { 
            &o_DevStatus->Input.CN15_7,
            &o_DevStatus->Input.CN15_6,
            &o_DevStatus->Input.CN15_5,
            &o_DevStatus->Input.CN15_4,
            &o_DevStatus->Input.CN15_3,
        };

        for (size_t i = 0; i < 5; i++) {
            *left_map[i] = i >= left;
        }

        uint8_t* right_map[] = {
            &o_DevStatus->Input.CN11_20,
            &o_DevStatus->Input.CN11_19,
            &o_DevStatus->Input.CN9_10,
            &o_DevStatus->Input.CN9_9,
            &o_DevStatus->Input.CN9_8,
        };

        for (size_t i = 0; i < 5; i++) {
            *right_map[i] = i >= right;
        }

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane1])) {
            o_DevStatus->Input.CN12_11 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane2]))
        {
            o_DevStatus->Input.CN12_12 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane3]))
        {
            o_DevStatus->Input.CN12_13 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane4]))
        {
            o_DevStatus->Input.CN12_14 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane5]))
        {
            o_DevStatus->Input.CN12_15 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane6]))
        {
            o_DevStatus->Input.CN12_16 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane7]))
        {
            o_DevStatus->Input.CN12_17 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane8]))
        {
            o_DevStatus->Input.CN12_18 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane9]))
        {
            o_DevStatus->Input.CN12_19 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane10]))
        {
            o_DevStatus->Input.CN12_20 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane11]))
        {
            o_DevStatus->Input.CN12_21 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Lane12]))
        {
            o_DevStatus->Input.CN12_22 = 1;
        }

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Headphone]))
        {
            o_DevStatus->Input.CN15_10 = 1;
        }

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Recorder]))
        {
            o_DevStatus->Input.CN15_12 = 1;
        }

        o_DevStatus->Input.Coin1Count = eamuse_coin_get_stock();

        count++;
    }

    static void __fastcall aioIob2Bi2xAC1_IoReset(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_bfIoReset) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_IoReset_orig(i_pNodeCtl, i_bfIoReset);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_SetWatchDogTimer(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_AddCounter(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {
        if (i_pNodeCtl == aioIob2Bi2xAc1 && i_Count == 0) {
            eamuse_coin_set_stock((uint16_t) i_Count);
            return;
        }
        return aioIob2Bi2xAC1_AddCounter_orig(i_pNodeCtl, i_Counter, i_Count);
    }

    static void __fastcall aioIob2Bi2xAC1_SetAmpVolume(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Amp, uint32_t i_Volume) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetAmpVolume_orig(i_pNodeCtl, i_Amp, i_Volume);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_SetOutputData(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_CnPin, uint8_t i_Data) {
        if (i_pNodeCtl == aioIob2Bi2xAc1 && i_CnPin == 1) {
            eamuse_coin_set_block(i_Data == 0);
            return;
        }
        // TODO: LED impl
        // return aioIob2Bi2xAC1_SetOutputData_orig(i_pNodeCtl, i_CnPin, i_Data);
    }

    static void __fastcall aioIob2Bi2xAC1_SetTapeLedDataPart(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLedCh, uint32_t i_Offset, uint8_t i_pData[], uint32_t i_cntTapeLed, bool i_bReverse) {
        // TODO: LED tape impl
        // return aioIob2Bi2xAC1_SetTapeLedDataPart_orig(i_pNodeCtl, i_TapeLedCh, i_Offset, i_pData, i_cntTapeLed, i_bReverse);
    }

    static AIO_NMGR_IOB* __fastcall aioNMgrIob2_Create(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode) {
        if (i_pSci == aioSciComm) {
            aioNmgrIob = new AIO_NMGR_IOB;
            return aioNmgrIob;
        }
        return aioNMgrIob2_Create_orig(i_pSci, i_bfMode);
    }

    static uint32_t __fastcall aioNMgrIob_GetCommStatus(AIO_NMGR_IOB *i_pNodeMgr, AIO_NMGR_IOB__NODEINFO o_pCommStatus, uint32_t i_cbCommStatus) {
        return aioNMgrIob_GetCommStatus_orig(i_pNodeMgr, o_pCommStatus, i_cbCommStatus);
    }

    static void __fastcall aioNMgrIob_BeginManage(AIO_NMGR_IOB *i_pNodeMgr) {
        if (i_pNodeMgr != aioNmgrIob) {
            return aioNMgrIob_BeginManage_orig(i_pNodeMgr);
        }
    }

    static void __fastcall aioNCtlIob_GetNodeInfo(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo, uint32_t i_cbNodeInfo) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            memset(o_NodeInfo, 0, sizeof(AIO_NMGR_IOB__NODEINFO));
        } else {
            return aioNCtlIob_GetNodeInfo_orig(i_pNodeCtl, o_NodeInfo, i_cbNodeInfo);
        }
    }

    static AIO_SCI_COMM* __fastcall aioIob2Bi2x_OpenSciUsbCdc(uint32_t i_SerialNumber) {
        aioSciComm = new AIO_SCI_COMM;
        return aioSciComm;
    }

    static AIO_SCI_COMM* __fastcall aioIob2Bi2x_SciOpenB8PNS1(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Port, uint32_t i_BaudRate) {
        log_info("bi2x_hook", "aioIob2Bi2x_SciOpenB8PNS1");
        return aioIob2Bi2x_SciOpenB8PNS1_orig(i_pNodeCtl, i_Port, i_BaudRate);
    }

    static AIO_IOB2_BI2X_WRFIRM* __fastcall aioIob2Bi2x_CreateWriteFirmContext(uint32_t i_SerialNumber, uint32_t i_bfIob) {
        aioIob2Bi2xWrfirm = new AIO_IOB2_BI2X_WRFIRM;
        return aioIob2Bi2xWrfirm;
    }

    static void __fastcall aioIob2Bi2x_SetTapeLedDataGroup(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_bfGroup) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            log_warning("bi2x_hook", "aioIob2Bi2x_SetTapeLedDataGroup: TODO");
        }
        return aioIob2Bi2x_SetTapeLedDataGroup_orig(i_pNodeCtl, i_bfGroup);
    }

    static void __fastcall aioIob2Bi2x_SetTapeLedDataLimit(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Channel, uint8_t i_Scale, uint8_t i_Limit) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            log_warning("bi2x_hook", "aioIob2Bi2x_SetTapeLedDataLimit: TODO");
            return;
        }

        return aioIob2Bi2x_SetTapeLedDataLimit_orig(i_pNodeCtl, i_Channel, i_Scale, i_Limit);
    }

    static void __fastcall aioIob2Bi2x_DestroyWriteFirmContext(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm) {
        if (i_pWrFirm != aioIob2Bi2xWrfirm) {
            return aioIob2Bi2x_DestroyWriteFirmContext_orig(i_pWrFirm);
        }
        delete aioIob2Bi2xWrfirm;
        aioIob2Bi2xWrfirm = nullptr;
    }

    static int32_t __fastcall aioIob2Bi2x_WriteFirmGetState(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm) {
        if (i_pWrFirm == aioIob2Bi2xWrfirm) {
            return 8;
        }
        return aioIob2Bi2x_WriteFirmGetState_orig(i_pWrFirm);
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsCompleted(int32_t i_State) {
        if (aioIob2Bi2xWrfirm != nullptr) {
            return true;
        }
        return aioIob2Bi2x_WriteFirmIsCompleted_orig(i_State);
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsError(int32_t i_State) {
        if (aioIob2Bi2xWrfirm != nullptr) {
            return false;
        }
        return aioIob2Bi2x_WriteFirmIsError_orig(i_State);
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB *i_pNodeMgr) {
        if (i_pNodeMgr != aioNmgrIob) {
            return aioNodeMgr_Destroy_orig(i_pNodeMgr);
        }
        delete aioNmgrIob;
        aioNmgrIob = nullptr;
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob) {
            return 1;
        }
        return aioNodeMgr_GetState_orig(i_pNodeMgr);
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob) {
            return true;
        }
        return aioNodeMgr_IsReady_orig(i_pNodeMgr, i_State);
    }

    static bool __fastcall aioNodeMgr_IsError(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob) {
            return false;
        }
        return aioNodeMgr_IsError_orig(i_pNodeMgr, i_State);
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB2_BI2X_AC1 *i_pNodeCtl) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioNodeCtl_Destroy_orig(i_pNodeCtl);
        }
        delete aioIob2Bi2xAc1;
        aioIob2Bi2xAc1 = nullptr;
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB2_BI2X_AC1 *i_pNodeCtl) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            return 1;
        }
        return aioNodeCtl_GetState_orig(i_pNodeCtl);
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            return true;
        }
        return aioNodeCtl_IsReady_orig(i_pNodeCtl, i_State);
    }

    static bool __fastcall aioNodeCtl_IsError(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            return false;
        }
        return aioNodeCtl_IsError_orig(i_pNodeCtl, i_State);
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
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_Create",
                               aioIob2Bi2xAC1_Create, &aioIob2Bi2xAC1_Create_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_GetDeviceStatus",
                               aioIob2Bi2xAC1_GetDeviceStatus, &aioIob2Bi2xAC1_GetDeviceStatus_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_IoReset",
                               aioIob2Bi2xAC1_IoReset, &aioIob2Bi2xAC1_IoReset_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetWatchDogTimer",
                               aioIob2Bi2xAC1_SetWatchDogTimer, &aioIob2Bi2xAC1_SetWatchDogTimer_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_AddCounter",
                               aioIob2Bi2xAC1_AddCounter, &aioIob2Bi2xAC1_AddCounter_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetAmpVolume",
                               aioIob2Bi2xAC1_SetAmpVolume, &aioIob2Bi2xAC1_SetAmpVolume_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetOutputData",
                               aioIob2Bi2xAC1_SetOutputData, &aioIob2Bi2xAC1_SetOutputData_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetTapeLedDataPart",
                               aioIob2Bi2xAC1_SetTapeLedDataPart, &aioIob2Bi2xAC1_SetTapeLedDataPart_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_OpenSciUsbCdc",
                                aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_SetTapeLedDataGroup",
                                aioIob2Bi2x_SetTapeLedDataGroup, &aioIob2Bi2x_SetTapeLedDataGroup_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_SetTapeLedDataLimit",
                                aioIob2Bi2x_SetTapeLedDataLimit, &aioIob2Bi2x_SetTapeLedDataLimit_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_CreateWriteFirmContext",
                                aioIob2Bi2x_CreateWriteFirmContext, &aioIob2Bi2x_CreateWriteFirmContext_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_DestroyWriteFirmContext",
                                aioIob2Bi2x_DestroyWriteFirmContext, &aioIob2Bi2x_DestroyWriteFirmContext_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_WriteFirmGetState",
                                aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_WriteFirmIsCompleted",
                                aioIob2Bi2x_WriteFirmIsCompleted, &aioIob2Bi2x_WriteFirmIsCompleted_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_WriteFirmIsError",
                                aioIob2Bi2x_WriteFirmIsError, &aioIob2Bi2x_WriteFirmIsError_orig);

        // libaio-iob.dll
        const auto libaioIobDll = "libaio-iob.dll";
        execexe::trampoline_try(libaioIobDll, "aioNMgrIob2_Create",
                               aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
        execexe::trampoline_try(libaioIobDll, "aioNMgrIob_GetCommStatus",
                               aioNMgrIob_GetCommStatus, &aioNMgrIob_GetCommStatus_orig);
        execexe::trampoline_try(libaioIobDll, "aioNMgrIob_BeginManage",
                               aioNMgrIob_BeginManage, &aioNMgrIob_BeginManage_orig);
        execexe::trampoline_try(libaioIobDll, "aioNCtlIob_GetNodeInfo",
                               aioNCtlIob_GetNodeInfo, &aioNCtlIob_GetNodeInfo_orig);
        execexe::trampoline_try(libaioIobDll, "aioIob2Bi2x_SciOpenB8PNS1",
                               aioIob2Bi2x_SciOpenB8PNS1, &aioIob2Bi2x_SciOpenB8PNS1_orig);
       
        // libaio.dll
        const auto libaioDll = "libaio.dll";
        execexe::trampoline_try(libaioDll, "aioNodeMgr_Destroy",
                               aioNodeMgr_Destroy, &aioNodeMgr_Destroy_orig);
        execexe::trampoline_try(libaioDll, "aioNodeMgr_GetState",
                               aioNodeMgr_GetState, &aioNodeMgr_GetState_orig);
        execexe::trampoline_try(libaioDll, "aioNodeMgr_IsReady",
                               aioNodeMgr_IsReady, &aioNodeMgr_IsReady_orig);
        execexe::trampoline_try(libaioDll, "aioNodeMgr_IsError",
                               aioNodeMgr_IsError, &aioNodeMgr_IsError_orig);
        execexe::trampoline_try(libaioDll, "aioNodeCtl_Destroy",
                               aioNodeCtl_Destroy, &aioNodeCtl_Destroy_orig);
        execexe::trampoline_try(libaioDll, "aioNodeCtl_GetState",
                               aioNodeCtl_GetState, &aioNodeCtl_GetState_orig);
        execexe::trampoline_try(libaioDll, "aioNodeCtl_IsReady",
                               aioNodeCtl_IsReady, &aioNodeCtl_IsReady_orig);
        execexe::trampoline_try(libaioDll, "aioNodeCtl_IsError",
                               aioNodeCtl_IsError, &aioNodeCtl_IsError_orig);
    }
}