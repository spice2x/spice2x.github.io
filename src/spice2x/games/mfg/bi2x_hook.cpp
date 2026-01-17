#include "bi2x_hook.h"

#include <cstdint>
#include "util/execexe.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/io.h"
#include "io.h"
#include "util/tapeled.h"

namespace games::mfg {

    /*
     * class definitions
     */

    struct AIO_SCI_COMM {
    };

    struct AIO_NMGR_IOB {
    };

    struct AIO_NMGR_IOB2 {
    };

    struct AIO_IOB_BI2A_VFG2 {
    };

    struct AIO_IOB2_BI2X_WRFIRM {
    };

    struct AIO_NMGR_IOB__NODEINFO {
        uint8_t data[0xA3];
    };

    struct AIO_IOB2_BI2X_AC1 {
    };

    struct AIO_IOB2_BI2X_AC1__INPUTDATA {
        uint8_t data[241];
    };

    struct AIO_IOB2_BI2X_AC1__OUTPUTDATA {
        uint8_t data[48];
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

    struct AIO_IOB2_BI2X_AC1__IORESETDATA {
        uint8_t Data[4];
    };

    struct AIO_IOB2_BI2X_AC1__ICNPIN {
        uint16_t Ain[4];
        uint64_t CnPin;
    };

    struct AIO_IOB2_BI2X_AC1__SETTING_COIN {
        uint8_t m_Connector;
        uint8_t m_Pin;
        uint8_t m_OnTime;
        uint8_t m_OffTime;
        uint16_t m_JamTimeout;
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

    struct AIO_IOB2_BI2X_AC1__SETTING {
        AIO_IOB2_BI2X_AC1__SETTING_COIN m_aCoin[4];
        AIO_IOB2_BI2X_AC1__SETTING_COUNTER m_aCounter[4];
        AIO_IOB2_BI2X_AC1__SETTING_AMPVOL m_aAmpVol[4];

        uint16_t m_aTapeLed[8];
        uint16_t m_IoInterval;
        uint8_t m_TestFirm;
        uint8_t m_bEnableDbgMon;
        uint8_t m_DbgMonPort;
        uint8_t m_bEnableSciCmd;
        uint8_t m_SciCmdPort;
        uint8_t m_bEnablePcPwrCtl;
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
        AIO_IOB2_BI2X_AC1__ICNPIN ICnPinHist[18];
        AIO_IOB2_BI2X_AC1__SETTING Setting;
    };

    /*
     * typedefs
     */

    // libaio-iob_video.dll
    typedef AIO_SCI_COMM* (__fastcall *aioIob2Bi2x_OpenSciUsbCdc_t)(uint32_t i_SerialNumber);
    typedef AIO_IOB_BI2A_VFG2* (__fastcall *aioIobBi2aVFG2_Create_t)(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId);
    typedef void (__fastcall *aioIobBi2aVFG2_SetMechType_t)(AIO_IOB_BI2A_VFG2 *i_pNodeCtl, uint32_t i_MechType);
    typedef void (__fastcall *aioIobBi2aVFG2_SetWatchDogTimer_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint16_t i_Count);
    typedef void (__fastcall *aioIobBi2aVFG2_ControlCoinBlocker_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Slot,
                                                                  bool i_bOpen);
    typedef void (__fastcall *aioIobBi2aVFG2_AddCounter_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Counter,
                                                          uint32_t i_Count);
    typedef void (__fastcall *aioIobBi2aVFG2_SetAmpVolume_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Amp,
                                                            uint32_t i_Volume);
    typedef void (__fastcall *aioIobBi2aVFG2_EnableUsbCharger_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, bool i_bEnable);
    typedef void (__fastcall *aioIobBi2aVFG2_SetLamp_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Lamp, uint8_t i_Bright);
    typedef void (__fastcall *aioIobBi2aVFG2_SetIccrLed_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIobBi2aVFG2_SetLedData_t)(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint8_t *i_pData,
                                                          uint32_t i_cbData);

