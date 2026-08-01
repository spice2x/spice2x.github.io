#include "bi2x_hook.h"

#if SPICE64

#include <array>
#include <cstdint>
#include <cstring>

#include "games/io.h"
#include "games/jb/jb_touch.h"
#include "io.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/logging.h"

namespace games::jb {

    struct AIO_SCI_COMM {
        std::array<uint8_t, 0x100> data;
    };

    struct AIO_NMGR_IOB2;

    struct AIO_NMGR_IOB2_VTABLE {
        std::array<void *, 10> unused;
        void (__fastcall *begin_manage)(AIO_NMGR_IOB2 *node_mgr);
    };

    struct AIO_NMGR_IOB2 {
        AIO_NMGR_IOB2_VTABLE *vtable;
        std::array<uint8_t, 0x78> data;
    };

    struct AIO_IOB2_BI2X_T44 {
        std::array<uint8_t, 0x80> data;
    };

    struct AIO_IOB2_BI2X_T44_DEVSTATUS {
        std::array<uint8_t, 0x140> data;
    };

    struct AIO_IOB2_BI2X_WRFIRM {
        uint8_t data;
    };

    static_assert(sizeof(AIO_NMGR_IOB2) == 0x80);
    static_assert(sizeof(AIO_IOB2_BI2X_T44_DEVSTATUS) == 0x140);

