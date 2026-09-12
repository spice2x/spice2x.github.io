#include "bi2x_hook.h"

#if SPICE64

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

#include "acio/icca/icca.h"
#include "games/io.h"
#include "io.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/execexe.h"
#include "util/logging.h"
#include "util/tapeled.h"
#include "util/utils.h"

namespace games::udn {

    struct AIO_SCI_COMM {
        uint8_t data[0xf8];
    };

    struct AIO_NMGR_IOB2 {
        uint8_t data[0xe30];
    };

    struct AIO_IOB2_BI2X_UDN {
        uint8_t data[0x3b00];
    };

    struct AIO_IOB2_BI2X_WRFIRM {
        uint8_t data[0x20f48];
    };

    struct AIO_IOB_ICCA;

#pragma pack(push, 1)
    struct AIO_IOB2_BI2X_UDN__INPUT {
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
        uint8_t bStart;
        uint8_t bUp;
        uint8_t bDown;
        uint8_t bLeft;
        uint8_t bRight;
    };

    struct AIO_IOB2_BI2X_UDN__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        uint8_t TapeLedCounter;
        uint8_t TapeLedRate[8];
        AIO_IOB2_BI2X_UDN__INPUT Input;
        uint8_t InputData[241];
        uint8_t OutputData[48];
        uint8_t IoResetData[4];
        uint8_t PlyrInHist[18];
    };

    // Fixed card-status wire format used by the compatibility interface.
    // The payload is exactly 58 bytes.
    // Field widths remain explicit to keep the boundary stable.
    struct AIO_IOB_ICCA__CARDID {
        uint8_t CardType;
        uint8_t cbCardId;
        uint8_t CardId[10];
    };

    struct AIO_IOB_ICCA__INPUT {
        uint8_t cuStatus;
        uint8_t cuError;
        AIO_IOB_ICCA__CARDID CardId;
        uint8_t KeyNo;
        uint8_t KeyCount;
        uint8_t bKeyDataValid;
        uint8_t Key2No;
        uint8_t Key2Count;
        uint8_t bKey2DataValid;
        uint8_t bKey0;
        uint8_t bKey1;
        uint8_t bKey4;
        uint8_t bKey7;
        uint8_t bKey00;
        uint8_t bKey2;
        uint8_t bKey5;
        uint8_t bKey8;
        uint8_t bKeyBK;
        uint8_t bKey3;
        uint8_t bKey6;
        uint8_t bKey9;
        uint8_t CN1_1;
        uint8_t CN1_2;
        uint8_t CN1_3;
        uint8_t CN1_4;
    };

    struct AIO_IOB_ICCA__DEVSTATUS {
        uint8_t InputCounter;
        uint8_t OutputCounter;
        AIO_IOB_ICCA__INPUT Input;
        uint8_t InputData[16];
        uint8_t OutputData[4];
    };