    // libaio-iob.dll
    typedef AIO_NMGR_IOB2* (__fastcall *aioNMgrIob2_Create_t)(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode);
    typedef void (__fastcall *aioNMgrIob_BeginManage_t)(AIO_NMGR_IOB2 *i_pNodeMgr);
    typedef void (__fastcall *aioNCtlIob_GetNodeInfo_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl,
                                                        AIO_NMGR_IOB__NODEINFO *o_NodeInfo);

    // libaio.dll
    typedef void (__fastcall *aioNodeMgr_Destroy_t)(AIO_NMGR_IOB2 *i_pNodeMgr);
    typedef int32_t (__fastcall *aioNodeMgr_GetState_t)(AIO_NMGR_IOB2 *i_pNodeMgr);
    typedef bool (__fastcall *aioNodeMgr_IsReady_t)(AIO_NMGR_IOB2 *i_pNodeMgr, int32_t i_State);
    typedef bool (__fastcall *aioNodeMgr_IsError_t)(AIO_NMGR_IOB2 *i_pNodeMgr, int32_t i_State);
    typedef void (__fastcall *aioNodeCtl_Destroy_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl);
    typedef int32_t (__fastcall *aioNodeCtl_GetState_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl);
    typedef bool (__fastcall *aioNodeCtl_IsReady_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State);
    typedef bool (__fastcall *aioNodeCtl_IsError_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State);

