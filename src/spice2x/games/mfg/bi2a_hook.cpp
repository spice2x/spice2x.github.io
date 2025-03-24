#include "bi2a_hook.h"

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

    struct AIO_IOB_BI2A_VFG {
    };

    struct AIO_IOB_BI2A_VFG__INPUTDATA {
        uint8_t data[16];
    };

    struct AIO_IOB_BI2A_VFG__OUTPUTDATA {
        uint8_t data[40];
    };

    struct AIO_IOB_BI2A_VFG__INPUT
    {
        uint8_t bPcPowerCheck;
        uint8_t CoinCount;
        uint8_t bTest;
        uint8_t bService;
        uint8_t bCoinSw;
        uint8_t bCoinJam;
        uint8_t bCabinet;
        uint8_t bHPDetect;
        uint8_t bRecDetect;
        uint8_t bStart;
    };

    struct AIO_IOB_BI2A_VFG__DEVSTATUS
    {
        uint8_t MechType;
        uint8_t bIsBi2a;
        uint8_t IoCounter;
        AIO_IOB_BI2A_VFG__INPUT Input;
        AIO_IOB_BI2A_VFG__INPUTDATA InputData;
        AIO_IOB_BI2A_VFG__OUTPUTDATA OutputData;
    };

    /*
     * typedefs
     */

    // libaio-iob_video.dll
    typedef AIO_SCI_COMM* (__fastcall *aioIobBi2a_OpenSciUsbCdc_t)(uint32_t i_SerialNumber);
    typedef AIO_IOB_BI2A_VFG* (__fastcall *aioIobBi2aVFG_Create_t)(AIO_NMGR_IOB *i_pNodeMgr, uint32_t i_DevId);
    typedef void (__fastcall *aioIobBi2aVFG_SetMechType_t)(AIO_IOB_BI2A_VFG *i_pNodeCtl, uint32_t i_MechType);
    typedef void (__fastcall *aioIobBi2aVFG_GetDeviceStatus_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl,
                                                               AIO_IOB_BI2A_VFG__DEVSTATUS *o_pDevStatus,
                                                               uint32_t i_cbDevStatus);
    typedef void (__fastcall *aioIobBi2aVFG_SetWatchDogTimer_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint16_t i_Count);
    typedef void (__fastcall *aioIobBi2aVFG_ControlCoinBlocker_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Slot,
                                                                  bool i_bOpen);
    typedef void (__fastcall *aioIobBi2aVFG_AddCounter_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Counter,
                                                          uint32_t i_Count);
    typedef void (__fastcall *aioIobBi2aVFG_SetAmpVolume_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Amp,
                                                            uint32_t i_Volume);
    typedef void (__fastcall *aioIobBi2aVFG_EnableUsbCharger_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, bool i_bEnable);
    typedef void (__fastcall *aioIobBi2aVFG_SetLamp_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Lamp, uint8_t i_Bright);
    typedef void (__fastcall *aioIobBi2aVFG_SetIccrLed_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIobBi2aVFG_SetLedData_t)(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint8_t *i_pData,
                                                          uint32_t i_cbData);

    // libaio-iob.dll
    typedef AIO_NMGR_IOB* (__fastcall *aioNMgrIob_Create_t)(AIO_SCI_COMM* i_pSci, uint32_t i_bfMode);

    // libaio.dll
    typedef void (__fastcall *aioNodeMgr_Destroy_t)(AIO_NMGR_IOB *i_pNodeMgr);
    typedef int32_t (__fastcall *aioNodeMgr_GetState_t)(AIO_NMGR_IOB *i_pNodeMgr);
    typedef bool (__fastcall *aioNodeMgr_IsReady_t)(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State);
    typedef bool (__fastcall *aioNodeMgr_IsError_t)(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State);
    typedef void (__fastcall *aioNodeCtl_Destroy_t)(AIO_IOB_BI2A_VFG *i_pNodeCtl);
    typedef int32_t (__fastcall *aioNodeCtl_GetState_t)(AIO_IOB_BI2A_VFG *i_pNodeCtl);
    typedef bool (__fastcall *aioNodeCtl_IsReady_t)(AIO_IOB_BI2A_VFG *i_pNodeCtl, int32_t i_State);
    typedef bool (__fastcall *aioNodeCtl_IsError_t)(AIO_IOB_BI2A_VFG *i_pNodeCtl, int32_t i_State);

    /*
     * function pointers
     */

    // libaio-iob_video.dll
    static aioIobBi2a_OpenSciUsbCdc_t aioIobBi2a_OpenSciUsbCdc_orig = nullptr;
    static aioIobBi2aVFG_Create_t aioIobBi2aVFG_Create_orig = nullptr;
    static aioIobBi2aVFG_SetMechType_t aioIobBi2aVFG_SetMechType_orig = nullptr;
    static aioIobBi2aVFG_GetDeviceStatus_t aioIobBi2aVFG_GetDeviceStatus_orig = nullptr;
    static aioIobBi2aVFG_SetWatchDogTimer_t aioIobBi2aVFG_SetWatchDogTimer_orig = nullptr;
    static aioIobBi2aVFG_ControlCoinBlocker_t aioIobBi2aVFG_ControlCoinBlocker_orig = nullptr;
    static aioIobBi2aVFG_AddCounter_t aioIobBi2aVFG_AddCounter_orig = nullptr;
    static aioIobBi2aVFG_SetAmpVolume_t aioIobBi2aVFG_SetAmpVolume_orig = nullptr;
    static aioIobBi2aVFG_EnableUsbCharger_t aioIobBi2aVFG_EnableUsbCharger_orig = nullptr;
    static aioIobBi2aVFG_SetLamp_t aioIobBi2aVFG_SetLamp_orig = nullptr;
    static aioIobBi2aVFG_SetIccrLed_t aioIobBi2aVFG_SetIccrLed_orig = nullptr;
    static aioIobBi2aVFG_SetLedData_t aioIobBi2aVFG_SetLedData_orig = nullptr;

    // libaio-iob.dll
    static aioNMgrIob_Create_t aioNMgrIob_Create_orig = nullptr;

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
    static AIO_NMGR_IOB *aioNmgrIob;
    static AIO_IOB_BI2A_VFG *aioIobBi2aVfg;
    static uint8_t mechType = 0;
    static uint8_t count = 0;

    /*
     * implementations
     */

    static AIO_SCI_COMM* __fastcall aioIob2Bi2a_OpenSciUsbCdc(uint32_t i_SerialNumber) {
        log_info("bi2a_hook", "aioIob2Bi2a_OpenSciUsbCdc hook hit");
        aioSciComm = new AIO_SCI_COMM;
        return aioSciComm;
    }

    static AIO_IOB_BI2A_VFG* __fastcall aioIobBi2aVFG_Create(AIO_NMGR_IOB *i_pNodeMgr, uint32_t i_DevId) {
        if (i_pNodeMgr == aioNmgrIob) {
            log_info("bi2a_hook", "node created");
            aioIobBi2aVfg = new AIO_IOB_BI2A_VFG;
            return aioIobBi2aVfg;
        } else {
            return aioIobBi2aVFG_Create_orig(i_pNodeMgr, i_DevId);
        }
    }

    void __fastcall aioIobBi2aVFG_SetMechType(AIO_IOB_BI2A_VFG *i_pNodeCtl, uint32_t i_MechType) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
            mechType = i_MechType;
        } else {
            return aioIobBi2aVFG_SetMechType_orig(i_pNodeCtl, i_MechType);
        }
    }

    void __fastcall aioIobBi2aVFG_GetDeviceStatus(AIO_IOB_BI2A_VFG* i_pNodeCtl,
                                                  AIO_IOB_BI2A_VFG__DEVSTATUS *o_pDevStatus,
                                                  uint32_t i_cbDevStatus) {

        RI_MGR->devices_flush_output();

        if (i_pNodeCtl != aioIobBi2aVfg) {
            return aioIobBi2aVFG_GetDeviceStatus_orig(i_pNodeCtl, o_pDevStatus, i_cbDevStatus);
        }

        memset(o_pDevStatus, 0x00, sizeof(AIO_IOB_BI2A_VFG__DEVSTATUS));

        o_pDevStatus->MechType = mechType;
        o_pDevStatus->bIsBi2a = 1;
        o_pDevStatus->IoCounter = count++;

        auto &buttons = get_buttons();
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]))
            o_pDevStatus->Input.bTest = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]))
            o_pDevStatus->Input.bService = 1;
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]))
            o_pDevStatus->Input.bCoinSw = 1;

        o_pDevStatus->Input.CoinCount = eamuse_coin_get_stock();
    }

    void __fastcall aioIobBi2aVFG_SetWatchDogTimer(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint16_t i_Count) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_SetWatchDogTimer_orig(i_pNodeCtl, i_Count);
        }
    }

    void __fastcall aioIobBi2aVFG_ControlCoinBlocker(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Slot, bool i_bOpen) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_ControlCoinBlocker_orig(i_pNodeCtl, i_Slot, i_bOpen);
        }
    }

    void __fastcall aioIobBi2aVFG_AddCounter(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Counter, uint32_t i_Count) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_AddCounter_orig(i_pNodeCtl, i_Counter, i_Count);
        }
    }

    void __fastcall aioIobBi2aVFG_SetAmpVolume(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Amp, uint32_t i_Volume) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_SetAmpVolume_orig(i_pNodeCtl, i_Amp, i_Volume);
        }
    }

    void __fastcall aioIobBi2aVFG_EnableUsbCharger(AIO_IOB_BI2A_VFG* i_pNodeCtl, bool i_bEnable) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_EnableUsbCharger_orig(i_pNodeCtl, i_bEnable);
        }
    }

    void __fastcall aioIobBi2aVFG_SetLamp(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_Lamp, uint8_t i_Bright) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_SetLamp_orig(i_pNodeCtl, i_Lamp, i_Bright);
        }
    }

    void __fastcall aioIobBi2aVFG_SetIccrLed(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint32_t i_RGB) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_SetIccrLed_orig(i_pNodeCtl, i_RGB);
        }
    }

    void __fastcall aioIobBi2aVFG_SetLedData(AIO_IOB_BI2A_VFG* i_pNodeCtl, uint8_t *i_pData, uint32_t i_cbData) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
        } else {
            return aioIobBi2aVFG_SetLedData_orig(i_pNodeCtl, i_pData, i_cbData);
        }
    }

    static AIO_NMGR_IOB* __fastcall aioNMgrIob_Create(AIO_SCI_COMM *i_pSci, uint32_t i_bfMode) {
        if (i_pSci == aioSciComm) {
            aioNmgrIob = new AIO_NMGR_IOB;
            return aioNmgrIob;
        } else {
            return aioNMgrIob_Create_orig(i_pSci, i_bfMode);
        }
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob) {
            delete aioNmgrIob;
            aioNmgrIob = nullptr;
        } else {
            return aioNodeMgr_Destroy_orig(i_pNodeMgr);
        }
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob) {
            return 1;
        } else {
            return aioNodeMgr_GetState_orig(i_pNodeMgr);
        }
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob) {
            return true;
        } else {
            return aioNodeMgr_IsReady_orig(i_pNodeMgr, i_State);
        }
    }

    static bool __fastcall aioNodeMgr_IsError(AIO_NMGR_IOB *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob) {
            return false;
        } else {
            return aioNodeMgr_IsError_orig(i_pNodeMgr, i_State);
        }
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB_BI2A_VFG *i_pNodeCtl) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
            delete aioIobBi2aVfg;
            aioIobBi2aVfg = nullptr;
        } else {
            return aioNodeCtl_Destroy_orig(i_pNodeCtl);
        }
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB_BI2A_VFG *i_pNodeCtl) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
            return 1;
        } else {
            return aioNodeCtl_GetState_orig(i_pNodeCtl);
        }
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB_BI2A_VFG *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
            return true;
        } else {
            return aioNodeCtl_IsReady_orig(i_pNodeCtl, i_State);
        }
    }

    static bool __fastcall aioNodeCtl_IsError(AIO_IOB_BI2A_VFG *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIobBi2aVfg) {
            return false;
        } else {
            return aioNodeCtl_IsError_orig(i_pNodeCtl, i_State);
        }
    }

    void bi2a_hook_init() {
        // avoid double init
        static bool initialized = false;
        if (initialized) {
            return;
        } else {
            initialized = true;
        }

        // announce
        log_info("bi2a_hook", "init");

        // libaio-iob_video.dll
        const auto libaioIobVideoDll = "libaio-iob_video.dll";
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2a_OpenSciUsbCdc",
                               aioIob2Bi2a_OpenSciUsbCdc, &aioIobBi2a_OpenSciUsbCdc_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_Create",
                               aioIobBi2aVFG_Create, &aioIobBi2aVFG_Create_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetMechType",
                               aioIobBi2aVFG_SetMechType, &aioIobBi2aVFG_SetMechType_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_GetDeviceStatus",
                               aioIobBi2aVFG_GetDeviceStatus, &aioIobBi2aVFG_GetDeviceStatus_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetWatchDogTimer",
                               aioIobBi2aVFG_SetWatchDogTimer, &aioIobBi2aVFG_SetWatchDogTimer_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_ControlCoinBlocker",
                               aioIobBi2aVFG_ControlCoinBlocker, &aioIobBi2aVFG_ControlCoinBlocker_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_AddCounter",
                               aioIobBi2aVFG_AddCounter, &aioIobBi2aVFG_AddCounter_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetAmpVolume",
                               aioIobBi2aVFG_SetAmpVolume, &aioIobBi2aVFG_SetAmpVolume_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_EnableUsbCharger",
                               aioIobBi2aVFG_EnableUsbCharger, &aioIobBi2aVFG_EnableUsbCharger_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetLamp",
                               aioIobBi2aVFG_SetLamp, &aioIobBi2aVFG_SetLamp_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetIccrLed",
                               aioIobBi2aVFG_SetIccrLed, &aioIobBi2aVFG_SetIccrLed_orig);
        execexe::trampoline_try(libaioIobVideoDll, "aioIobBi2aVFG_SetLedData",
                               aioIobBi2aVFG_SetLedData, &aioIobBi2aVFG_SetLedData_orig);

        // libaio-iob.dll
        const auto libaioIobDll = "libaio-iob.dll";
        execexe::trampoline_try(libaioIobDll, "aioNMgrIob_Create",
                               aioNMgrIob_Create, &aioNMgrIob_Create_orig);

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