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

    struct AIO_IOB5_BI3A__DEVSTATUS__INPUT {
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
        uint8_t Padding;
        uint16_t AnalogCh1; 
        uint16_t AnalogCh2; 
        uint16_t AnalogCh3; 
        uint16_t AnalogCh4; 
        uint8_t Reserved0[2]; 
        uint8_t TestButton;    // [220]
        uint8_t ServiceButton; // [221]
        uint8_t CoinMech;      // [222]
        uint8_t Buttons[23];   // [222-245]
        uint8_t Headphones;    // [246]
        uint8_t Recorder;      // [247]
        uint8_t Reserved1[14]; // [248-261]
        uint8_t JOYL_SW;       // [262]
        uint8_t Reserved2[4];  // [263-266]
        uint8_t JOYR_SW;       // [267]
    };

    struct AIO_IOB5_BI3A {
        uint8_t data[0x4FF0];
    };

    struct AIO_IOB5_BI3A__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        uint8_t IoResetCounter;
        uint8_t TapeLedCounter;
        uint8_t TapeLedRate[8];
        AIO_IOB5_BI3A__DEVSTATUS__INPUT Input; // offset 12
        uint8_t Reserved[564];
    };

    // verified with M39-004-2025121500
    static_assert(sizeof(AIO_NMGR_IOB2) == 0xe18);
    static_assert(sizeof(AIO_NMGR_IOB5) == 0xb10);
    static_assert(sizeof(AIO_SCI_COMM) == 0x108);
    static_assert(sizeof(AIO_IOB2_BI2X_AC1) == 0x4570);
    static_assert(sizeof(AIO_IOB2_BI2X_WRFIRM) == 0x20450);
    static_assert(sizeof(AIO_IOB5_BI3A) == 0x4FF0);
    static_assert(sizeof(AIO_IOB5_BI3A__DEVSTATUS) == 0x288);

    /*
     * typedefs
     */

    // libaio-iob2_video.dll
    typedef AIO_IOB2_BI2X_AC1 *(__fastcall *aioIob2Bi2xAC1_Create_t)(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId,
                                                                     AIO_IOB2_BI2X_AC1__SETTING *i_Setting);
    typedef void(__fastcall *aioIob2Bi2xAC1_GetDeviceStatus_t)(AIO_IOB2_BI2X_AC1 *i_pNodeCtl,
                                                               VOID *o_DevStatus);
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
    typedef void (__fastcall *aioNCtlIob_GetNodeInfo_t)(AIO_IOB5_BI3A *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo, uint32_t i_cbNodeInfo);

    // libaio.dll
    typedef AIO_SCI_COMM_T *(__fastcall *aioSciComm_Open_t)(AIO_SCI_COMM_T *unk);
    typedef void(__fastcall *aioSci_Destroy_t)(AIO_SCI_COMM *i_pNodeMgr);
    typedef void(__fastcall *aioSci_GetCommStatus_t)(AIO_SCI_COMM *i_pNodeMgr, AIO_COMM_STATUS *i_Status);
    typedef void(__fastcall *aioNodeMgr_Destroy_t)(AIO_NMGR_IOB5 *i_pNodeMgr);
    typedef int32_t(__fastcall *aioNodeMgr_GetState_t)(AIO_NMGR_IOB5 *i_pNodeMgr);
    typedef bool(__fastcall *aioNodeMgr_IsReady_t)(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State);
    typedef bool(__fastcall *aioNodeMgr_IsError_t)(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State);
    typedef void (__fastcall *aioNodeCtl_Destroy_t)(AIO_IOB5_BI3A *i_pNodeCtl);
    typedef int32_t (__fastcall *aioNodeCtl_GetState_t)(AIO_IOB5_BI3A *i_pNodeCtl);
    typedef bool (__fastcall *aioNodeCtl_IsReady_t)(AIO_IOB5_BI3A *i_pNodeCtl, int32_t i_State);
    typedef bool (__fastcall *aioNodeCtl_IsError_t)(AIO_IOB5_BI3A *i_pNodeCtl, int32_t i_State);
    typedef void(__fastcall *aioNodeCtl_UpdateDevicesStatus_t)();

    // libaio-iob5.dll
    typedef AIO_NMGR_IOB5 *(__fastcall *aioNMgrIob5_Create_t)(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode);
    typedef AIO_IOB5_BI3A *(__fastcall *aioIob5Bi3a_Create_t)(AIO_NMGR_IOB5 *i_pNodeMgr, uint8_t i, void *p);
    typedef void(__fastcall *aioIob5Bi3a_GetDeviceStatus_t)(AIO_IOB5_BI3A *i_pNodeCtl, AIO_IOB5_BI3A__DEVSTATUS *o_DevStatus);

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
    static aioIob5Bi3a_Create_t aioIob5Bi3a_Create_orig = nullptr;
    static aioIob5Bi3a_GetDeviceStatus_t aioIob5Bi3a_GetDeviceStatus_orig = nullptr;

    // libaio-iob.dll
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;
    static aioNCtlIob_GetNodeInfo_t aioNCtlIob_GetNodeInfo_orig = nullptr;

    /*
     * variables
     */
    static int count = 0;
    static AIO_NMGR_IOB2 *aioNmgrIob2;
    static AIO_NMGR_IOB5 *aioNmgrIob5;
    static AIO_SCI_COMM *aioSciComm;
    static AIO_IOB2_BI2X_WRFIRM *aioIob2Bi2xWrfirm;
    static AIO_IOB5_BI3A *aioIob5Bi3a;

    /*
     * implementations
     */

    // libaio-iob2_video.dll

    static AIO_IOB2_BI2X_AC1 *__fastcall aioIob2Bi2xAC1_Create(AIO_NMGR_IOB2 *i_pNodeMgr, uint32_t i_DevId,
                                                               AIO_IOB2_BI2X_AC1__SETTING *i_Setting) {
                               
        log_fatal("bi2x_hook", "old io");
    }

    void __fastcall aioIob2Bi2xAC1_GetDeviceStatus(AIO_IOB2_BI2X_AC1 *i_pNodeCtl,
                                                   VOID *o_DevStatus) {
        
        log_fatal("bi2x_hook", "old io");
    }

    void __fastcall aioIob2Bi2xAC1_SetWatchDogTimer(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint8_t i_Count) {
        log_fatal("bi2x_hook", "old io");
    }

    void __fastcall aioIob2Bi2xAC1_AddCounter(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {
        log_fatal("bi2x_hook", "old io");
    }

    void __fastcall aioIob2Bi2xAC1_SetOutputData(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_CnPin, uint8_t i_Data) {
        log_fatal("bi2x_hook", "old io");
    }

    void __fastcall aioIob2Bi2xAC1_SetTapeLedDataPart(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_TapeLedCh,
                                                      uint32_t i_Offset, uint8_t *i_pData,
                                                      uint32_t i_cntTapeLed, bool i_bReverse) {
        log_fatal("bi2x_hook", "old io");
    }

    void __fastcall aioIob2Bi2x_SetTapeLedDataLimit(AIO_IOB2_BI2X_AC1 *i_pNodeCtl, uint32_t i_Channel,
                                                    uint8_t i_Scale, uint8_t i_Limit) {

        log_fatal("bi2x_hook", "old io");
    }

    static AIO_IOB2_BI2X_WRFIRM *__fastcall aioIob2Bi2x_CreateWriteFirmContext(
        uint32_t i_SerialNumber, uint32_t i_bfIob) {

        log_fatal("bi2x_hook", "old io");
        aioIob2Bi2xWrfirm = new AIO_IOB2_BI2X_WRFIRM;
        log_info("bi2x_hook", "aioIob2Bi2x_CreateWriteFirmContext called, returning custom AIO_IOB2_BI2X_WRFIRM: {}", fmt::ptr(aioIob2Bi2xWrfirm));
        return aioIob2Bi2xWrfirm;
    }

    static void __fastcall aioIob2Bi2x_DestroyWriteFirmContext(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm) {

        log_fatal("bi2x_hook", "old io");
        if (i_pWrFirm == aioIob2Bi2xWrfirm) {
            delete aioIob2Bi2xWrfirm;
            aioIob2Bi2xWrfirm = nullptr;
        } else {
            return aioIob2Bi2x_DestroyWriteFirmContext_orig(i_pWrFirm);
        }
    }

    static int32_t __fastcall aioIob2Bi2x_WriteFirmGetState(AIO_IOB2_BI2X_WRFIRM *i_pWrFirm) {

        log_fatal("bi2x_hook", "old io");
        if (i_pWrFirm == aioIob2Bi2xWrfirm) {
            return 8;
        } else {
            return aioIob2Bi2x_WriteFirmGetState_orig(i_pWrFirm);
        }
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsCompleted(int32_t i_State) {

        log_fatal("bi2x_hook", "old io");
        if (aioIob2Bi2xWrfirm != nullptr) {
            return true;
        } else {
            return aioIob2Bi2x_WriteFirmIsCompleted_orig(i_State);
        }
    }

    // libaio-iob.dll
    static AIO_NMGR_IOB2 *__fastcall aioNMgrIob2_Create(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode) {
        log_fatal("bi2x_hook", "old io");
    }

    static void __fastcall aioNCtlIob_GetNodeInfo(AIO_IOB5_BI3A *i_pNodeCtl, AIO_NMGR_IOB__NODEINFO *o_NodeInfo, uint32_t i_cbNodeInfo) {
        log_info("bi2x_hook", "aioNCtlIob_GetNodeInfo called with i_pNodeCtl={}, o_NodeInfo={}, i_cbNodeInfo={}", fmt::ptr(i_pNodeCtl), fmt::ptr(o_NodeInfo), i_cbNodeInfo);
        if (i_pNodeCtl == aioIob5Bi3a) {
            memset(o_NodeInfo, 0, sizeof(AIO_NMGR_IOB__NODEINFO));
        } else {
            return aioNCtlIob_GetNodeInfo_orig(i_pNodeCtl, o_NodeInfo, i_cbNodeInfo);
        }
    }

    // libaio.dll

    static AIO_SCI_COMM_T *__fastcall aioSciComm_Open(AIO_SCI_COMM_T *unk) {

        aioSciComm = new AIO_SCI_COMM;
        log_info("bi2x_hook", "aioSciComm_Open called, returning custom AIO_SCI_COMM_T: {}", fmt::ptr(unk));
        memset(unk, 0, sizeof(AIO_SCI_COMM_T));
        unk->aioSciComm = aioSciComm;
        return unk;
    }

    static void __fastcall aioSci_GetCommStatus(AIO_SCI_COMM *i_pNodeMgr, AIO_COMM_STATUS *i_Status) {

        log_info("bi2x_hook", "aioSci_GetCommStatus called with i_pNodeMgr={}, i_Status={}", fmt::ptr(i_pNodeMgr), fmt::ptr(i_Status));
        if (i_pNodeMgr == aioSciComm) {
            memset(i_Status, 0, sizeof(AIO_COMM_STATUS));
        } else {
            return aioSci_GetCommStatus_orig(i_pNodeMgr, i_Status);
        }
    }

    static void __fastcall aioSci_Destroy(AIO_SCI_COMM *i_pNodeMgr) {
      
        log_info("bi2x_hook", "aioSci_Destroy called with i_pNodeMgr={}", fmt::ptr(i_pNodeMgr));
        if (i_pNodeMgr == aioSciComm) {
            delete aioSciComm;
        } else {
            return aioSci_Destroy_orig(i_pNodeMgr);
        }
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB5 *i_pNodeMgr) {

        log_info("bi2x_hook", "aioNodeMgr_Destroy called with i_pNodeMgr={}", fmt::ptr(i_pNodeMgr));

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            delete aioNmgrIob5;
            aioNmgrIob5 = nullptr;
        } else {
            return aioNodeMgr_Destroy_orig(i_pNodeMgr);
        }
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB5 *i_pNodeMgr) {

        log_info("bi2x_hook", "aioNodeMgr_GetState called with i_pNodeMgr={}", fmt::ptr(i_pNodeMgr));

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {
            return 1;
        } else {
            return aioNodeMgr_GetState_orig(i_pNodeMgr);
        }
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State) {

        log_info("bi2x_hook", "aioNodeMgr_IsReady called with i_pNodeMgr={}, i_State={}", fmt::ptr(i_pNodeMgr), i_State);

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            return true;
        } else {
            return aioNodeMgr_IsReady_orig(i_pNodeMgr, i_State);
        }
    }

    static bool __fastcall aioNodeMgr_IsError(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State) {

        log_info("bi2x_hook", "aioNodeMgr_IsError called with i_pNodeMgr={}, i_State={}", fmt::ptr(i_pNodeMgr), i_State);

        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            return false;
        } else {
            return aioNodeMgr_IsError_orig(i_pNodeMgr, i_State);
        }
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB5_BI3A *i_pNodeCtl) {

        log_info("bi2x_hook", "aioNodeCtl_Destroy called with i_pNodeCtl={}", fmt::ptr(i_pNodeCtl));

        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioNodeCtl_Destroy_orig(i_pNodeCtl);
        }
        delete aioIob5Bi3a;
        aioIob5Bi3a = nullptr;
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB5_BI3A *i_pNodeCtl) {
        log_info("bi2x_hook", "aioNodeCtl_GetState called with i_pNodeCtl={}", fmt::ptr(i_pNodeCtl));

        if (i_pNodeCtl == aioIob5Bi3a) {
            return 1;
        }
        return aioNodeCtl_GetState_orig(i_pNodeCtl);
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB5_BI3A *i_pNodeCtl, int32_t i_State) {
        log_info("bi2x_hook", "aioNodeCtl_IsReady called with i_pNodeCtl={}, i_State={}", fmt::ptr(i_pNodeCtl), i_State);
        if (i_pNodeCtl == aioIob5Bi3a) {
            return true;
        }
        return aioNodeCtl_IsReady_orig(i_pNodeCtl, i_State);
    }

    static bool __fastcall aioNodeCtl_IsError(AIO_IOB5_BI3A *i_pNodeCtl, int32_t i_State) {
        log_info("bi2x_hook", "aioNodeCtl_IsError called with i_pNodeCtl={}, i_State={}", fmt::ptr(i_pNodeCtl), i_State);
        if (i_pNodeCtl == aioIob5Bi3a) {
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
            log_info("bi2x_hook", "aioNMgrIob5_Create called, returning custom AIO_NMGR_IOB5: {}", fmt::ptr(aioNmgrIob5));
            return aioNmgrIob5;
        } else {
            return aioNMgrIob5_Create_orig(i_pSci, i_bfMode);
        }
    }

    static AIO_IOB5_BI3A *__fastcall aioIob5Bi3a_Create(AIO_NMGR_IOB5 *i_pNodeMgr, uint8_t i, void *p) {

        log_info("bi2x_hook", "aioIob5Bi3a_Create called with i_pNodeMgr={}, i={}, p={}", fmt::ptr(i_pNodeMgr), i, fmt::ptr(p));

        if (i_pNodeMgr == aioNmgrIob5) {
            aioIob5Bi3a = new AIO_IOB5_BI3A;
            log_info("bi2x_hook", "aioIob5Bi3a_Create: returning custom AIO_IOB5_BI3A: {}", fmt::ptr(aioIob5Bi3a));
            return aioIob5Bi3a;
        } else {
            return aioIob5Bi3a_Create_orig(i_pNodeMgr, i, p);
        }
    }

    static void __fastcall aioIob5Bi3a_GetDeviceStatus(AIO_IOB5_BI3A *i_pNodeCtl, AIO_IOB5_BI3A__DEVSTATUS *o_DevStatus) {

        log_info("bi2x_hook", "aioIob5Bi3a_GetDeviceStatus called with i_pNodeCtl={}, o_DevStatus={}", fmt::ptr(i_pNodeCtl), fmt::ptr(o_DevStatus));

        RI_MGR->devices_flush_output();
        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioIob5Bi3a_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus);
        }

        memset(o_DevStatus, 0, sizeof(*o_DevStatus));
        
        // TODO: is this needed?
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

        // TODO: buttons b1-9 don't work for some reason
        // see table at 0x1804D7180 in M39-004-2025121500
        // third integer is 1 for these, setting them to 0 make them work
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button1])) {
            o_DevStatus->Input.Buttons[13] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button2])) {
            o_DevStatus->Input.Buttons[14] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button3])) {
            o_DevStatus->Input.Buttons[15] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button4])) {
            o_DevStatus->Input.Buttons[16] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button5])) {
            o_DevStatus->Input.Buttons[17] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button6])) {
            o_DevStatus->Input.Buttons[18] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button7])) {
            o_DevStatus->Input.Buttons[19] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button8])) {
            o_DevStatus->Input.Buttons[20] = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button9])) {
            o_DevStatus->Input.Buttons[21] = 1;
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
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_Create",
                               aioIob5Bi3a_Create, &aioIob5Bi3a_Create_orig);
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_GetDeviceStatus",
                               aioIob5Bi3a_GetDeviceStatus, &aioIob5Bi3a_GetDeviceStatus_orig);

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
