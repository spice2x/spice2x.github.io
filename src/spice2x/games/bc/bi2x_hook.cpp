#include "bi2x_hook.h"

#if SPICE64

#include <cstdint>
#include <iterator>

#include "util/detour.h"
#include "util/logging.h"
#include "rawinput/rawinput.h"
#include "games/io.h"
#include "io.h"
#include "util/tapeled.h"

namespace games::bc {

    struct AIO_IOB2_BI2X_TBS;

    /*
     * typedefs
     */

    typedef void (__fastcall *aioIob2Bi2xTBS_GetDeviceStatus_t)(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, void *o_DevStatus);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetButton0Lamp_t)(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetIrLed_t)(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetIccrLed_t)(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetStickLed_t)(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB);
    typedef void (__fastcall *aioIob2Bi2xTBS_SetTapeLedData_t)(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_TapeLed, uint8_t *i_pData);

    static aioIob2Bi2xTBS_GetDeviceStatus_t aioIob2Bi2xTBS_GetDeviceStatus_orig = nullptr;
    static aioIob2Bi2xTBS_SetButton0Lamp_t aioIob2Bi2xTBS_SetButton0Lamp_orig = nullptr;
    static aioIob2Bi2xTBS_SetIrLed_t aioIob2Bi2xTBS_SetIrLed_orig = nullptr;
    static aioIob2Bi2xTBS_SetIccrLed_t aioIob2Bi2xTBS_SetIccrLed_orig = nullptr;
    static aioIob2Bi2xTBS_SetStickLed_t aioIob2Bi2xTBS_SetStickLed_orig = nullptr;
    static aioIob2Bi2xTBS_SetTapeLedData_t aioIob2Bi2xTBS_SetTapeLedData_orig = nullptr;

    /*
     * hooks
     */

    static void write_light_rgb(
        Lights::bc_lights_t r, Lights::bc_lights_t g, Lights::bc_lights_t b, uint32_t color) {

        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(r), ((color >> 16) & 0xFF) / 255.f);
        GameAPI::Lights::writeLight(RI_MGR, lights.at(g), ((color >> 8) & 0xFF) / 255.f);
        GameAPI::Lights::writeLight(RI_MGR, lights.at(b), (color & 0xFF) / 255.f);
    }

    static void __fastcall aioIob2Bi2xTBS_GetDeviceStatus(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, void *o_DevStatus) {

        RI_MGR->devices_flush_output();

        if (aioIob2Bi2xTBS_GetDeviceStatus_orig) {
            aioIob2Bi2xTBS_GetDeviceStatus_orig(i_pNodeCtl, o_DevStatus);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetButton0Lamp(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn) {

        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::Button), (i_bOn ? 1.f : 0.f));

        if (aioIob2Bi2xTBS_SetButton0Lamp_orig) {
            aioIob2Bi2xTBS_SetButton0Lamp_orig(i_pNodeCtl, i_bOn);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetIrLed(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, bool i_bOn) {

        auto &lights = get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::IrLed), (i_bOn ? 1.f : 0.f));

        if (aioIob2Bi2xTBS_SetIrLed_orig) {
            aioIob2Bi2xTBS_SetIrLed_orig(i_pNodeCtl, i_bOn);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetIccrLed(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB) {

        write_light_rgb(Lights::CardReader_R, Lights::CardReader_G, Lights::CardReader_B, i_RGB);

        if (aioIob2Bi2xTBS_SetIccrLed_orig) {
            aioIob2Bi2xTBS_SetIccrLed_orig(i_pNodeCtl, i_RGB);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetStickLed(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_RGB) {

        write_light_rgb(Lights::Joystick_R, Lights::Joystick_G, Lights::Joystick_B, i_RGB);

        if (aioIob2Bi2xTBS_SetStickLed_orig) {
            aioIob2Bi2xTBS_SetStickLed_orig(i_pNodeCtl, i_RGB);
        }
    }

    static void __fastcall aioIob2Bi2xTBS_SetTapeLedData(
        AIO_IOB2_BI2X_TBS *i_pNodeCtl, uint32_t i_TapeLed, uint8_t *i_pData) {

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
            const auto rgb = tapeledutils::pick_color_from_led_tape(i_pData, map.data_size);

            auto &lights = get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights.at(map.r), rgb.r);
            GameAPI::Lights::writeLight(RI_MGR, lights.at(map.g), rgb.g);
            GameAPI::Lights::writeLight(RI_MGR, lights.at(map.b), rgb.b);
        }

        if (aioIob2Bi2xTBS_SetTapeLedData_orig) {
            aioIob2Bi2xTBS_SetTapeLedData_orig(i_pNodeCtl, i_TapeLed, i_pData);
        }
    }

    void bi2x_hook_init() {
        static bool initialized = false;
        if (initialized) {
            return;
        } else {
            initialized = true;
        }

        log_info("bc_bi2x_hook", "init");

        const auto libaioIob2VideoDll = "libaio-iob2_video.dll";
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_GetDeviceStatus",
                               aioIob2Bi2xTBS_GetDeviceStatus, &aioIob2Bi2xTBS_GetDeviceStatus_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetButton0Lamp",
                               aioIob2Bi2xTBS_SetButton0Lamp, &aioIob2Bi2xTBS_SetButton0Lamp_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetIrLed",
                               aioIob2Bi2xTBS_SetIrLed, &aioIob2Bi2xTBS_SetIrLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetIccrLed",
                               aioIob2Bi2xTBS_SetIccrLed, &aioIob2Bi2xTBS_SetIccrLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetStickLed",
                               aioIob2Bi2xTBS_SetStickLed, &aioIob2Bi2xTBS_SetStickLed_orig);
        detour::trampoline_try(libaioIob2VideoDll, "aioIob2Bi2xTBS_SetTapeLedData",
                               aioIob2Bi2xTBS_SetTapeLedData, &aioIob2Bi2xTBS_SetTapeLedData_orig);
    }
}

#endif
