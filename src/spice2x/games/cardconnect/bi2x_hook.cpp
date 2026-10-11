#include "bi2x_hook.h"

#if SPICE64

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "io.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/tapeled.h"

namespace games::cardconnect {

    struct AIO_SCI_COMM {
        uint8_t data[0xf8];
    };

    struct AIO_NMGR_IOB2 {
        uint8_t data[0x9f8];
    };

    struct AIO_IOB2_BI2X_TCS {
        uint8_t data[0x350];
    };

    struct AIO_IOB2_BI2X_WRFIRM {
        uint8_t data[0x20f48];
    };

#pragma pack(push, 1)
    struct AIO_IOB2_BI2X_TCS__INPUT {
        uint8_t DevIoCounter;
        uint8_t bTest;
        uint8_t bService;
        uint8_t bCoinSw;
        uint8_t bCoinJam;
        uint8_t CoinCount;
    };

    struct AIO_IOB2_BI2X_TCS__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        uint8_t TapeLedCounter;
        AIO_IOB2_BI2X_TCS__INPUT Input;
        uint8_t reserved[17];
    };
#pragma pack(pop)

    // The supplied native DLL copies 26 bytes, even though the managed wrapper requests 27.
    static_assert(sizeof(AIO_IOB2_BI2X_TCS__DEVSTATUS) == 26);
    static_assert(offsetof(AIO_IOB2_BI2X_TCS__DEVSTATUS, Input) == 3);
    static_assert(offsetof(AIO_IOB2_BI2X_TCS__INPUT, bTest) == 1);
    static_assert(offsetof(AIO_IOB2_BI2X_TCS__INPUT, bService) == 2);
    static_assert(offsetof(AIO_IOB2_BI2X_TCS__INPUT, bCoinSw) == 3);
    static_assert(offsetof(AIO_IOB2_BI2X_TCS__INPUT, CoinCount) == 5);

    static AIO_IOB2_BI2X_TCS *(__fastcall *aioIob2Bi2xTCS_Create_orig)(
            AIO_NMGR_IOB2 *, uint32_t) = nullptr;
    static uint32_t (__fastcall *aioIob2Bi2xTCS_GetDeviceStatus_orig)(
            AIO_IOB2_BI2X_TCS *, AIO_IOB2_BI2X_TCS__DEVSTATUS *, uint32_t) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_IoReset_orig)(
            AIO_IOB2_BI2X_TCS *, uint32_t) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_SetWatchDogTimer_orig)(
            AIO_IOB2_BI2X_TCS *, uint8_t) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_ControlCoinBlocker_orig)(
            AIO_IOB2_BI2X_TCS *, uint32_t, bool) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_AddCounter_orig)(
            AIO_IOB2_BI2X_TCS *, uint32_t, uint32_t) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_SetPhotoLight_orig)(
            AIO_IOB2_BI2X_TCS *, bool) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_SetIccrLed_orig)(
            AIO_IOB2_BI2X_TCS *, uint32_t) = nullptr;
    static void (__fastcall *aioIob2Bi2xTCS_SetTapeLedData_orig)(
            AIO_IOB2_BI2X_TCS *, uint32_t, uint8_t *) = nullptr;

    static AIO_SCI_COMM *(__fastcall *aioIob2Bi2x_OpenSciUsbCdc_orig)(uint32_t) = nullptr;
    static AIO_IOB2_BI2X_WRFIRM *(__fastcall *aioIob2Bi2x_CreateWriteFirmContext_orig)(
            uint32_t, uint32_t) = nullptr;
    static void (__fastcall *aioIob2Bi2x_DestroyWriteFirmContext_orig)(
            AIO_IOB2_BI2X_WRFIRM *) = nullptr;
    static int32_t (__fastcall *aioIob2Bi2x_WriteFirmGetState_orig)(
            AIO_IOB2_BI2X_WRFIRM *) = nullptr;
    static AIO_NMGR_IOB2 *(__fastcall *aioNMgrIob2_Create_orig)(
            AIO_SCI_COMM *, uint32_t) = nullptr;
    static void (__fastcall *aioNMgrIob_BeginManage_orig)(AIO_NMGR_IOB2 *) = nullptr;

    static void (__fastcall *aioNodeMgr_Destroy_orig)(AIO_NMGR_IOB2 *) = nullptr;
    static int32_t (__fastcall *aioNodeMgr_GetState_orig)(AIO_NMGR_IOB2 *) = nullptr;
    static bool (__fastcall *aioNodeMgr_IsReady_orig)(AIO_NMGR_IOB2 *, int32_t) = nullptr;
    static void (__fastcall *aioNodeCtl_Destroy_orig)(AIO_IOB2_BI2X_TCS *) = nullptr;
    static int32_t (__fastcall *aioNodeCtl_GetState_orig)(AIO_IOB2_BI2X_TCS *) = nullptr;
    static bool (__fastcall *aioNodeCtl_IsReady_orig)(AIO_IOB2_BI2X_TCS *, int32_t) = nullptr;
    static void (__fastcall *aioSci_Destroy_orig)(AIO_SCI_COMM *) = nullptr;

    static std::unique_ptr<AIO_SCI_COMM> aio_sci;
    static std::unique_ptr<AIO_NMGR_IOB2> aio_manager;
    static std::unique_ptr<AIO_IOB2_BI2X_TCS> aio_board;
    static std::unique_ptr<AIO_IOB2_BI2X_WRFIRM> aio_firmware;
    static uint8_t input_counter = 0;
    static bool coin_pressed = false;

    static void write_light(size_t index, float value) {
        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights[index], value);
    }

    static AIO_IOB2_BI2X_TCS *__fastcall aioIob2Bi2xTCS_Create(
            AIO_NMGR_IOB2 *manager, uint32_t device_id) {
        if (!aio_manager || manager != aio_manager.get()) {
            return aioIob2Bi2xTCS_Create_orig(manager, device_id);
        }
        if (!aio_board) {
            aio_board = std::make_unique<AIO_IOB2_BI2X_TCS>();
            input_counter = 0;
            coin_pressed = false;
            log_info("cardconnect::bi2x", "created emulated BI2X-TCS board");
        }
        return aio_board.get();
    }

    static uint32_t __fastcall aioIob2Bi2xTCS_GetDeviceStatus(
            AIO_IOB2_BI2X_TCS *board, AIO_IOB2_BI2X_TCS__DEVSTATUS *status,
            uint32_t status_size) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_GetDeviceStatus_orig(board, status, status_size);
        }
        if (!status && status_size > 0) {
            log_warning("cardconnect::bi2x", "GetDeviceStatus called with a null output buffer");
            return 0;
        }

        auto &buttons = get_buttons();
        const bool coin = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]);
        if (coin && !coin_pressed) {
            eamuse_coin_insert();
        }
        coin_pressed = coin;

        AIO_IOB2_BI2X_TCS__DEVSTATUS current {};
        current.InputCounter = ++input_counter;
        current.Input.DevIoCounter = input_counter;
        current.Input.bTest = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]) ? 1 : 0;
        current.Input.bService = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]) ? 1 : 0;
        current.Input.bCoinSw = coin ? 1 : 0;
        current.Input.CoinCount = static_cast<uint8_t>(eamuse_coin_get_stock());

        RI_MGR->devices_flush_output();

        const auto copy_size = std::min<uint32_t>(status_size, sizeof(current));
        if (copy_size > 0) {
            std::memcpy(status, &current, copy_size);
        }
        return copy_size;
    }

    static void __fastcall aioIob2Bi2xTCS_IoReset(AIO_IOB2_BI2X_TCS *board, uint32_t bits) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_IoReset_orig(board, bits);
        }
    }

    static void __fastcall aioIob2Bi2xTCS_SetWatchDogTimer(
            AIO_IOB2_BI2X_TCS *board, uint8_t count) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_SetWatchDogTimer_orig(board, count);
        }
    }

    static void __fastcall aioIob2Bi2xTCS_ControlCoinBlocker(
            AIO_IOB2_BI2X_TCS *board, uint32_t slot, bool open) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_ControlCoinBlocker_orig(board, slot, open);
        }
        if (slot != 0) {
            log_warning("cardconnect::bi2x", "unknown coin blocker slot {}", slot);
            return;
        }
        eamuse_coin_set_block(!open);
    }

    static void __fastcall aioIob2Bi2xTCS_AddCounter(
            AIO_IOB2_BI2X_TCS *board, uint32_t counter, uint32_t count) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_AddCounter_orig(board, counter, count);
        }
        // This drives the physical bookkeeping meter, not the input coin counter.
    }

    static void __fastcall aioIob2Bi2xTCS_SetPhotoLight(AIO_IOB2_BI2X_TCS *board, bool on) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_SetPhotoLight_orig(board, on);
        }
        write_light(Lights::PhotoLight, on ? 1.f : 0.f);
    }

    static void __fastcall aioIob2Bi2xTCS_SetIccrLed(AIO_IOB2_BI2X_TCS *board, uint32_t rgb) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_SetIccrLed_orig(board, rgb);
        }
        write_light(Lights::CardReaderR, ((rgb >> 16) & 0xff) / 255.f);
        write_light(Lights::CardReaderG, ((rgb >> 8) & 0xff) / 255.f);
        write_light(Lights::CardReaderB, (rgb & 0xff) / 255.f);
    }

    static void __fastcall aioIob2Bi2xTCS_SetTapeLedData(
            AIO_IOB2_BI2X_TCS *board, uint32_t tape, uint8_t *data) {
        if (!aio_board || board != aio_board.get()) {
            return aioIob2Bi2xTCS_SetTapeLedData_orig(board, tape, data);
        }
        if (!data || tape >= 2) {
            log_warning("cardconnect::bi2x", "invalid tape LED data for channel {}", tape);
            return;
        }
        if (tapeledutils::is_enabled()) {
            const auto color = tapeledutils::pick_color_from_led_tape(data, 15);
            const auto first = tape == 0 ? Lights::SideLeftR : Lights::SideRightR;
            write_light(first, color.r);
            write_light(first + 1, color.g);
            write_light(first + 2, color.b);
        }
    }

    static AIO_SCI_COMM *__fastcall aioIob2Bi2x_OpenSciUsbCdc(uint32_t serial_number) {
        if (!aio_sci) {
            aio_sci = std::make_unique<AIO_SCI_COMM>();
        }
        return aio_sci.get();
    }

    static AIO_IOB2_BI2X_WRFIRM *__fastcall aioIob2Bi2x_CreateWriteFirmContext(
            uint32_t serial_number, uint32_t iob_mask) {
        if (!aio_firmware) {
            aio_firmware = std::make_unique<AIO_IOB2_BI2X_WRFIRM>();
        }
        return aio_firmware.get();
    }

    static void __fastcall aioIob2Bi2x_DestroyWriteFirmContext(AIO_IOB2_BI2X_WRFIRM *context) {
        if (!aio_firmware || context != aio_firmware.get()) {
            return aioIob2Bi2x_DestroyWriteFirmContext_orig(context);
        }
        aio_firmware.reset();
    }

    static int32_t __fastcall aioIob2Bi2x_WriteFirmGetState(AIO_IOB2_BI2X_WRFIRM *context) {
        if (!aio_firmware || context != aio_firmware.get()) {
            return aioIob2Bi2x_WriteFirmGetState_orig(context);
        }
        return 8;
    }

    static AIO_NMGR_IOB2 *__fastcall aioNMgrIob2_Create(AIO_SCI_COMM *sci, uint32_t mode) {
        if (!aio_sci || sci != aio_sci.get()) {
            return aioNMgrIob2_Create_orig(sci, mode);
        }
        if (!aio_manager) {
            aio_manager = std::make_unique<AIO_NMGR_IOB2>();
        }
        return aio_manager.get();
    }

    static void __fastcall aioNMgrIob_BeginManage(AIO_NMGR_IOB2 *manager) {
        if (!aio_manager || manager != aio_manager.get()) {
            return aioNMgrIob_BeginManage_orig(manager);
        }
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB2 *manager) {
        if (!aio_manager || manager != aio_manager.get()) {
            return aioNodeMgr_Destroy_orig(manager);
        }
        aio_manager.reset();
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB2 *manager) {
        if (!aio_manager || manager != aio_manager.get()) {
            return aioNodeMgr_GetState_orig(manager);
        }
        return 1;
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB2 *manager, int32_t state) {
        if (!aio_manager || manager != aio_manager.get()) {
            return aioNodeMgr_IsReady_orig(manager, state);
        }
        return state >= 1;
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB2_BI2X_TCS *board) {
        if (!aio_board || board != aio_board.get()) {
            return aioNodeCtl_Destroy_orig(board);
        }
        aio_board.reset();
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB2_BI2X_TCS *board) {
        if (!aio_board || board != aio_board.get()) {
            return aioNodeCtl_GetState_orig(board);
        }
        return 1;
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB2_BI2X_TCS *board, int32_t state) {
        if (!aio_board || board != aio_board.get()) {
            return aioNodeCtl_IsReady_orig(board, state);
        }
        return state >= 1;
    }

    static void __fastcall aioSci_Destroy(AIO_SCI_COMM *sci) {
        if (!aio_sci || sci != aio_sci.get()) {
            return aioSci_Destroy_orig(sci);
        }
        aio_sci.reset();
    }

    void bi2x_hook_init() {
        log_info("cardconnect::bi2x", "initializing native BI2X-TCS hooks");

        const auto video = "libaio-iob2_video.dll";
        detour::trampoline(video, "aioIob2Bi2xTCS_Create",
                           aioIob2Bi2xTCS_Create, &aioIob2Bi2xTCS_Create_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_GetDeviceStatus",
                           aioIob2Bi2xTCS_GetDeviceStatus, &aioIob2Bi2xTCS_GetDeviceStatus_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_IoReset",
                           aioIob2Bi2xTCS_IoReset, &aioIob2Bi2xTCS_IoReset_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_SetWatchDogTimer",
                           aioIob2Bi2xTCS_SetWatchDogTimer, &aioIob2Bi2xTCS_SetWatchDogTimer_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_ControlCoinBlocker",
                           aioIob2Bi2xTCS_ControlCoinBlocker, &aioIob2Bi2xTCS_ControlCoinBlocker_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_AddCounter",
                           aioIob2Bi2xTCS_AddCounter, &aioIob2Bi2xTCS_AddCounter_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_SetPhotoLight",
                           aioIob2Bi2xTCS_SetPhotoLight, &aioIob2Bi2xTCS_SetPhotoLight_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_SetIccrLed",
                           aioIob2Bi2xTCS_SetIccrLed, &aioIob2Bi2xTCS_SetIccrLed_orig);
        detour::trampoline(video, "aioIob2Bi2xTCS_SetTapeLedData",
                           aioIob2Bi2xTCS_SetTapeLedData, &aioIob2Bi2xTCS_SetTapeLedData_orig);

        const auto iob = "libaio-iob.dll";
        detour::trampoline(iob, "aioIob2Bi2x_OpenSciUsbCdc",
                           aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        detour::trampoline(iob, "aioIob2Bi2x_CreateWriteFirmContext",
                           aioIob2Bi2x_CreateWriteFirmContext, &aioIob2Bi2x_CreateWriteFirmContext_orig);
        detour::trampoline(iob, "aioIob2Bi2x_DestroyWriteFirmContext",
                           aioIob2Bi2x_DestroyWriteFirmContext, &aioIob2Bi2x_DestroyWriteFirmContext_orig);
        detour::trampoline(iob, "aioIob2Bi2x_WriteFirmGetState",
                           aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);
        detour::trampoline(iob, "aioNMgrIob2_Create",
                           aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
        detour::trampoline(iob, "aioNMgrIob_BeginManage",
                           aioNMgrIob_BeginManage, &aioNMgrIob_BeginManage_orig);

        const auto aio = "libaio.dll";
        detour::trampoline(aio, "aioNodeMgr_Destroy", aioNodeMgr_Destroy, &aioNodeMgr_Destroy_orig);
        detour::trampoline(aio, "aioNodeMgr_GetState", aioNodeMgr_GetState, &aioNodeMgr_GetState_orig);
        detour::trampoline(aio, "aioNodeMgr_IsReady", aioNodeMgr_IsReady, &aioNodeMgr_IsReady_orig);
        detour::trampoline(aio, "aioNodeCtl_Destroy", aioNodeCtl_Destroy, &aioNodeCtl_Destroy_orig);
        detour::trampoline(aio, "aioNodeCtl_GetState", aioNodeCtl_GetState, &aioNodeCtl_GetState_orig);
        detour::trampoline(aio, "aioNodeCtl_IsReady", aioNodeCtl_IsReady, &aioNodeCtl_IsReady_orig);
        detour::trampoline(aio, "aioSci_Destroy", aioSci_Destroy, &aioSci_Destroy_orig);
    }
}

#endif
