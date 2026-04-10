#include "bi2x_hook.h"

#if SPICE64

#include <optional>
#include <cstdint>
#include "util/detour.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/io.h"
#include "io.h"
#include "util/tapeled.h"
#include <typeinfo>

namespace games::popn {

    /*
     * class definitions
     */

    struct AIO_NMGR_IOB2 {
        uint8_t data[0xe18];
    };

    struct AIO_IOB2_BI2X_AC1__SETTING {
    };

    struct AIO_NMGR_IOB5 {
        uint8_t data[0xb10];
    };

    struct AIO_SCI_COMM {
        uint8_t data[0x108];
    };

    struct AIO_IOB2_BI2X_AC1 {
        uint8_t data[0x4570];
    };

    struct AIO_NMGR_IOB__NODEINFO {
        uint8_t data[0xA8];
    };

    struct AIO_IOB2_BI2X_WRFIRM {
        uint8_t data[0x20450];
    };

    struct AIO_SCI_COMM_T {
        uint64_t unk_0;
        struct AIO_SCI_COMM *aioSciComm;
    };

    struct AIO_COMM_STATUS {
        uint64_t unk[5];
    };

    struct AIO_IOB2_BI2X_AC1__INPUT {
        uint8_t DevIoCounter; // 12
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
        uint8_t Reserved0[2];
        uint8_t TestButton;
        uint8_t ServiceButton;
        uint8_t CoinMech;
        uint8_t Reserved1[23];
        uint8_t Headphones;
        uint8_t Recorder; // 63
        uint8_t Reserved2[14];
        uint8_t JOYL_SW;
        uint8_t Reserved3[4];
        uint8_t JOYR_SW;
    };