    using aioIob2Bi2xT44_Create_t = AIO_IOB2_BI2X_T44 *(__fastcall *)(AIO_NMGR_IOB2 *node_mgr,
            uint32_t device_id);
    using aioIob2Bi2xT44_GetDeviceStatus_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node,
            AIO_IOB2_BI2X_T44_DEVSTATUS *status);
    using aioIob2Bi2xT44_IoReset_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node, uint32_t reset);
    using aioIob2Bi2xT44_SetWatchDogTimer_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node, uint8_t count);
    using aioIob2Bi2xT44_ControlCoinBlocker_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node,
            uint32_t slot, bool open);
    using aioIob2Bi2xT44_AddCounter_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node,
            uint32_t counter, uint32_t count);
    using aioIob2Bi2xT44_SetIccrLed_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node, uint32_t color);
    using aioIob2Bi2xT44_SetTapeLedData_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node,
            uint32_t tape, const void *data);
    using aioIob2Bi2x_OpenSciUsbCdc_t = AIO_SCI_COMM *(__fastcall *)(uint32_t serial_number);
    using aioIob2Bi2x_CreateWriteFirmContext_t = AIO_IOB2_BI2X_WRFIRM *(__fastcall *)(
            uint32_t serial_number, uint32_t iob_mask);
    using aioIob2Bi2x_DestroyWriteFirmContext_t = void (__fastcall *)(AIO_IOB2_BI2X_WRFIRM *context);
    using aioIob2Bi2x_WriteFirmGetState_t = int32_t (__fastcall *)(AIO_IOB2_BI2X_WRFIRM *context);
    using aioIob2Bi2x_WriteFirmIsCompleted_t = bool (__fastcall *)(int32_t state);
    using aioIob2Bi2x_WriteFirmIsError_t = bool (__fastcall *)(int32_t state);
    using aioNMgrIob2_Create_t = AIO_NMGR_IOB2 *(__fastcall *)(AIO_SCI_COMM *sci, uint32_t mode);
    using aioSci_Destroy_t = void (__fastcall *)(AIO_SCI_COMM *sci);
    using aioNodeMgr_Destroy_t = void (__fastcall *)(AIO_NMGR_IOB2 *node_mgr);
    using aioNodeCtl_Destroy_t = void (__fastcall *)(AIO_IOB2_BI2X_T44 *node);
    using aioNodeCtl_UpdateDevicesStatus_t = void (__fastcall *)();

    static aioIob2Bi2xT44_Create_t aioIob2Bi2xT44_Create_orig = nullptr;
    static aioIob2Bi2xT44_GetDeviceStatus_t aioIob2Bi2xT44_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xT44_IoReset_t aioIob2Bi2xT44_IoReset_orig = nullptr;
    static aioIob2Bi2xT44_SetWatchDogTimer_t aioIob2Bi2xT44_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xT44_ControlCoinBlocker_t aioIob2Bi2xT44_ControlCoinBlocker_orig = nullptr;
    static aioIob2Bi2xT44_AddCounter_t aioIob2Bi2xT44_AddCounter_orig = nullptr;
    static aioIob2Bi2xT44_SetIccrLed_t aioIob2Bi2xT44_SetIccrLed_orig = nullptr;
    static aioIob2Bi2xT44_SetTapeLedData_t aioIob2Bi2xT44_SetTapeLedData_orig = nullptr;
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_DestroyWriteFirmContext_t aioIob2Bi2x_DestroyWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsCompleted_t aioIob2Bi2x_WriteFirmIsCompleted_orig = nullptr;
    static aioIob2Bi2x_WriteFirmIsError_t aioIob2Bi2x_WriteFirmIsError_orig = nullptr;
    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;
    static aioSci_Destroy_t aioSci_Destroy_orig = nullptr;
    static aioNodeMgr_Destroy_t aioNodeMgr_Destroy_orig = nullptr;
    static aioNodeCtl_Destroy_t aioNodeCtl_Destroy_orig = nullptr;
    static aioNodeCtl_UpdateDevicesStatus_t aioNodeCtl_UpdateDevicesStatus_orig = nullptr;

    static AIO_SCI_COMM *aio_sci_comm = nullptr;
    static AIO_NMGR_IOB2 *aio_node_mgr = nullptr;
    static AIO_IOB2_BI2X_T44 *aio_t44 = nullptr;
    static AIO_IOB2_BI2X_WRFIRM *aio_write_firm = nullptr;
    static uint8_t input_counter = 0;

    static void __fastcall aioNMgrIob_BeginManage(AIO_NMGR_IOB2 *) {
    }

    static AIO_NMGR_IOB2_VTABLE aio_node_mgr_vtable {
        {},
        aioNMgrIob_BeginManage,
    };

    static AIO_SCI_COMM *__fastcall aioIob2Bi2x_OpenSciUsbCdc(uint32_t) {
        aio_sci_comm = new AIO_SCI_COMM {};
        return aio_sci_comm;
    }

    static AIO_NMGR_IOB2 *__fastcall aioNMgrIob2_Create(AIO_SCI_COMM *sci, uint32_t mode) {
        if (sci != aio_sci_comm) {
            return aioNMgrIob2_Create_orig(sci, mode);
        }

        aio_node_mgr = new AIO_NMGR_IOB2 {};
        aio_node_mgr->vtable = &aio_node_mgr_vtable;
        return aio_node_mgr;
    }

    static AIO_IOB2_BI2X_T44 *__fastcall aioIob2Bi2xT44_Create(
            AIO_NMGR_IOB2 *node_mgr, uint32_t device_id) {
        if (node_mgr != aio_node_mgr) {
            return aioIob2Bi2xT44_Create_orig(node_mgr, device_id);
        }

        log_info("jb::bi2x", "T44 node created");
        aio_t44 = new AIO_IOB2_BI2X_T44 {};
        return aio_t44;
    }

    static void __fastcall aioIob2Bi2xT44_GetDeviceStatus(
            AIO_IOB2_BI2X_T44 *node, AIO_IOB2_BI2X_T44_DEVSTATUS *status) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_GetDeviceStatus_orig(node, status);
        }

        RI_MGR->devices_flush_output();
        std::memset(status, 0, sizeof(*status));

        status->data[0x00] = input_counter;
        status->data[0x03] = input_counter++;
        status->data[0x0A] = static_cast<uint8_t>(eamuse_coin_get_stock());

        games::jb::touch_update();
        const auto touched = games::jb::touch_state();
        auto &buttons = get_buttons();

        status->data[0x04] = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]) ? 0xFF : 0;
        status->data[0x05] = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]) ? 0xFF : 0;
        status->data[0x06] = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]) ? 0xFF : 0;

        static constexpr size_t PANEL_ORDER[16] = {
            3, 7, 11, 15,
            2, 6, 10, 14,
            1, 5, 9, 13,
            0, 4, 8, 12,
        };
        for (size_t status_index = 0; status_index < std::size(PANEL_ORDER); status_index++) {
            const auto panel_index = PANEL_ORDER[status_index];
            if (touched[panel_index] || GameAPI::Buttons::getState(
                    RI_MGR, buttons[Buttons::Button1 + panel_index])) {
                status->data[0x0F + status_index] = 0xFF;
            }
        }
    }

    static void __fastcall aioIob2Bi2xT44_IoReset(AIO_IOB2_BI2X_T44 *node, uint32_t reset) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_IoReset_orig(node, reset);
        }
    }

    static void __fastcall aioIob2Bi2xT44_SetWatchDogTimer(AIO_IOB2_BI2X_T44 *node, uint8_t count) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_SetWatchDogTimer_orig(node, count);
        }
    }

    static void __fastcall aioIob2Bi2xT44_ControlCoinBlocker(
            AIO_IOB2_BI2X_T44 *node, uint32_t slot, bool open) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_ControlCoinBlocker_orig(node, slot, open);
        }

        eamuse_coin_set_block(!open);
    }

    static void __fastcall aioIob2Bi2xT44_AddCounter(
            AIO_IOB2_BI2X_T44 *node, uint32_t counter, uint32_t count) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_AddCounter_orig(node, counter, count);
        }
    }

    static void __fastcall aioIob2Bi2xT44_SetIccrLed(AIO_IOB2_BI2X_T44 *node, uint32_t color) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_SetIccrLed_orig(node, color);
        }
    }

    static void __fastcall aioIob2Bi2xT44_SetTapeLedData(
            AIO_IOB2_BI2X_T44 *node, uint32_t tape, const void *data) {
        if (node != aio_t44) {
            return aioIob2Bi2xT44_SetTapeLedData_orig(node, tape, data);
        }
    }

    static AIO_IOB2_BI2X_WRFIRM *__fastcall aioIob2Bi2x_CreateWriteFirmContext(uint32_t, uint32_t) {
        aio_write_firm = new AIO_IOB2_BI2X_WRFIRM {};
        return aio_write_firm;
    }

    static void __fastcall aioIob2Bi2x_DestroyWriteFirmContext(AIO_IOB2_BI2X_WRFIRM *context) {
        if (context != aio_write_firm) {
            return aioIob2Bi2x_DestroyWriteFirmContext_orig(context);
        }

        delete aio_write_firm;
        aio_write_firm = nullptr;
    }

    static int32_t __fastcall aioIob2Bi2x_WriteFirmGetState(AIO_IOB2_BI2X_WRFIRM *context) {
        if (context != aio_write_firm) {
            return aioIob2Bi2x_WriteFirmGetState_orig(context);
        }
        return 8;
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsCompleted(int32_t state) {
        if (aio_write_firm) {
            return true;
        }
        return aioIob2Bi2x_WriteFirmIsCompleted_orig(state);
    }

    static bool __fastcall aioIob2Bi2x_WriteFirmIsError(int32_t state) {
        if (aio_write_firm) {
            return false;
        }
        return aioIob2Bi2x_WriteFirmIsError_orig(state);
    }

    static void __fastcall aioSci_Destroy(AIO_SCI_COMM *sci) {
        if (sci != aio_sci_comm) {
            return aioSci_Destroy_orig(sci);
        }

        delete aio_sci_comm;
        aio_sci_comm = nullptr;
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB2 *node_mgr) {
        if (node_mgr != aio_node_mgr) {
            return aioNodeMgr_Destroy_orig(node_mgr);
        }

        delete aio_node_mgr;
        aio_node_mgr = nullptr;
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB2_BI2X_T44 *node) {
        if (node != aio_t44) {
            return aioNodeCtl_Destroy_orig(node);
        }

        delete aio_t44;
        aio_t44 = nullptr;
    }

    static void __fastcall aioNodeCtl_UpdateDevicesStatus() {
    }

    void bi2x_hook_init() {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;

        log_info("jb::bi2x", "initializing T44 hooks");

        const auto libaio_iob2_video = "libaio-iob2_video.dll";
        detour::trampoline_try(libaio_iob2_video, "aioIob2Bi2xT44_Create",
                aioIob2Bi2xT44_Create, &aioIob2Bi2xT44_Create_orig);
        detour::trampoline_try(libaio_iob2_video,
                "?GetDeviceStatus@AIO_IOB2_BI2X_T44@@QEBAXAEAUDEVSTATUS@1@@Z",
                aioIob2Bi2xT44_GetDeviceStatus, &aioIob2Bi2xT44_GetDeviceStatus_orig);
        detour::trampoline_try(libaio_iob2_video, "?IoReset@AIO_IOB2_BI2X_T44@@QEAAXI@Z",
                aioIob2Bi2xT44_IoReset, &aioIob2Bi2xT44_IoReset_orig);
        detour::trampoline_try(libaio_iob2_video,
                "?SetWatchDogTimer@AIO_IOB2_BI2X_T44@@QEAAXE@Z",
                aioIob2Bi2xT44_SetWatchDogTimer, &aioIob2Bi2xT44_SetWatchDogTimer_orig);
        detour::trampoline_try(libaio_iob2_video,
                "?ControlCoinBlocker@AIO_IOB2_BI2X_T44@@QEAAXI_N@Z",
                aioIob2Bi2xT44_ControlCoinBlocker, &aioIob2Bi2xT44_ControlCoinBlocker_orig);
        detour::trampoline_try(libaio_iob2_video, "?AddCounter@AIO_IOB2_BI2X_T44@@QEAAXII@Z",
                aioIob2Bi2xT44_AddCounter, &aioIob2Bi2xT44_AddCounter_orig);
        detour::trampoline_try(libaio_iob2_video, "?SetIccrLed@AIO_IOB2_BI2X_T44@@QEAAXI@Z",
                aioIob2Bi2xT44_SetIccrLed, &aioIob2Bi2xT44_SetIccrLed_orig);
        detour::trampoline_try(libaio_iob2_video,
                "?SetTapeLedData@AIO_IOB2_BI2X_T44@@QEAAXIPEBX@Z",
                aioIob2Bi2xT44_SetTapeLedData, &aioIob2Bi2xT44_SetTapeLedData_orig);

        const auto libaio_iob = "libaio-iob.dll";
        detour::trampoline_try(libaio_iob, "aioIob2Bi2x_OpenSciUsbCdc",
                aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        detour::trampoline_try(libaio_iob, "aioIob2Bi2x_CreateWriteFirmContext",
                aioIob2Bi2x_CreateWriteFirmContext, &aioIob2Bi2x_CreateWriteFirmContext_orig);
        detour::trampoline_try(libaio_iob, "aioIob2Bi2x_DestroyWriteFirmContext",
                aioIob2Bi2x_DestroyWriteFirmContext, &aioIob2Bi2x_DestroyWriteFirmContext_orig);
        detour::trampoline_try(libaio_iob, "aioIob2Bi2x_WriteFirmGetState",
                aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        detour::trampoline_try(libaio_iob, "aioIob2Bi2x_WriteFirmIsCompleted",
                aioIob2Bi2x_WriteFirmIsCompleted, &aioIob2Bi2x_WriteFirmIsCompleted_orig);
        detour::trampoline_try(libaio_iob, "aioIob2Bi2x_WriteFirmIsError",
                aioIob2Bi2x_WriteFirmIsError, &aioIob2Bi2x_WriteFirmIsError_orig);
        detour::trampoline_try(libaio_iob, "aioNMgrIob2_Create",
                aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);

        const auto libaio = "libaio.dll";
        detour::trampoline_try(libaio, "aioSci_Destroy", aioSci_Destroy, &aioSci_Destroy_orig);
        detour::trampoline_try(libaio, "aioNodeMgr_Destroy",
                aioNodeMgr_Destroy, &aioNodeMgr_Destroy_orig);
        detour::trampoline_try(libaio, "aioNodeCtl_Destroy",
                aioNodeCtl_Destroy, &aioNodeCtl_Destroy_orig);
        detour::trampoline_try(libaio, "aioNodeCtl_UpdateDevicesStatus",
                aioNodeCtl_UpdateDevicesStatus, &aioNodeCtl_UpdateDevicesStatus_orig);
    }
}

#endif