#pragma pack(pop)

    static_assert(sizeof(AIO_SCI_COMM) == 0xf8);
    static_assert(sizeof(AIO_NMGR_IOB2) == 0xe30);
    static_assert(sizeof(AIO_IOB2_BI2X_UDN) == 0x3b00);
    static_assert(sizeof(AIO_IOB2_BI2X_WRFIRM) == 0x20f48);
    static_assert(sizeof(AIO_IOB2_BI2X_UDN__INPUT) == 15);
    static_assert(sizeof(AIO_IOB2_BI2X_UDN__DEVSTATUS) == 337);
    static_assert(sizeof(AIO_IOB_ICCA__CARDID) == 12);
    static_assert(sizeof(AIO_IOB_ICCA__INPUT) == 36);
    static_assert(sizeof(AIO_IOB_ICCA__DEVSTATUS) == 58);

    using aioIob2Bi2xUDN_Create_t = AIO_IOB2_BI2X_UDN *(__fastcall *)(AIO_NMGR_IOB2 *, uint32_t);
    using aioIob2Bi2xUDN_GetDeviceStatus_t = uint32_t (__fastcall *)(
            AIO_IOB2_BI2X_UDN *, AIO_IOB2_BI2X_UDN__DEVSTATUS *, uint32_t);
    using aioIob2Bi2xAC1_GetDeviceStatus_t = uint32_t (__fastcall *)(void *, void *, uint32_t);
    using aioIob2Bi2xUDN_IoReset_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint32_t);
    using aioIob2Bi2xUDN_SetWatchDogTimer_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint8_t);
    using aioIob2Bi2xUDN_ControlCoinBlocker_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint32_t, bool);
    using aioIob2Bi2xUDN_AddCounter_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint32_t, uint32_t);
    using aioIob2Bi2xUDN_SetStartLamp_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, bool);
    using aioIob2Bi2xUDN_SetPlayerButtonLamp_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint32_t, bool);
    using aioIob2Bi2xUDN_SetIccrLed_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint32_t);
    using aioIob2Bi2xUDN_SetTapeLedDataLimit_t = void (__fastcall *)(
            AIO_IOB2_BI2X_UDN *, uint32_t, uint8_t, uint8_t);
    using aioIob2Bi2xUDN_SetTapeLedData_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *, uint32_t, uint8_t *);
    using aioIob2Bi2x_OpenSciUsbCdc_t = AIO_SCI_COMM *(__fastcall *)(uint32_t);
    using aioIob2Bi2x_CreateWriteFirmContext_t = AIO_IOB2_BI2X_WRFIRM *(__fastcall *)(uint32_t, uint32_t);
    using aioIob2Bi2x_DestroyWriteFirmContext_t = void (__fastcall *)(AIO_IOB2_BI2X_WRFIRM *);
    using aioIob2Bi2x_WriteFirmGetState_t = int32_t (__fastcall *)(AIO_IOB2_BI2X_WRFIRM *);

    using aioIobIcca_Create_t = AIO_IOB_ICCA *(__fastcall *)(void *, uint32_t, uint8_t);
    using aioIobIcca_GetDeviceStatus_t = uint32_t (__fastcall *)(
            AIO_IOB_ICCA *, AIO_IOB_ICCA__DEVSTATUS *, uint32_t);
    using aioIobIcca_BeginGetCardId_t = void (__fastcall *)(AIO_IOB_ICCA *);
    using aioIobIcca_EndGetCardId_t = void (__fastcall *)(AIO_IOB_ICCA *);

    using aioNMgrIob2_Create_t = AIO_NMGR_IOB2 *(__fastcall *)(AIO_SCI_COMM *, uint32_t);
    using aioNMgrIob_BeginManage_t = void (__fastcall *)(AIO_NMGR_IOB2 *);

    using aioNodeMgr_Destroy_t = void (__fastcall *)(AIO_NMGR_IOB2 *);
    using aioNodeMgr_GetState_t = int32_t (__fastcall *)(AIO_NMGR_IOB2 *);
    using aioNodeMgr_IsReady_t = bool (__fastcall *)(AIO_NMGR_IOB2 *, int32_t);
    using aioNodeCtl_Destroy_t = void (__fastcall *)(AIO_IOB2_BI2X_UDN *);
    using aioNodeCtl_GetState_t = int32_t (__fastcall *)(AIO_IOB2_BI2X_UDN *);
    using aioNodeCtl_IsReady_t = bool (__fastcall *)(AIO_IOB2_BI2X_UDN *, int32_t);

    static aioIob2Bi2xUDN_Create_t aioIob2Bi2xUDN_Create_orig = nullptr;
    static aioIob2Bi2xUDN_GetDeviceStatus_t aioIob2Bi2xUDN_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xAC1_GetDeviceStatus_t aioIob2Bi2xAC1_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xUDN_IoReset_t aioIob2Bi2xUDN_IoReset_orig = nullptr;
    static aioIob2Bi2xUDN_SetWatchDogTimer_t aioIob2Bi2xUDN_SetWatchDogTimer_orig = nullptr;
    static aioIob2Bi2xUDN_ControlCoinBlocker_t aioIob2Bi2xUDN_ControlCoinBlocker_orig = nullptr;
    static aioIob2Bi2xUDN_AddCounter_t aioIob2Bi2xUDN_AddCounter_orig = nullptr;
    static aioIob2Bi2xUDN_SetStartLamp_t aioIob2Bi2xUDN_SetStartLamp_orig = nullptr;
    static aioIob2Bi2xUDN_SetPlayerButtonLamp_t aioIob2Bi2xUDN_SetPlayerButtonLamp_orig = nullptr;
    static aioIob2Bi2xUDN_SetIccrLed_t aioIob2Bi2xUDN_SetIccrLed_orig = nullptr;
    static aioIob2Bi2xUDN_SetTapeLedDataLimit_t aioIob2Bi2xUDN_SetTapeLedDataLimit_orig = nullptr;
    static aioIob2Bi2xUDN_SetTapeLedData_t aioIob2Bi2xUDN_SetTapeLedData_orig = nullptr;
    static aioIob2Bi2x_OpenSciUsbCdc_t aioIob2Bi2x_OpenSciUsbCdc_orig = nullptr;
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_DestroyWriteFirmContext_t aioIob2Bi2x_DestroyWriteFirmContext_orig = nullptr;
    static aioIob2Bi2x_WriteFirmGetState_t aioIob2Bi2x_WriteFirmGetState_orig = nullptr;

    static aioIobIcca_Create_t aioIobIcca_Create_orig = nullptr;
    static aioIobIcca_GetDeviceStatus_t aioIobIcca_GetDeviceStatus_orig = nullptr;
    static aioIobIcca_BeginGetCardId_t aioIobIcca_BeginGetCardId_orig = nullptr;
    static aioIobIcca_EndGetCardId_t aioIobIcca_EndGetCardId_orig = nullptr;

    static aioNMgrIob2_Create_t aioNMgrIob2_Create_orig = nullptr;
    static aioNMgrIob_BeginManage_t aioNMgrIob_BeginManage_orig = nullptr;

    static aioNodeMgr_Destroy_t aioNodeMgr_Destroy_orig = nullptr;
    static aioNodeMgr_GetState_t aioNodeMgr_GetState_orig = nullptr;
    static aioNodeMgr_IsReady_t aioNodeMgr_IsReady_orig = nullptr;
    static aioNodeCtl_Destroy_t aioNodeCtl_Destroy_orig = nullptr;
    static aioNodeCtl_GetState_t aioNodeCtl_GetState_orig = nullptr;
    static aioNodeCtl_IsReady_t aioNodeCtl_IsReady_orig = nullptr;

    static std::unique_ptr<AIO_SCI_COMM> aio_sci_comm;
    static std::unique_ptr<AIO_NMGR_IOB2> aio_node_manager;
    static AIO_NMGR_IOB2 *aio_icca_node_manager = nullptr;
    static std::unique_ptr<AIO_IOB2_BI2X_UDN> aio_udn;
    static std::unique_ptr<AIO_IOB2_BI2X_WRFIRM> aio_write_firm;
    static uint8_t input_counter = 0;

    // Native ICCA status/begin/end operations also lock the reader state.
    // The managed wrapper can destroy its node from a finalizer thread.
    static std::mutex icca_mutex;
    static AIO_IOB_ICCA *aio_icca = nullptr;
    static bool icca_reading = false;
    static bool icca_card_ready = false;
    static uint8_t icca_card_uid[8] {};
    static uint8_t icca_input_counter = 0;

    static void write_light(Lights::udn_lights_t light, float value) {
        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(light), value);
    }

    static AIO_IOB2_BI2X_UDN *__fastcall aioIob2Bi2xUDN_Create(
            AIO_NMGR_IOB2 *node_manager, uint32_t device_id) {

        if (node_manager != aio_node_manager.get()) {
            return aioIob2Bi2xUDN_Create_orig(node_manager, device_id);
        }

        if (!aio_udn) {
            aio_udn = std::make_unique<AIO_IOB2_BI2X_UDN>();
            log_info("udn_bi2x", "UDN node created");
        }
        return aio_udn.get();
    }

    static uint32_t __fastcall aioIob2Bi2xAC1_GetDeviceStatus(
            void *node, void *status, uint32_t status_size) {

        // aioIob2Bi2xUDN.GetDeviceStatus() first invokes its managed AC1 base method.
        if (node == aio_udn.get()) {
            if (status && status_size > 0) {
                std::memset(status, 0, status_size);
            }
            return status_size;
        }
        return aioIob2Bi2xAC1_GetDeviceStatus_orig(node, status, status_size);
    }

    static uint32_t __fastcall aioIob2Bi2xUDN_GetDeviceStatus(
            AIO_IOB2_BI2X_UDN *node, AIO_IOB2_BI2X_UDN__DEVSTATUS *status,
            uint32_t status_size) {

        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_GetDeviceStatus_orig(node, status, status_size);
        }

        RI_MGR->devices_flush_output();

        AIO_IOB2_BI2X_UDN__DEVSTATUS current {};
        current.InputCounter = input_counter;
        current.Input.DevIoCounter = input_counter++;

        auto &buttons = get_buttons();
        current.Input.bService = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service]) ? 1 : 0;
        current.Input.bTest = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test]) ? 1 : 0;
        current.Input.bCoinSw = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech]) ? 1 : 0;
        current.Input.bStart = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Start]) ? 1 : 0;
        current.Input.bUp = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Up]) ? 1 : 0;
        current.Input.bDown = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Down]) ? 1 : 0;
        current.Input.bLeft = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Left]) ? 1 : 0;
        current.Input.bRight = GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Right]) ? 1 : 0;
        // The hardware counter wraps at 256; clamping it would stop accepting
        // coins once the shared stock reaches 255.
        current.Input.CoinCount = static_cast<uint8_t>(eamuse_coin_get_stock());

        const auto copy_size = std::min<uint32_t>(status_size, sizeof(current));
        if (status && copy_size > 0) {
            std::memcpy(status, &current, copy_size);
        }
        return copy_size;
    }

    static void __fastcall aioIob2Bi2xUDN_IoReset(AIO_IOB2_BI2X_UDN *node, uint32_t reset_bits) {
        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_IoReset_orig(node, reset_bits);
        }
    }

    static void __fastcall aioIob2Bi2xUDN_SetWatchDogTimer(AIO_IOB2_BI2X_UDN *node, uint8_t count) {
        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_SetWatchDogTimer_orig(node, count);
        }
    }

    static void __fastcall aioIob2Bi2xUDN_ControlCoinBlocker(
            AIO_IOB2_BI2X_UDN *node, uint32_t slot, bool open) {

        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_ControlCoinBlocker_orig(node, slot, open);
        }
        eamuse_coin_set_block(!open);
    }

    static void __fastcall aioIob2Bi2xUDN_AddCounter(
            AIO_IOB2_BI2X_UDN *node, uint32_t counter, uint32_t count) {

        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_AddCounter_orig(node, counter, count);
        }
    }

    static void __fastcall aioIob2Bi2xUDN_SetStartLamp(AIO_IOB2_BI2X_UDN *node, bool on) {
        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_SetStartLamp_orig(node, on);
        }
        write_light(Lights::Start, on ? 1.f : 0.f);
    }

    static void __fastcall aioIob2Bi2xUDN_SetPlayerButtonLamp(
            AIO_IOB2_BI2X_UDN *node, uint32_t button, bool on) {

        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_SetPlayerButtonLamp_orig(node, button, on);
        }

        static const Lights::udn_lights_t mapping[] {
            Lights::Start,
            Lights::Up,
            Lights::Down,
            Lights::Left,
            Lights::Right,
        };
        if (button < std::size(mapping)) {
            write_light(mapping[button], on ? 1.f : 0.f);
        }
    }

    static void __fastcall aioIob2Bi2xUDN_SetIccrLed(AIO_IOB2_BI2X_UDN *node, uint32_t rgb) {
        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_SetIccrLed_orig(node, rgb);
        }

        write_light(Lights::CardReaderR, ((rgb >> 16) & 0xff) / 255.f);
        write_light(Lights::CardReaderG, ((rgb >> 8) & 0xff) / 255.f);
        write_light(Lights::CardReaderB, (rgb & 0xff) / 255.f);
    }

    static void __fastcall aioIob2Bi2xUDN_SetTapeLedDataLimit(
            AIO_IOB2_BI2X_UDN *node, uint32_t channel, uint8_t scale, uint8_t limit) {

        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_SetTapeLedDataLimit_orig(node, channel, scale, limit);
        }
    }

    static void __fastcall aioIob2Bi2xUDN_SetTapeLedData(
            AIO_IOB2_BI2X_UDN *node, uint32_t tape, uint8_t *data) {

        if (node != aio_udn.get()) {
            return aioIob2Bi2xUDN_SetTapeLedData_orig(node, tape, data);
        }

        struct TapeMapping {
            size_t color_count;
            Lights::udn_lights_t r;
            Lights::udn_lights_t g;
            Lights::udn_lights_t b;
        };
        static const TapeMapping mapping[] {
            {62, Lights::TitleR, Lights::TitleG, Lights::TitleB},
            {152, Lights::PillarRightTopR, Lights::PillarRightTopG, Lights::PillarRightTopB},
            {165, Lights::PillarRightBottomR, Lights::PillarRightBottomG, Lights::PillarRightBottomB},
            {9, Lights::StageLeftR, Lights::StageLeftG, Lights::StageLeftB},
            {17, Lights::CabinetLeftR, Lights::CabinetLeftG, Lights::CabinetLeftB},
            {152, Lights::PillarLeftTopR, Lights::PillarLeftTopG, Lights::PillarLeftTopB},
            {165, Lights::PillarLeftBottomR, Lights::PillarLeftBottomG, Lights::PillarLeftBottomB},
            {9, Lights::StageRightR, Lights::StageRightG, Lights::StageRightB},
            {17, Lights::CabinetRightR, Lights::CabinetRightG, Lights::CabinetRightB},
        };

        if (data && tapeledutils::is_enabled() && tape < std::size(mapping)) {
            const auto &entry = mapping[tape];
            const auto color = tapeledutils::pick_color_from_led_tape(data, entry.color_count);
            write_light(entry.r, color.r);
            write_light(entry.g, color.g);
            write_light(entry.b, color.b);
        }
    }

    static AIO_SCI_COMM *__fastcall aioIob2Bi2x_OpenSciUsbCdc(uint32_t serial_number) {
        if (!aio_sci_comm) {
            aio_sci_comm = std::make_unique<AIO_SCI_COMM>();
        }
        return aio_sci_comm.get();
    }

    static AIO_IOB2_BI2X_WRFIRM *__fastcall aioIob2Bi2x_CreateWriteFirmContext(
            uint32_t serial_number, uint32_t iob_mask) {

        if (!aio_write_firm) {
            aio_write_firm = std::make_unique<AIO_IOB2_BI2X_WRFIRM>();
        }
        return aio_write_firm.get();
    }

    static void __fastcall aioIob2Bi2x_DestroyWriteFirmContext(AIO_IOB2_BI2X_WRFIRM *context) {
        if (context != aio_write_firm.get()) {
            return aioIob2Bi2x_DestroyWriteFirmContext_orig(context);
        }
        aio_write_firm.reset();
    }

    static int32_t __fastcall aioIob2Bi2x_WriteFirmGetState(AIO_IOB2_BI2X_WRFIRM *context) {
        if (context == aio_write_firm.get()) {
            // The native state machine treats both 7 (NODEV) and 8
            // (COMPLETED) as a successful terminal state.  UDN additionally
            // interprets state 8 as "firmware was written this boot" and
            // deliberately powers the cabinet down after loading so the real
            // I/O board can reboot.  There is no physical BI2X board behind
            // this emulation, so report NODEV: the check is complete without
            // scheduling that otherwise endless firmware-update reboot.
            return 7;
        }
        return aioIob2Bi2x_WriteFirmGetState_orig(context);
    }

    static AIO_IOB_ICCA *__fastcall aioIobIcca_Create(
            void *node_manager, uint32_t device_id, uint8_t init_mode) {

        auto *node = aioIobIcca_Create_orig(node_manager, device_id, init_mode);
        if (node) {
            {
                std::lock_guard<std::mutex> lock(icca_mutex);
                aio_icca = node;
                aio_icca_node_manager =
                    reinterpret_cast<AIO_NMGR_IOB2 *>(node_manager);
                icca_reading = false;
                icca_card_ready = false;
                std::memset(icca_card_uid, 0, sizeof(icca_card_uid));
            }
            log_info("udn_bi2x", "ICCA node attached to Spice CardIO bridge");
        }
        return node;
    }

    static uint32_t __fastcall aioIobIcca_GetDeviceStatus(
            AIO_IOB_ICCA *node, AIO_IOB_ICCA__DEVSTATUS *status, uint32_t status_size) {

        std::unique_lock<std::mutex> lock(icca_mutex);
        if (node != aio_icca) {
            lock.unlock();
            return aioIobIcca_GetDeviceStatus_orig(node, status, status_size);
        }

        AIO_IOB_ICCA__DEVSTATUS current {};
        current.InputCounter = icca_input_counter++;
        current.OutputCounter = current.InputCounter;

        // DNI while idle, BUSY while waiting for a card, READY once a UID is
        // available. The card-reader state machine accepts a card only on READY.
        current.Input.cuStatus = icca_reading ? 1 : 0;
        if (icca_reading && !icca_card_ready && eamuse_card_insert_consume(1, 0)) {
            uint8_t uid[8] {};
            if (eamuse_get_card(1, 0, uid)) {
                std::memcpy(icca_card_uid, uid, sizeof(icca_card_uid));
                icca_card_ready = true;
                log_info(
                    "udn_bi2x",
                    "ICCA accepted UID {} from the Spice card queue; reporting READY to Unity",
                    bin2hex(icca_card_uid, sizeof(icca_card_uid)));
            } else {
                log_warning(
                    "udn_bi2x",
                    "ICCA consumed a card-insert event but Spice returned no UID");
            }
        }

        if (icca_card_ready) {
            current.Input.cuStatus = 2;
            current.Input.CardId.CardType = is_card_uid_felica(icca_card_uid) ? 2 : 1;
            current.Input.CardId.cbCardId = sizeof(icca_card_uid);
            std::memcpy(current.Input.CardId.CardId, icca_card_uid, sizeof(icca_card_uid));
        }

        // The ICCA keypad scan-code order is identical to eamuse.h.  Keeping
        // these bits populated also preserves the usual Spice numpad PIN flow.
        const auto keypad = eamuse_get_keypad_state(0);
        auto *key_states = &current.Input.bKey0;
        for (size_t key = 0; key < EAM_IO_KEYPAD_COUNT; ++key) {
            key_states[key] = (keypad & (1u << key)) ? 1 : 0;
        }

        const auto copy_size = std::min<uint32_t>(status_size, sizeof(current));
        if (status && copy_size > 0) {
            std::memcpy(status, &current, copy_size);
        }
        return copy_size;
    }

    static void __fastcall aioIobIcca_BeginGetCardId(AIO_IOB_ICCA *node) {
        std::unique_lock<std::mutex> lock(icca_mutex);
        if (node != aio_icca) {
            lock.unlock();
            return aioIobIcca_BeginGetCardId_orig(node);
        }
        if (!icca_reading) {
            log_info("udn_bi2x", "ICCA BeginGetCardId: waiting for the Spice/CardIO queue");
        }
        icca_reading = true;
        icca_card_ready = false;
        std::memset(icca_card_uid, 0, sizeof(icca_card_uid));
    }

    static void __fastcall aioIobIcca_EndGetCardId(AIO_IOB_ICCA *node) {
        std::unique_lock<std::mutex> lock(icca_mutex);
        if (node != aio_icca) {
            lock.unlock();
            return aioIobIcca_EndGetCardId_orig(node);
        }
        if (icca_reading || icca_card_ready) {
            log_info("udn_bi2x", "ICCA EndGetCardId: returning to idle");
        }
        icca_reading = false;
        icca_card_ready = false;
        std::memset(icca_card_uid, 0, sizeof(icca_card_uid));
    }

    static AIO_NMGR_IOB2 *__fastcall aioNMgrIob2_Create(AIO_SCI_COMM *sci, uint32_t mode) {
        if (sci != aio_sci_comm.get()) {
            return aioNMgrIob2_Create_orig(sci, mode);
        }
        if (!aio_node_manager) {
            aio_node_manager = std::make_unique<AIO_NMGR_IOB2>();
        }
        return aio_node_manager.get();
    }

    static void __fastcall aioNMgrIob_BeginManage(AIO_NMGR_IOB2 *node_manager) {
        if (node_manager != aio_node_manager.get()) {
            return aioNMgrIob_BeginManage_orig(node_manager);
        }
    }

    static void __fastcall aioNodeMgr_Destroy(AIO_NMGR_IOB2 *node_manager) {
        if (node_manager != aio_node_manager.get()) {
            {
                std::lock_guard<std::mutex> lock(icca_mutex);
                if (node_manager == aio_icca_node_manager) {
                    aio_icca_node_manager = nullptr;
                }
            }
            return aioNodeMgr_Destroy_orig(node_manager);
        }
        aio_node_manager.reset();
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB2 *node_manager) {
        bool virtual_manager = node_manager == aio_node_manager.get();
        {
            std::lock_guard<std::mutex> lock(icca_mutex);
            virtual_manager |= node_manager == aio_icca_node_manager;
        }
        if (virtual_manager) {
            return 0;
        }
        return aioNodeMgr_GetState_orig(node_manager);
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB2 *node_manager, int32_t state) {
        bool virtual_manager = node_manager == aio_node_manager.get();
        {
            std::lock_guard<std::mutex> lock(icca_mutex);
            virtual_manager |= node_manager == aio_icca_node_manager;
        }
        if (virtual_manager) {
            return true;
        }
        return aioNodeMgr_IsReady_orig(node_manager, state);
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB2_BI2X_UDN *node) {
        if (node != aio_udn.get()) {
            {
                std::lock_guard<std::mutex> lock(icca_mutex);
                if (reinterpret_cast<AIO_IOB_ICCA *>(node) == aio_icca) {
                    aio_icca = nullptr;
                    aio_icca_node_manager = nullptr;
                    icca_reading = false;
                    icca_card_ready = false;
                    std::memset(icca_card_uid, 0, sizeof(icca_card_uid));
                }
            }
            return aioNodeCtl_Destroy_orig(node);
        }
        aio_udn.reset();
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB2_BI2X_UDN *node) {
        bool virtual_node = node == aio_udn.get();
        {
            std::lock_guard<std::mutex> lock(icca_mutex);
            virtual_node |= reinterpret_cast<AIO_IOB_ICCA *>(node) == aio_icca;
        }
        if (virtual_node) {
            return 0;
        }
        return aioNodeCtl_GetState_orig(node);
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB2_BI2X_UDN *node, int32_t state) {
        bool virtual_node = node == aio_udn.get();
        {
            std::lock_guard<std::mutex> lock(icca_mutex);
            virtual_node |= reinterpret_cast<AIO_IOB_ICCA *>(node) == aio_icca;
        }
        if (virtual_node) {
            return true;
        }
        return aioNodeCtl_IsReady_orig(node, state);
    }

    void bi2x_hook_init() {
        log_info("udn_bi2x", "init (BI2X + ICCA/CardIO bridge)");

        // The runtime imports the alias DLL names, not their lib-prefixed originals.
        const auto aio_iob2_video = "aio-iob2_video.dll";
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_Create",
                                aioIob2Bi2xUDN_Create, &aioIob2Bi2xUDN_Create_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_GetDeviceStatus",
                                aioIob2Bi2xUDN_GetDeviceStatus, &aioIob2Bi2xUDN_GetDeviceStatus_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xAC1_GetDeviceStatus",
                                aioIob2Bi2xAC1_GetDeviceStatus, &aioIob2Bi2xAC1_GetDeviceStatus_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_IoReset",
                                aioIob2Bi2xUDN_IoReset, &aioIob2Bi2xUDN_IoReset_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_SetWatchDogTimer",
                                aioIob2Bi2xUDN_SetWatchDogTimer, &aioIob2Bi2xUDN_SetWatchDogTimer_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_ControlCoinBlocker",
                                aioIob2Bi2xUDN_ControlCoinBlocker, &aioIob2Bi2xUDN_ControlCoinBlocker_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_AddCounter",
                                aioIob2Bi2xUDN_AddCounter, &aioIob2Bi2xUDN_AddCounter_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_SetStartLamp",
                                aioIob2Bi2xUDN_SetStartLamp, &aioIob2Bi2xUDN_SetStartLamp_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_SetPlayerButtonLamp",
                                aioIob2Bi2xUDN_SetPlayerButtonLamp, &aioIob2Bi2xUDN_SetPlayerButtonLamp_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_SetIccrLed",
                                aioIob2Bi2xUDN_SetIccrLed, &aioIob2Bi2xUDN_SetIccrLed_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_SetTapeLedDataLimit",
                                aioIob2Bi2xUDN_SetTapeLedDataLimit, &aioIob2Bi2xUDN_SetTapeLedDataLimit_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2xUDN_SetTapeLedData",
                                aioIob2Bi2xUDN_SetTapeLedData, &aioIob2Bi2xUDN_SetTapeLedData_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2x_OpenSciUsbCdc",
                                aioIob2Bi2x_OpenSciUsbCdc, &aioIob2Bi2x_OpenSciUsbCdc_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2x_CreateWriteFirmContext",
                                aioIob2Bi2x_CreateWriteFirmContext, &aioIob2Bi2x_CreateWriteFirmContext_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2x_DestroyWriteFirmContext",
                                aioIob2Bi2x_DestroyWriteFirmContext, &aioIob2Bi2x_DestroyWriteFirmContext_orig);
        execexe::trampoline_try(aio_iob2_video, "aioIob2Bi2x_WriteFirmGetState",
                                aioIob2Bi2x_WriteFirmGetState, &aioIob2Bi2x_WriteFirmGetState_orig);

        const auto aio_iob_video = "aio-iob_video.dll";
        execexe::trampoline_try(aio_iob_video, "aioIobIcca_Create",
                                aioIobIcca_Create, &aioIobIcca_Create_orig);
        execexe::trampoline_try(aio_iob_video, "aioIobIcca_GetDeviceStatus",
                                aioIobIcca_GetDeviceStatus, &aioIobIcca_GetDeviceStatus_orig);
        execexe::trampoline_try(aio_iob_video, "aioIobIcca_BeginGetCardId",
                                aioIobIcca_BeginGetCardId, &aioIobIcca_BeginGetCardId_orig);
        execexe::trampoline_try(aio_iob_video, "aioIobIcca_EndGetCardId",
                                aioIobIcca_EndGetCardId, &aioIobIcca_EndGetCardId_orig);

        const auto aio_iob = "aio-iob.dll";
        execexe::trampoline_try(aio_iob, "aioNMgrIob2_Create",
                                aioNMgrIob2_Create, &aioNMgrIob2_Create_orig);
        execexe::trampoline_try(aio_iob, "aioNMgrIob_BeginManage",
                                aioNMgrIob_BeginManage, &aioNMgrIob_BeginManage_orig);

        const auto aio = "aio.dll";
        execexe::trampoline_try(aio, "aioNodeMgr_Destroy",
                                aioNodeMgr_Destroy, &aioNodeMgr_Destroy_orig);
        execexe::trampoline_try(aio, "aioNodeMgr_GetState",
                                aioNodeMgr_GetState, &aioNodeMgr_GetState_orig);
        execexe::trampoline_try(aio, "aioNodeMgr_IsReady",
                                aioNodeMgr_IsReady, &aioNodeMgr_IsReady_orig);
        execexe::trampoline_try(aio, "aioNodeCtl_Destroy",
                                aioNodeCtl_Destroy, &aioNodeCtl_Destroy_orig);
        execexe::trampoline_try(aio, "aioNodeCtl_GetState",
                                aioNodeCtl_GetState, &aioNodeCtl_GetState_orig);
        execexe::trampoline_try(aio, "aioNodeCtl_IsReady",
                                aioNodeCtl_IsReady, &aioNodeCtl_IsReady_orig);
    }
}

#endif
