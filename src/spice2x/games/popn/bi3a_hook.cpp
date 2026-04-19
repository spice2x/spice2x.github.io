#include "bi3a_hook.h"

#if SPICE64 && !SPICE_XP

#include <optional>
#include <cstdint>
#include "util/detour.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/io.h"
#include "popn.h"
#include "io.h"
#include "util/tapeled.h"
#include <typeinfo>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace games::popn {

    /*
     * class definitions
     */

    struct AIO_NMGR_IOB2 {
        uint8_t data[0xe18];
    };

    struct AIO_NMGR_IOB5 {
        uint8_t data[0xb10];
    };

    struct AIO_SCI_COMM {
        uint8_t data[0x108];
    };

    struct AIO_SCI_COMM_T {
        uint64_t unk_0;
        struct AIO_SCI_COMM *aioSciComm;
    };

    struct AIO_COMM_STATUS {
        uint64_t unk[5];
    };

    struct AIO_IOB5_BI3A {
        uint8_t data[0x4FF0];
    };

    union AIO_IOB5_BI3A__DEVSTATUS {
        uint8_t RawBytes[648];
        struct {
            uint8_t Reserved0[27];  // 0-26
            uint8_t Coin0;          // 27
            uint8_t Coin1;          // 28
            uint8_t Reserved1[15];  // 29-43
            uint8_t TestButton;     // 44, io code offset rcx+0xe4
            uint8_t ServiceButton;  // 45, io code offset rcx+0xe5
            uint8_t CoinMech;       // 46, io code offset rcx+0xe6
            uint8_t Reserved2[19];  // 47-65
            uint8_t JOYL_SW;        // 66
            uint8_t Reserved3[4];   // 67-70
            uint8_t JOYR_SW;        // 71
            uint8_t Headphones;     // 72
            uint8_t Recorder;       // 73, unused
            uint8_t Reserved4[295]; // 74-368
            uint8_t Buttons_0_8;    // 369
            uint8_t Button_9;       // 370
        } Input;
    };

    // verified with M39-004-2025121500
    static_assert(sizeof(AIO_NMGR_IOB2) == 0xe18);
    static_assert(sizeof(AIO_NMGR_IOB5) == 0xb10);
    static_assert(sizeof(AIO_SCI_COMM) == 0x108);
    static_assert(sizeof(AIO_IOB5_BI3A) == 0x4FF0);
    static_assert(sizeof(AIO_IOB5_BI3A__DEVSTATUS) == 0x288);

    /*
     * typedefs
     */

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
    typedef void(__fastcall *aioIob5Bi3a_SetOutputData_t)(AIO_IOB5_BI3A *a1, unsigned int a2, unsigned __int8 a3);
    typedef void(__fastcall *aioIob5Bi3a_SetTapeLedDataPart_t)(AIO_IOB5_BI3A *a1, unsigned int a2, char a3, const void *a4, unsigned int a5, bool a6);
    typedef int64_t (__fastcall *aioIob5Bi3a_SetTapeLedDataLimit_t)(AIO_IOB5_BI3A *i_pNodeCtl, uint32_t i_Channel, uint8_t i_Scale, uint8_t i_Limit);

    /*
     * function pointers
     */

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
    static aioIob5Bi3a_SetOutputData_t aioIob5Bi3a_SetOutputData_orig = nullptr;
    static aioIob5Bi3a_SetTapeLedDataPart_t aioIob5Bi3a_SetTapeLedDataPart_orig = nullptr;
    static aioIob5Bi3a_SetTapeLedDataLimit_t aioIob5Bi3a_SetTapeLedDataLimit_orig = nullptr;

    /*
     * variables
     */
    static AIO_NMGR_IOB2 *aioNmgrIob2;
    static AIO_NMGR_IOB5 *aioNmgrIob5;
    static AIO_SCI_COMM *aioSciComm;
    static AIO_IOB5_BI3A *aioIob5Bi3a;

    /*
     * implementations
     */

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
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            delete aioNmgrIob5;
            aioNmgrIob5 = nullptr;
        } else {
            return aioNodeMgr_Destroy_orig(i_pNodeMgr);
        }
    }

    static int32_t __fastcall aioNodeMgr_GetState(AIO_NMGR_IOB5 *i_pNodeMgr) {
        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {
            return 1;
        } else {
            return aioNodeMgr_GetState_orig(i_pNodeMgr);
        }
    }

    static bool __fastcall aioNodeMgr_IsReady(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            return true;
        } else {
            return aioNodeMgr_IsReady_orig(i_pNodeMgr, i_State);
        }
    }

    static bool __fastcall aioNodeMgr_IsError(AIO_NMGR_IOB5 *i_pNodeMgr, int32_t i_State) {
        if (i_pNodeMgr == aioNmgrIob5 ||
            i_pNodeMgr == (AIO_NMGR_IOB5 *)aioNmgrIob2) {

            return false;
        } else {
            return aioNodeMgr_IsError_orig(i_pNodeMgr, i_State);
        }
    }

    static void __fastcall aioNodeCtl_Destroy(AIO_IOB5_BI3A *i_pNodeCtl) {
        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioNodeCtl_Destroy_orig(i_pNodeCtl);
        }
        delete aioIob5Bi3a;
        aioIob5Bi3a = nullptr;
    }

    static int32_t __fastcall aioNodeCtl_GetState(AIO_IOB5_BI3A *i_pNodeCtl) {
        if (i_pNodeCtl == aioIob5Bi3a) {
            return 1;
        }
        return aioNodeCtl_GetState_orig(i_pNodeCtl);
    }

    static bool __fastcall aioNodeCtl_IsReady(AIO_IOB5_BI3A *i_pNodeCtl, int32_t i_State) {
        if (i_pNodeCtl == aioIob5Bi3a) {
            return true;
        }
        return aioNodeCtl_IsReady_orig(i_pNodeCtl, i_State);
    }

    static bool __fastcall aioNodeCtl_IsError(AIO_IOB5_BI3A *i_pNodeCtl, int32_t i_State) {
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
            log_info("bi3a_hook", "aioNMgrIob5_Create called, returning custom AIO_NMGR_IOB5: {}", fmt::ptr(aioNmgrIob5));
            return aioNmgrIob5;
        } else {
            return aioNMgrIob5_Create_orig(i_pSci, i_bfMode);
        }
    }

    static AIO_IOB5_BI3A *__fastcall aioIob5Bi3a_Create(AIO_NMGR_IOB5 *i_pNodeMgr, uint8_t i, void *p) {

        log_info("bi3a_hook", "aioIob5Bi3a_Create called with i_pNodeMgr={}, i={}, p={}", fmt::ptr(i_pNodeMgr), i, fmt::ptr(p));

        if (i_pNodeMgr == aioNmgrIob5) {
            aioIob5Bi3a = new AIO_IOB5_BI3A;
            log_info("bi3a_hook", "aioIob5Bi3a_Create: returning custom AIO_IOB5_BI3A: {}", fmt::ptr(aioIob5Bi3a));
            return aioIob5Bi3a;
        } else {
            return aioIob5Bi3a_Create_orig(i_pNodeMgr, i, p);
        }
    }

    static void __fastcall aioIob5Bi3a_GetDeviceStatus(AIO_IOB5_BI3A *i_pNodeCtl, AIO_IOB5_BI3A__DEVSTATUS *o_DevStatus) {
        RI_MGR->devices_flush_output();
        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioIob5Bi3a_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus);
        }

        memset(o_DevStatus, 0, sizeof(*o_DevStatus));
        auto &buttons = get_buttons();

        // operator
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test])) {
            o_DevStatus->Input.TestButton = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service])) {
            o_DevStatus->Input.ServiceButton = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::CoinMech])) {
            o_DevStatus->Input.CoinMech = 1;
        }

        // buttons 1-9
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button1])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 0);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button2])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 1);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button3])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 2);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button4])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 3);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button5])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 4);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button6])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 5);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button7])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 6);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button8])) {
            o_DevStatus->Input.Buttons_0_8 |= (1 << 7);
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button9])) {
            o_DevStatus->Input.Button_9 = 1;
        }

        // deka pop-kuns
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::RedPop])) {
            o_DevStatus->Input.JOYL_SW = 1;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::BluePop])) {
            o_DevStatus->Input.JOYR_SW = 1;
        }

        // audio
        if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Headphone])) {
            o_DevStatus->Input.Headphones = 1;
        }

        // coin
        o_DevStatus->Input.Coin0 += eamuse_coin_get_stock();
    }

    static void __fastcall aioIob5Bi3a_SetOutputData(AIO_IOB5_BI3A *i_pNodeCtl, uint32_t i_CnPin, uint8_t i_Data) {
        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioIob5Bi3a_SetOutputData_orig(i_pNodeCtl, i_CnPin, i_Data);
        }

        //  7 = IC_CARD_R
        //  8 = IC_CARD_G
        //  9 = IC_CARD_B
        // 10 = WOOFER_R
        // 11 = WOOFER_G
        // 12 = WOOFER_B
        // values = 0 to 255 in test menu 

        std::optional<Lights::popn_lights_t> light;
        switch (i_CnPin) {
            case 7:
                light = Lights::IC_Card_R;
                break;
            case 8:
                light = Lights::IC_Card_G;
                break;
            case 9:
                light = Lights::IC_Card_B;
                break;
            case 10:
                light = Lights::WooferLED_R;
                break;
            case 11:
                light = Lights::WooferLED_G;
                break;
            case 12:
                light = Lights::WooferLED_B;
                break;
            default:
                break;
        }
        if (light.has_value()) {
            auto &lights = get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights.at(light.value()), i_Data / 255.f);
        }

        return;
    }

    static int64_t __fastcall aioIob5Bi3a_SetTapeLedDataLimit(
        AIO_IOB5_BI3A *i_pNodeCtl, uint32_t i_Channel, uint8_t i_Scale, uint8_t i_Limit) {

        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioIob5Bi3a_SetTapeLedDataLimit_orig(i_pNodeCtl, i_Channel, i_Scale, i_Limit);
        }
        return 0;
    }

    struct PopnLight {
        int data_index;
        Lights::popn_lights_t light;
        uint8_t size;
        PopnLight(
            int data_index, Lights::popn_lights_t light, uint8_t size) :
                data_index(data_index), light(light), size(size) {}
    };

    static void __fastcall aioIob5Bi3a_SetTapeLedDataPart(
        AIO_IOB5_BI3A *i_pNodeCtl, uint32_t i_CnPin, char i_LedType, const void *i_pData, uint32_t i_DataSize, bool i_bIsLast) {

        if (i_pNodeCtl != aioIob5Bi3a) {
            return aioIob5Bi3a_SetTapeLedDataPart_orig(i_pNodeCtl, i_CnPin, i_LedType, i_pData, i_DataSize, i_bIsLast);
        }

        auto number_of_leds = i_DataSize;
        // buttons 1-9, but game passes in wrong length; fix it up
        if (i_CnPin == 0 && i_DataSize == 9) {
            number_of_leds = i_DataSize * 3;
        }

        if (FALSE) {
            std::string data;
            for (uint32_t i = 0; i < number_of_leds; i++) {
                auto dataByte = ((uint8_t *)i_pData)[i];
                data += fmt::format("{:02X} ", dataByte);
            }
            
            log_info(
                "bi3a_hook",
                "aioIob5Bi3a_SetTapeLedDataPart Pin={}, Size={}, Type={}, data@{} = {}",
                i_CnPin, number_of_leds, (uint8_t)i_LedType, fmt::ptr(i_pData), data);
        }

        auto &lights = get_lights();
        if (i_CnPin == 0 && number_of_leds == 9 * 3) {
            // special handling converting to non-RGB lights
            // take the max(R, G, B) and use it to write the light value
            uint8_t light_value[9] = { 0 };
            for (uint32_t i = 0; i < number_of_leds; i += 3) {
                light_value[i / 3] =
                    std::max({ ((uint8_t *)i_pData)[i], ((uint8_t *)i_pData)[i + 1], ((uint8_t *)i_pData)[i + 2] });
            }

            constexpr Lights::popn_lights_t legacy_button_lights[] = {
                Lights::popn_lights_t::Button1,
                Lights::popn_lights_t::Button2,
                Lights::popn_lights_t::Button3,
                Lights::popn_lights_t::Button4,
                Lights::popn_lights_t::Button5,
                Lights::popn_lights_t::Button6,
                Lights::popn_lights_t::Button7,
                Lights::popn_lights_t::Button8,
                Lights::popn_lights_t::Button9
            };

            static_assert(std::size(legacy_button_lights) == 9);

            for (size_t light = 0; light < 9; light++) {
                // on the new cab, buttons are colorless plastic and they rely on RGB to be lit at all times, even when "off"
                // when translating to legacy on/off lights, treat anything above ~60% brightness as fully on,
                // otherwise off, to avoid dimly lit "off" state
                GameAPI::Lights::writeLight(
                    RI_MGR,
                    lights.at(legacy_button_lights[light]),
                    light_value[light] > 150 ? 1.f : 0.f);
            }

            // color buttons
            constexpr Lights::popn_lights_t button_lights[] = {
                Lights::popn_lights_t::PikaButton1_R, Lights::popn_lights_t::PikaButton1_G, Lights::popn_lights_t::PikaButton1_B,
                Lights::popn_lights_t::PikaButton2_R, Lights::popn_lights_t::PikaButton2_G, Lights::popn_lights_t::PikaButton2_B,
                Lights::popn_lights_t::PikaButton3_R, Lights::popn_lights_t::PikaButton3_G, Lights::popn_lights_t::PikaButton3_B,
                Lights::popn_lights_t::PikaButton4_R, Lights::popn_lights_t::PikaButton4_G, Lights::popn_lights_t::PikaButton4_B,
                Lights::popn_lights_t::PikaButton5_R, Lights::popn_lights_t::PikaButton5_G, Lights::popn_lights_t::PikaButton5_B,
                Lights::popn_lights_t::PikaButton6_R, Lights::popn_lights_t::PikaButton6_G, Lights::popn_lights_t::PikaButton6_B,
                Lights::popn_lights_t::PikaButton7_R, Lights::popn_lights_t::PikaButton7_G, Lights::popn_lights_t::PikaButton7_B,
                Lights::popn_lights_t::PikaButton8_R, Lights::popn_lights_t::PikaButton8_G, Lights::popn_lights_t::PikaButton8_B,
                Lights::popn_lights_t::PikaButton9_R, Lights::popn_lights_t::PikaButton9_G, Lights::popn_lights_t::PikaButton9_B
            };

            static_assert(std::size(button_lights) == 9 * 3);

            for (size_t light = 0; light < number_of_leds; light++) {
                GameAPI::Lights::writeLight(
                    RI_MGR,
                    lights.at(button_lights[light]),
                    ((uint8_t *)i_pData)[light] / 255.f);
            }
        }

        if (tapeledutils::is_enabled() && i_CnPin > 0 && i_CnPin < std::size(TAPELED_MAPPING)) {
            auto &map = TAPELED_MAPPING[i_CnPin];
            const auto data_size = std::min(map.data.capacity(), (size_t)number_of_leds / 3);

            // pick a color to use
            const auto rgb = tapeledutils::pick_color_from_led_tape(map, (uint8_t *)i_pData, data_size);

            // program the lights into API
            auto &lights = get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_r], rgb.r);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_g], rgb.g);
            GameAPI::Lights::writeLight(RI_MGR, lights[map.index_b], rgb.b);

            // tape LED output over API not implemented
            // for (size_t i = 0; i < data_size; i++) {
            //     map.data[i].r = ((uint8_t *)i_pData)[i * 3];
            //     map.data[i].g = ((uint8_t *)i_pData)[i * 3 + 1];
            //     map.data[i].b = ((uint8_t *)i_pData)[i * 3 + 2];
            // }
        }
    }

    void bi3a_hook_init() {

        // avoid double init
        static bool initialized = false;
        if (initialized) {
            return;
        } else {
            initialized = true;
        }

        // announce
        log_info("bi3a_hook", "init");

        // libaio-iob5.dll
        const auto libaioIob5Dll = "libaio-iob5.dll";
        detour::trampoline_try(libaioIob5Dll, "aioNMgrIob5_Create",
                               aioNMgrIob5_Create, &aioNMgrIob5_Create_orig);
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_Create",
                               aioIob5Bi3a_Create, &aioIob5Bi3a_Create_orig);
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_GetDeviceStatus",
                               aioIob5Bi3a_GetDeviceStatus, &aioIob5Bi3a_GetDeviceStatus_orig);
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_SetOutputData",
                               aioIob5Bi3a_SetOutputData, &aioIob5Bi3a_SetOutputData_orig);
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_SetTapeLedDataPart",
                               aioIob5Bi3a_SetTapeLedDataPart, &aioIob5Bi3a_SetTapeLedDataPart_orig);
        detour::trampoline_try(libaioIob5Dll, "aioIob5Bi3a_SetTapeLedDataLimit",
                               aioIob5Bi3a_SetTapeLedDataLimit, &aioIob5Bi3a_SetTapeLedDataLimit_orig);

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