    // libaio-iob2_video.dll
    typedef void (__fastcall *aioIob2Bi2xAC1_SetOutputData_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_CnPin, uint8_t i_Data);
    typedef AIO_IOB2_BI2X_AC1* (__fastcall *aioIob2Bi2xAC1_Create_t)(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId, AIO_IOB2_BI2X_AC1__SETTING *i_pSetting, uint32_t i_cbSetting);
    typedef void (__fastcall *aioIob2Bi2xAC1_GetDeviceStatus_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl,
                                                                AIO_IOB2_BI2X_AC1__DEVSTATUS *o_DevStatus);
    typedef void (__fastcall *aioIob2Bi2xAC1_IoReset_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_bfIoReset);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetWatchDogTimer_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xAC1_ControlCoinBlocker_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Slot,
                                                                   bool i_bOpen);
    typedef void (__fastcall *aioIob2Bi2xAC1_AddCounter_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter,
                                                           uint32_t i_Count);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetAmpVolume_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Volume);
    typedef void (__fastcall *aioIob2Bi2xAC1_EnableUsbCharger_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, bool i_bEnable);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetIrLed_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, bool i_bOn);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetButton0Lamp_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, bool i_bOn);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetIccrLed_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetStickLed_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIob2Bi2xAC1_SetTapeLedData_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLed, uint8_t *i_pData);

    typedef AIO_IOB2_BI2X_WRFIRM* (__fastcall *aioIob2Bi2x_CreateWriteFirmContext_t)(uint32_t i_SerialNumber,
                                                                                     uint32_t i_bfIob);
    typedef void (__fastcall *aioIob2Bi2x_DestroyWriteFirmContext_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef int32_t (__fastcall *aioIob2Bi2x_WriteFirmGetState_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef bool (__fastcall *aioIob2Bi2x_WriteFirmIsCompleted_t)(int32_t i_State);
    typedef bool (__fastcall *aioIob2Bi2x_WriteFirmIsError_t)(int32_t i_State);

    /*
     * function pointers
     */

    // libaio-iob_video.dll
    static aioIobBi2aVFG2_Create_t aioIobBi2aVFG2_Create_orig = nullptr;
    static aioIobBi2aVFG2_SetMechType_t aioIobBi2aVFG2_SetMechType_orig = nullptr;
    static aioIobBi2aVFG2_SetWatchDogTimer_t aioIobBi2aVFG2_SetWatchDogTimer_orig = nullptr;
    static aioIobBi2aVFG2_ControlCoinBlocker_t aioIobBi2aVFG2_ControlCoinBlocker_orig = nullptr;
    static aioIobBi2aVFG2_AddCounter_t aioIobBi2aVFG2_AddCounter_orig = nullptr;
    static aioIobBi2aVFG2_SetAmpVolume_t aioIobBi2aVFG2_SetAmpVolume_orig = nullptr;
    static aioIobBi2aVFG2_EnableUsbCharger_t aioIobBi2aVFG2_EnableUsbCharger_orig = nullptr;
    static aioIobBi2aVFG2_SetLamp_t aioIobBi2aVFG2_SetLamp_orig = nullptr;
    static aioIobBi2aVFG2_SetIccrLed_t aioIobBi2aVFG2_SetIccrLed_orig = nullptr;
    static aioIobBi2aVFG2_SetLedData_t aioIobBi2aVFG2_SetLedData_orig = nullptr;

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

    // libaio-iob2_video.dll
    static aioIob2Bi2xAC1_SetOutputData_t aioIob2Bi2xAC1_SetOutputData_orig = nullptr;
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2xAC1_Create_t aioIob2Bi2xAC1_Create_orig = nullptr;
    static aioIob2Bi2xAC1_GetDeviceStatus_t aioIob2Bi2xAC1_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xAC1_IoReset_t aioIob2Bi2xAC1_IoReset_orig = nullptr;
    static aioIob2Bi2xAC1_SetWatchDogTimer_t aioIob2Bi2xAC1_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xAC1_ControlCoinBlocker_t aioIob2Bi2xAC1_ControlCoinBlocker_orig = nullptr;
    static aioIob2Bi2xAC1_AddCounter_t aioIob2Bi2xAC1_AddCounter_orig = nullptr;
    static aioIob2Bi2xAC1_SetAmpVolume_t aioIob2Bi2xAC1_SetAmpVolume_orig = nullptr;
    static aioIob2Bi2xAC1_EnableUsbCharger_t aioIob2Bi2xAC1_EnableUsbCharger_orig = nullptr;
    static aioIob2Bi2xAC1_SetIrLed_t aioIob2Bi2xAC1_SetIrLed_orig = nullptr;
    static aioIob2Bi2xAC1_SetButton0Lamp_t aioIob2Bi2xAC1_SetButton0Lamp_orig = nullptr;
    static aioIob2Bi2xAC1_SetIccrLed_t aioIob2Bi2xAC1_SetIccrLed_orig = nullptr;
    static aioIob2Bi2xAC1_SetStickLed_t aioIob2Bi2xAC1_SetStickLed_orig = nullptr;
    static aioIob2Bi2xAC1_SetTapeLedData_t aioIob2Bi2xAC1_SetTapeLedData_orig = nullptr;
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_DestroyWriteFirmContext_t aioIob2Bi2x_DestroyWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsCompleted_t aioIob2Bi2x_WriteFirmIsCompleted_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsError_t aioIob2Bi2x_WriteFirmIsError_orig = nullptr;

    /*
     * variables
     */
    static AIO_NMGR_IOB2 *aioNmgrIob2;
    static AIO_IOB2_BI2X_AC1 *aioIob2Bi2xAc1;
    static AIO_IOB2_BI2X_WRFIRM *aioIob2Bi2xWrfirm;
    static AIO_SCI_COMM *aioSciComm;
    static AIO_IOB_BI2A_VFG2 *aioIobBi2aVFG2;
    static uint8_t mechType = 0;
    static uint8_t count = 0;

    /*
     * implementations
     */


    static AIO_IOB_BI2A_VFG2* __fastcall aioIobBi2aVFG2_Create(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId) {
        if (i_pNodeMgr == aioNmgrIob2) {
            log_info("bi2a_hook", "node created");
            aioIobBi2aVFG2 = new AIO_IOB_BI2A_VFG2;
            return aioIobBi2aVFG2;
        } else {
            return aioIobBi2aVFG2_Create_orig(i_pNodeMgr, i_DevId);
        }
    }

    void __fastcall aioIobBi2aVFG2_SetMechType(AIO_IOB_BI2A_VFG2 *i_pNodeCtl, uint32_t i_MechType) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
            mechType = i_MechType;
        } else {
            return aioIobBi2aVFG2_SetMechType_orig(i_pNodeCtl, i_MechType);
        }
    }

    void __fastcall aioIobBi2aVFG2_SetWatchDogTimer(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint16_t i_Count) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    void __fastcall aioIobBi2aVFG2_ControlCoinBlocker(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Slot, bool i_bOpen) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_ControlCoinBlocker_orig(i_pNodeCtl, i_Slot, i_bOpen);
        }
    }

    void __fastcall aioIobBi2aVFG2_AddCounter(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_AddCounter_orig(i_pNodeCtl, i_Counter, i_Count);
        }
    }

    void __fastcall aioIobBi2aVFG2_SetAmpVolume(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Amp, uint32_t i_Volume) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_SetAmpVolume_orig(i_pNodeCtl, i_Amp, i_Volume);
        }
    }

    void __fastcall aioIobBi2aVFG2_EnableUsbCharger(AIO_IOB_BI2A_VFG2* i_pNodeCtl, bool i_bEnable) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_EnableUsbCharger_orig(i_pNodeCtl, i_bEnable);
        }
    }

    void __fastcall aioIobBi2aVFG2_SetLamp(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_Lamp, uint8_t i_Bright) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_SetLamp_orig(i_pNodeCtl, i_Lamp, i_Bright);
        }
    }

    void __fastcall aioIobBi2aVFG2_SetIccrLed(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint32_t i_RGB) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_SetIccrLed_orig(i_pNodeCtl, i_RGB);
        }
    }

    void __fastcall aioIobBi2aVFG2_SetLedData(AIO_IOB_BI2A_VFG2* i_pNodeCtl, uint8_t *i_pData, uint32_t i_cbData) {
        if (i_pNodeCtl == aioIobBi2aVFG2) {
        } else {
            return aioIobBi2aVFG2_SetLedData_orig(i_pNodeCtl, i_pData, i_cbData);
        }
    }

    static AIO_IOB2_BI2X_AC1* __fastcall aioIob2Bi2xAC1_Create(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId, AIO_IOB2_BI2X_AC1__SETTING *i_pSetting, uint32_t i_cbSetting) {
        if (i_pNodeMgr == aioNmgrIob2) {
            log_info("bi2x_hook", "AC1 node created");
            aioIob2Bi2xAc1 = new AIO_IOB2_BI2X_AC1;
            return aioIob2Bi2xAc1;
        }
        return aioIob2Bi2xAC1_Create_orig(i_pNodeMgr, i_DevId, i_pSetting, i_cbSetting);
    }

    static void __fastcall aioIob2Bi2xAC1_GetDeviceStatus(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_IOB2_BI2X_AC1__DEVSTATUS *o_DevStatus) {

        RI_MGR->devices_flush_output();

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus);
        }

        memset(o_DevStatus, 0x00, sizeof(AIO_IOB2_BI2X_AC1__DEVSTATUS));

        auto &buttons = get_buttons();

        o_DevStatus->Input.DevIoCounter = count;
        o_DevStatus->InputCounter = count;
        o_DevStatus->Input.CN11_13 = 1; // Alive flag?
        o_DevStatus->Input.CN8_8 = 1;
        o_DevStatus->Input.CN8_9 = 1;
        o_DevStatus->Input.CN8_10 = 1;
        o_DevStatus->Input.CN15_3 = 0;
        o_DevStatus->Input.CN15_4 = 0;
        o_DevStatus->Input.CN15_5 = 0;
        o_DevStatus->Input.CN15_8 = 1;
        o_DevStatus->Input.CN12_12 = 0;
        o_DevStatus->Input.CN12_13 = 0;

        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test])) {
            o_DevStatus->Input.CN8_8 = 0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service])) {
            o_DevStatus->Input.CN8_9 = 0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech])) {
            o_DevStatus->Input.CN8_10 = 0;
            eamuse_coin_add();
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::QButton1])) {
            o_DevStatus->Input.CN15_3 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::QButton2])) {
            o_DevStatus->Input.CN15_4 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::QButton3])) {
            o_DevStatus->Input.CN15_5 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::QButton])) {
            o_DevStatus->Input.CN15_8 = 0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::JackDetect])) {
            o_DevStatus->Input.CN12_12 = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::MicDetect])) {
            o_DevStatus->Input.CN12_13 = 1;
        }

        o_DevStatus->Input.Coin1Count = eamuse_coin_get_stock();

        count++;
    }

    static void __fastcall aioIob2Bi2xAC1_IoReset(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_bfIoReset) {

        if (i_pNodeCtl == aioIob2Bi2xAc1) {
        } else {
            return aioIob2Bi2xAC1_IoReset_orig(i_pNodeCtl, i_bfIoReset);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_SetWatchDogTimer(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count) {

        if (i_pNodeCtl == aioIob2Bi2xAc1) {
        } else {
            return aioIob2Bi2xAC1_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_ControlCoinBlocker(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Slot, bool i_bOpen) {

        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            eamuse_coin_set_block(!i_bOpen);
        } else {
            return aioIob2Bi2xAC1_ControlCoinBlocker_orig(i_pNodeCtl, i_Slot, i_bOpen);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_AddCounter(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {

        if (i_pNodeCtl == aioIob2Bi2xAc1 && i_Count == 0) {
            eamuse_coin_set_stock((uint16_t) i_Count);
        } else {
            return aioIob2Bi2xAC1_AddCounter_orig(i_pNodeCtl, i_Counter, i_Count);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_SetAmpVolume(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Volume) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetAmpVolume_orig(i_pNodeCtl, i_Volume);
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

    static void __fastcall aioIob2Bi2xAC1_EnableUsbCharger(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, bool i_bEnable) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_EnableUsbCharger_orig(i_pNodeCtl, i_bEnable);
        }
    }

    static void __fastcall aioIob2Bi2xAC1_SetIrLed(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, bool i_bOn) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetIrLed_orig(i_pNodeCtl, i_bOn);
        }

        // handle ir led
    }

    static void __fastcall aioIob2Bi2xAC1_SetButton0Lamp(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, bool i_bOn) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetButton0Lamp_orig(i_pNodeCtl, i_bOn);
        }

        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::SpecialButton), (i_bOn ? 1.f : 0.f));
    }

    static void write_iccr_led(Lights::mfg_lights_t light, uint8_t value) {
        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(light), value / 255);
    }

    static void __fastcall aioIob2Bi2xAC1_SetIccrLed(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_RGB) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetIccrLed_orig(i_pNodeCtl, i_RGB);
        }

        write_iccr_led(Lights::CardReader_B, i_RGB);
        write_iccr_led(Lights::CardReader_G, i_RGB >> 8);
        write_iccr_led(Lights::CardReader_R, i_RGB >> 16);
    }

    static void __fastcall aioIob2Bi2xAC1_SetStickLed(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_RGB) {
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetStickLed_orig(i_pNodeCtl, i_RGB);
        }

        // handle stick led
    }

    static void __fastcall aioIob2Bi2xAC1_SetTapeLedData(
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLed, uint8_t *i_pData) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetTapeLedData_orig(i_pNodeCtl, i_TapeLed, i_pData);
        }

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
        AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo) {

        if (i_pNodeCtl == aioIob2Bi2xAc1) {
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

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB2_BI2X_AC1 *i_pNodeCtl) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            delete aioIob2Bi2xAc1;
            aioIob2Bi2xAc1 = nullptr;
        } else {
            return aioNodeCtl_Destroy_orig(i_pNodeCtl);
        }
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB2_BI2X_AC1 *i_pNodeCtl) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            return 1;
        } else {
            return aioNodeCtl_GetState_orig(i_pNodeCtl);
        }
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            return true;
        } else {
            return aioNodeCtl_IsReady_orig(i_pNodeCtl, i_State);
        }
    }

    static bool __fastcall aioNodeCtl_IsError(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
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
        log_info("ujk_bi2x_hook", "init");

        // libaio-iob_video.dll
        const auto libaioIobVideoDll = "libaio-iob_video.dll";
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_Create",
                               aioIobBi2aVFG2_Create, &aioIobBi2aVFG2_Create_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetMechType",
                               aioIobBi2aVFG2_SetMechType, &aioIobBi2aVFG2_SetMechType_orig);

        // libaio-iob2_video.dll
        const auto libaioIob2VideoDll = "libaio-iob2_video.dll";
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_OpenSciUsbCdc",
                               aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_Create",
                               aioIob2Bi2xAC1_Create, &aioIob2Bi2xAC1_Create_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_GetDeviceStatus",
                               aioIob2Bi2xAC1_GetDeviceStatus, &aioIob2Bi2xAC1_GetDeviceStatus_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_IoReset",
                               aioIob2Bi2xAC1_IoReset, &aioIob2Bi2xAC1_IoReset_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetWatchDogTimer",
                               aioIob2Bi2xAC1_SetWatchDogTimer, &aioIob2Bi2xAC1_SetWatchDogTimer_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_ControlCoinBlocker",
                               aioIob2Bi2xAC1_ControlCoinBlocker, &aioIob2Bi2xAC1_ControlCoinBlocker_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_AddCounter",
                               aioIob2Bi2xAC1_AddCounter, &aioIob2Bi2xAC1_AddCounter_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetAmpVolume",
                               aioIob2Bi2xAC1_SetAmpVolume, &aioIob2Bi2xAC1_SetAmpVolume_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_EnableUsbCharger",
                               aioIob2Bi2xAC1_EnableUsbCharger, &aioIob2Bi2xAC1_EnableUsbCharger_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetIrLed",
                               aioIob2Bi2xAC1_SetIrLed, &aioIob2Bi2xAC1_SetIrLed_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetButton0Lamp",
                               aioIob2Bi2xAC1_SetButton0Lamp, &aioIob2Bi2xAC1_SetButton0Lamp_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetIccrLed",
                               aioIob2Bi2xAC1_SetIccrLed, &aioIob2Bi2xAC1_SetIccrLed_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetStickLed",
                               aioIob2Bi2xAC1_SetStickLed, &aioIob2Bi2xAC1_SetStickLed_orig);
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetTapeLedData",
                               aioIob2Bi2xAC1_SetTapeLedData, &aioIob2Bi2xAC1_SetTapeLedData_orig);
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
        execexe::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetOutputData",
                               aioIob2Bi2xAC1_SetOutputData, &aioIob2Bi2xAC1_SetOutputData_orig);


        // libaio-iob.dll
        const auto libaioIobDll = "libaio-iob.dll";
        execexe::trampoline_try(libaioIobDll, "aioNMgrIob2_Create",
                               aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
        execexe::trampoline_try(libaioIobDll, "aioNMgrIob_BeginManage",
                               aioNMgrIob_BeginManage, &aioNMgrIob_BeginManage_orig);
        execexe::trampoline_try(libaioIobDll, "aioNCtlIob_GetNodeInfo",
                               aioNCtlIob_GetNodeInfo, &aioNCtlIob_GetNodeInfo_orig);


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