    struct AIO_IOB2_BI2X_AC1__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        uint8_t IoResetCounter;
        uint8_t TapeLedCounter;
        uint8_t TapeLedRate[8];
        AIO_IOB2_BI2X_AC1__INPUT Input;
        uint8_t Reserved[628];
    };

    // verified with M39-004-2025121500
    static_assert(sizeof(AIO_NMGR_IOB2) == 0xe18);
    static_assert(sizeof(AIO_NMGR_IOB5) == 0xb10);
    static_assert(sizeof(AIO_SCI_COMM) == 0x108);
    static_assert(sizeof(AIO_IOB2_BI2X_AC1) == 0x4570);
    static_assert(sizeof(AIO_IOB2_BI2X_WRFIRM) == 0x20450);
    static_assert(sizeof(AIO_IOB2_BI2X_AC1__DEVSTATUS) == 0x2C8);

    /*
     * typedefs
     */

    // libaio-iob2_video.dll
    typedef AIO_IOB2_BI2X_AC1 *(__fastcall *aioIob2Bi2xAC1_Create_t)(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId,
                                                                     AIO_IOB2_BI2X_AC1__SETTING *i_Setting);
    typedef void(__fastcall *aioIob2Bi2xAC1_GetDeviceStatus_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl,
                                                               AIO_IOB2_BI2X_AC1__DEVSTATUS *o_DevStatus);
    typedef void(__fastcall *aioIob2Bi2xAC1_SetWatchDogTimer_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count);
    typedef void(__fastcall *aioIob2Bi2xAC1_AddCounter_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter,
                                                          uint32_t i_Count);
    typedef void(__fastcall *aioIob2Bi2xAC1_SetOutputData_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_CnPin,
                                                             uint8_t i_Data);
    typedef void(__fastcall *aioIob2Bi2xAC1_SetTapeLedDataPart_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLedCh,
                                                                  uint32_t i_Offset, uint8_t *i_pData,
                                                                  uint32_t i_cntTapeLed, bool i_bReverse);
    typedef void(__fastcall *aioIob2Bi2x_SetTapeLedDataLimit_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Channel,
                                                                uint8_t i_Scale, uint8_t i_Limit);
    typedef AIO_IOB2_BI2X_WRFIRM *(__fastcall *aioIob2Bi2x_CreateWriteFirmContext_t)(uint32_t i_SerialNumber,
                                                                                     uint32_t i_bfIob);
    typedef void(__fastcall *aioIob2Bi2x_DestroyWriteFirmContext_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef int32_t(__fastcall *aioIob2Bi2x_WriteFirmGetState_t)(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm);
    typedef bool(__fastcall *aioIob2Bi2x_WriteFirmIsCompleted_t)(int32_t i_State);
    
    // libaio-iob.dll
    typedef AIO_NMGR_IOB2 *(__fastcall *aioNMgrIob2_Create_t)(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode);
    typedef void (__fastcall *aioNCtlIob_GetNodeInfo_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo, uint32_t i_cbNodeInfo);

    // libaio.dll
    typedef AIO_SCI_COMM_T *(__fastcall *aioSciComm_Open_t)(AIO_SCI_COMM_T *unk);
    typedef void(__fastcall *aioSci_Destroy_t)(AIO_SCI_COMM *i_pNodeMgr);
    typedef void(__fastcall *aioSci_GetCommStatus_t)(AIO_SCI_COMM *i_pNodeMgr, AIO_COMM_STATUS *i_Status);
    typedef void(__fastcall *aioNodeMgr_Destroy_t)(AIO_NMGR_IOB5 *i_pNodeMgr);
    typedef int32_t(__fastcall *aioNodeMgr_GetState_t)(AIO_NMGR_IOB5 *i_pNodeMgr);
    typedef bool(__fastcall *aioNodeMgr_IsReady_t)(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State);
    typedef bool(__fastcall *aioNodeMgr_IsError_t)(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State);
    typedef void (__fastcall *aioNodeCtl_Destroy_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl);
    typedef int32_t (__fastcall *aioNodeCtl_GetState_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl);
    typedef bool (__fastcall *aioNodeCtl_IsReady_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State);
    typedef bool (__fastcall *aioNodeCtl_IsError_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, int32_t i_State);
    typedef void(__fastcall *aioNodeCtl_UpdateDevicesStatus_t)();

    // libaio-iob5.dll
    typedef AIO_NMGR_IOB5 *(__fastcall *aioNMgrIob5_Create_t)(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode);

    /*
     * function pointers
     */

    // libaio-iob2_video.dll
    static aioIob2Bi2xAC1_Create_t aioIob2Bi2xAC1_Create_orig = nullptr;
    static aioIob2Bi2xAC1_GetDeviceStatus_t aioIob2Bi2xAC1_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xAC1_SetWatchDogTimer_t aioIob2Bi2xAC1_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xAC1_AddCounter_t aioIob2Bi2xAC1_AddCounter_orig = nullptr;
    static aioIob2Bi2xAC1_SetOutputData_t aioIob2Bi2xAC1_SetOutputData_orig = nullptr;
    static aioIob2Bi2xAC1_SetTapeLedDataPart_t aioIob2Bi2xAC1_SetTapeLedDataPart_orig = nullptr;
    static aioIob2Bi2x_SetTapeLedDataLimit_t aioIob2Bi2x_SetTapeLedDataLimit_orig = nullptr;
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_DestroyWriteFirmContext_t aioIob2Bi2x_DestroyWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsCompleted_t aioIob2Bi2x_WriteFirmIsCompleted_orig = nullptr;

    // libaio.dll
    static aioSciComm_Open_t aioSciComm_Open_orig = nullptr;
    static aioSci_Destroy_t aioSci_Destroy_orig = nullptr;
    static aioSci_GetCommStatus_t aioSci_GetCommStatus_orig = nullptr;
    static aioNodeMgr_Destroy_t aioNodeMgr_Destroy_orig = nullptr;
    static aioNodeMgr_GetState_t aioNodeMgr_GetState_orig = nullptr;
    static aioNodeMgr_IsReady_t aioNodeMgr_IsReady_orig = nullptr;
    static aioNodeMgr_IsError_t aioNodeMgr_IsError_orig = nullptr;
    static aioNodeCtl_Destroy_t aioNodeCtl_Destroy_orig = nullptr;
    static aioNodeCtl_GetState_t aioNodeCtl_GetState_orig = nullptr;
    static aioNodeCtl_IsReady_t aioNodeCtl_IsReady_orig = nullptr;
    static aioNodeCtl_IsError_t aioNodeCtl_IsError_orig = nullptr;
    static aioNodeCtl_UpdateDevicesStatus_t aioNodeCtl_UpdateDevicesStatus_orig = nullptr;

    // libaio-iob5.dll
    static aioNMgrIob5_Create_t aioNMgrIob5_Create_orig = nullptr;

    // libaio-iob.dll
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;
    static aioNCtlIob_GetNodeInfo_t aioNCtlIob_GetNodeInfo_orig = nullptr;

    /*
     * variables
     */
    static uint8_t count = 0;
    static AIO_IOB2_BI2X_AC1 *aioIob2Bi2xAc1;
    static AIO_NMGR_IOB2 *aioNmgrIob2;
    static AIO_NMGR_IOB5 *aioNmgrIob5;
    static AIO_SCI_COMM *aioSciComm;
    static AIO_IOB2_BI2X_WRFIRM *aioIob2Bi2xWrfirm;

    /*
     * implementations
     */

    // libaio-iob2_video.dll

    static AIO_IOB2_BI2X_AC1 *__fastcall aioIob2Bi2xAC1_Create(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId,
                                                               AIO_IOB2_BI2X_AC1__SETTING *i_Setting) {
                               
        if (i_pNodeMgr == aioNmgrIob2) {
            aioIob2Bi2xAc1 = new AIO_IOB2_BI2X_AC1;
            return aioIob2Bi2xAc1;
        } else {
            return aioIob2Bi2xAC1_Create_orig(i_pNodeMgr, i_DevId, i_Setting);
        }
    }


    void __fastcall aioIob2Bi2xAC1_GetDeviceStatus(AIO_IOB2_BI2X_AC1 *i_pNodeCtl,
                                                   AIO_IOB2_BI2X_AC1__DEVSTATUS *o_DevStatus) {
        
        RI_MGR->devices_flush_output();
        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus);
        }

        memset(o_DevStatus, 0x00, sizeof(*o_DevStatus));

        o_DevStatus->InputCounter = count;
        o_DevStatus->Input.DevIoCounter = count;
        count++;

        auto &buttons = get_buttons();

        if (!GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test])) {
            o_DevStatus->Input.TestButton = 1;
        }
        if (!GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service])) {
            o_DevStatus->Input.ServiceButton = 1;
        }
        if (!GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech])) {
            o_DevStatus->Input.CoinMech = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Headphone])) {
            o_DevStatus->Input.Headphones = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::RedPop])) {
            o_DevStatus->Input.JOYL_SW = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::BluePop])) {
            o_DevStatus->Input.JOYR_SW = 1;
        }

        // coin
        // TODO: this doesn't work properly
        o_DevStatus->Input.Coin3Count += eamuse_coin_get_stock();
    }

    void __fastcall aioIob2Bi2xAC1_SetWatchDogTimer(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    void __fastcall aioIob2Bi2xAC1_AddCounter(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {

        if (i_pNodeCtl == aioIob2Bi2xAc1 && i_Count == 0) {
            eamuse_coin_set_stock((uint16_t)i_Count);
        } else {
            return aioIob2Bi2xAC1_AddCounter_orig(i_pNodeCtl, i_Counter, i_Count);
        }
    }

    void __fastcall aioIob2Bi2xAC1_SetOutputData(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_CnPin, uint8_t i_Data) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetOutputData_orig(i_pNodeCtl, i_CnPin, i_Data);
        }

        // TODO: lights?
    }

    void __fastcall aioIob2Bi2xAC1_SetTapeLedDataPart(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLedCh,
                                                      uint32_t i_Offset, uint8_t *i_pData,
                                                      uint32_t i_cntTapeLed, bool i_bReverse) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2xAC1_SetTapeLedDataPart_orig(i_pNodeCtl, i_TapeLedCh, i_Offset, i_pData, i_cntTapeLed, i_bReverse);
        }

        // TODO: lights?
    }

    void __fastcall aioIob2Bi2x_SetTapeLedDataLimit(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Channel,
                                                    uint8_t i_Scale, uint8_t i_Limit) {

        if (i_pNodeCtl != aioIob2Bi2xAc1) {
            return aioIob2Bi2x_SetTapeLedDataLimit_orig(i_pNodeCtl, i_Channel, i_Scale, i_Limit);
        }
    }

    static AIO_IOB2_BI2X_WRFIRM *__fastcall aioIob2Bi2x_CreateWriteFirmContext(
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

        if (aioIob2Bi2xWrfirm != nullptr) {
            return true;
        } else {
            return aioIob2Bi2x_WriteFirmIsCompleted_orig(i_State);
        }
    }

    // libaio-iob.dll
    static AIO_NMGR_IOB2 *__fastcall aioNMgrIob2_Create(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode) {

        if (i_pSci == aioSciComm) {
            aioNmgrIob2 = new AIO_NMGR_IOB2;
            return aioNmgrIob2;
        } else {
            return aioNMgrIob2_Create_orig(i_pSci, i_bfMode);
        }
    }

    static void __fastcall aioNCtlIob_GetNodeInfo(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo, uint32_t i_cbNodeInfo) {
        if (i_pNodeCtl == aioIob2Bi2xAc1) {
            memset(o_NodeInfo, 0, sizeof(AIO_NMGR_IOB__NODEINFO));
        } else {
            return aioNCtlIob_GetNodeInfo_orig(i_pNodeCtl, o_NodeInfo, i_cbNodeInfo);
        }
    }

    // libaio.dll

    static AIO_SCI_COMM_T *__fastcall aioSciComm_Open(AIO_SCI_COMM_T *unk) {

        aioSciComm = new AIO_SCI_COMM;
        memset(unk, 0, sizeof(AIO_SCI_COMM_T));
        unk->aioSciComm = aioSciComm;
        return unk;
    }

    static void __fastcall aioSci_GetCommStatus(AIO_SCI_COMM *i_pNodeMgr, AIO_COMM_STATUS *i_Status) {

        if (i_pNodeMgr == aioSciComm) {
            memset(i_Status, 0, sizeof(AIO_COMM_STATUS));
        } else {
            return aioSci_GetCommStatus_orig(i_pNodeMgr, i_Status);
        }
    }

    static void __fastcall aioSci_Destroy(AIO_SCI_COMM *i_pNodeMgr) {
      
        if (i_pNodeMgr == aioSciComm) {
            delete aioSciComm;
        } else {
            return aioSci_Destroy_orig(i_pNodeMgr);
        }
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB5 *i_pNodeMgr) {

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioIob2Bi2xAc1 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            delete aioNmgrIob5;
            aioNmgrIob5 = nullptr;
        } else {
            return aioNodeMgr_Destroy_orig(i_pNodeMgr);
        }
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB5 *i_pNodeMgr) {

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioIob2Bi2xAc1 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {
            return 1;
        } else {
            return aioNodeMgr_GetState_orig(i_pNodeMgr);
        }
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State) {

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioIob2Bi2xAc1 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            return true;
        } else {
            return aioNodeMgr_IsReady_orig(i_pNodeMgr, i_State);
        }
    }

    static bool __fastcall aioNodeMgr_IsError(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State) {

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioIob2Bi2xAc1 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            return false;
        } else {
            return aioNodeMgr_IsError_orig(i_pNodeMgr, i_State);
        }
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

    static void __fastcall aioNodeCtl_UpdateDevicesStatus() {
    }

    // libaio-iob5.dll

    static AIO_NMGR_IOB5 *__fastcall aioNMgrIob5_Create(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode) {

        if (i_pSci == aioSciComm) {
            aioNmgrIob5 = new AIO_NMGR_IOB5;
            return aioNmgrIob5;
        } else {
            return aioNMgrIob5_Create_orig(i_pSci, i_bfMode);
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

        // libaio-iob5.dll
        const auto libaioIob5Dll = "libaio-iob5.dll";
        detour::trampoline_try(libaioIob5Dll, "aioNMgrIob5_Create",
                               aioNMgrIob5_Create, &aioNMgrIob5_Create_orig);

        // libaio-iob2_video.dll
        const auto libaioIob2VideoDll = "libaio-iob2_video.dll";
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_Create",
                               aioIob2Bi2xAC1_Create, &aioIob2Bi2xAC1_Create_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_GetDeviceStatus",
                               aioIob2Bi2xAC1_GetDeviceStatus, &aioIob2Bi2xAC1_GetDeviceStatus_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetWatchDogTimer",
                               aioIob2Bi2xAC1_SetWatchDogTimer, &aioIob2Bi2xAC1_SetWatchDogTimer_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_AddCounter",
                               aioIob2Bi2xAC1_AddCounter, &aioIob2Bi2xAC1_AddCounter_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetOutputData",
                               aioIob2Bi2xAC1_SetOutputData, &aioIob2Bi2xAC1_SetOutputData_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xAC1_SetTapeLedDataPart",
                               aioIob2Bi2xAC1_SetTapeLedDataPart, &aioIob2Bi2xAC1_SetTapeLedDataPart_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2x_SetTapeLedDataLimit",
                               aioIob2Bi2x_SetTapeLedDataLimit, &aioIob2Bi2x_SetTapeLedDataLimit_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?CreateWriteFirmContext@AIO_IOB2_BI2X@@SAPEAUWRFIRM@1@II@Z",
                               aioIob2Bi2x_CreateWriteFirmContext, &aioIob2Bi2x_CreateWriteFirmContext_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?DestroyWriteFirmContext@AIO_IOB2_BI2X@@SAXPEAUWRFIRM@1@@Z",
                               aioIob2Bi2x_DestroyWriteFirmContext, &aioIob2Bi2x_DestroyWriteFirmContext_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?WriteFirmGetState@AIO_IOB2_BI2X@@SAHPEAUWRFIRM@1@@Z",
                               aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        detour::trampoline_try(libaioIob2VideoDll, "?WriteFirmIsCompleted@AIO_IOB2_BI2X@@SA_NH@Z",
                               aioIob2Bi2x_WriteFirmIsCompleted, &aioIob2Bi2x_WriteFirmIsCompleted_orig);

        // libaio-iob.dll
        const auto libaioIobDll = "libaio-iob.dll";
        detour::trampoline_try(libaioIobDll, "aioNMgrIob2_Create",
                               aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
        detour::trampoline_try(libaioIobDll, "aioNCtlIob_GetNodeInfo",
                               aioNCtlIob_GetNodeInfo, &aioNCtlIob_GetNodeInfo_orig);

        // libaio.dll
        const auto libaioDll = "libaio.dll";
        detour::trampoline_try(libaioDll, "?Open@AIO_SCI_COMM@@SA?AU?$AC_ERESULT_T@PEAVAIO_SCI_COMM@@@@PEBD0AEBUSETTING@1@I@Z",
                               aioSciComm_Open, &aioSciComm_Open_orig);
        detour::trampoline_try(libaioDll, "aioSci_GetCommStatus",
                               aioSci_GetCommStatus, &aioSci_GetCommStatus_orig);
        detour::trampoline_try(libaioDll, "aioSci_Destroy",
                               aioSci_Destroy, &aioSci_Destroy_orig);
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
        detour::trampoline_try(libaioDll, "aioNodeCtl_UpdateDevicesStatus",
                               aioNodeCtl_UpdateDevicesStatus, &aioNodeCtl_UpdateDevicesStatus_orig);
    }
}

#endif
