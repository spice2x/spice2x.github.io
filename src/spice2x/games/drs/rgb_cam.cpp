#include "games/drs/rgb_cam.h"

// set version to Windows 7 to enable Media Foundation functions
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601

// turn on C-style COM objects
#define CINTERFACE

#include <mfobjects.h>
#include <mfidl.h>
#include <string>

#include "avs/game.h"
#include "cfg/configurator.h"
#include "hooks/cfgmgr32hook.h"
#include "util/detour.h"
#include "util/memutils.h"
#include "util/logging.h"
#include "util/utils.h"

static VTBL_TYPE(IMFActivate, GetAllocatedString) GetAllocatedString_orig = nullptr;
static decltype(MFEnumDeviceSources) *MFEnumDeviceSources_orig = nullptr;

namespace games::drs {

    /*
     * Camera related stuff
     */
    static std::wstring CAMERA0_ID;

    static HRESULT WINAPI GetAllocatedString_hook(IMFActivate* This, REFGUID guidKey, LPWSTR *ppwszValue,
                                                  UINT32 *pcchLength)
    {
        // call the original
        HRESULT result = GetAllocatedString_orig(This, guidKey, ppwszValue, pcchLength);

        // log the cam name
        log_misc("drs::rgbcam", "obtained {}", ws2s(*ppwszValue));

        // try first camera
        wchar_t *pwc = nullptr;
        if (CAMERA0_ID.length() == 23) {
            pwc = wcsstr(*ppwszValue, CAMERA0_ID.c_str());
        }

        // check if camera could be identified
        if (pwc) {

            // fake the USB IDs
            pwc[4] = L'2';
            pwc[5] = L'8';
            pwc[6] = L'8';
            pwc[7] = L'c';

            pwc[13] = L'0';
            pwc[14] = L'0';
            pwc[15] = L'0';
            pwc[16] = L'2';

            pwc[21] = L'0';
            pwc[22] = L'0';

            log_misc("drs::rgbcam", "replaced {}", ws2s(*ppwszValue));
        }

        // return original result
        return result;
    }

    static void hook_camera(std::wstring camera_id, std::string camera_instance) {
        // don't hook if camera 0 is already hooked
        if (CAMERA0_ID.length() > 0) {
            return;
        }

        // save the camera ID
        CAMERA0_ID = camera_id;

        // cfgmgr hook
        CFGMGR32_HOOK_SETTING camera_setting;
        camera_setting.device_instance = 0xDEADBEEF;
        camera_setting.parent_instance = ~camera_setting.device_instance;
        camera_setting.device_id = "USB\\VEN_8086&DEV_8C2D";
        camera_setting.device_node_id = "USB\\VID_288C&PID_0002&MI_00\\?&????????&?&????";
        if (camera_instance.length() == 17) {
            for (int i = 0; i < 17; i++) {
                camera_setting.device_node_id[28 + i] = camera_instance[i];
            }
        }
        cfgmgr32hook_add(camera_setting);

        log_info("drs::rgbcam", "hooked camera @ {}", ws2s(camera_id));
    }

    static void hook_camera_vtable(IMFActivate *camera) {

        // hook allocated string method for camera identification
        memutils::VProtectGuard camera_guard(camera->lpVtbl);
        camera->lpVtbl->GetAllocatedString = GetAllocatedString_hook;
    }

    static HRESULT WINAPI MFEnumDeviceSources_hook(IMFAttributes *pAttributes, IMFActivate ***pppSourceActivate,
                                                   UINT32 *pcSourceActivate) {

        // call original function
        HRESULT result_orig = MFEnumDeviceSources_orig(pAttributes, pppSourceActivate, pcSourceActivate);

        // check for capture devices
        if (FAILED(result_orig) || !*pcSourceActivate) {
            return result_orig;
        }

        // get camera
        IMFActivate *camera = **pppSourceActivate;

        // save original method for later use
        if (GetAllocatedString_orig == nullptr) {
            GetAllocatedString_orig = camera->lpVtbl->GetAllocatedString;
        }

        // hook allocated string method for camera identification
        hook_camera_vtable(camera);

        // get camera link
        LPWSTR camera_link_lpwstr;
        UINT32 camera_link_length;
        if (SUCCEEDED(GetAllocatedString_orig(
                camera,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                &camera_link_lpwstr,
                &camera_link_length))) {

            // cut name to make ID
            std::wstring camera_link_ws = std::wstring(camera_link_lpwstr);
            std::wstring camera_id = camera_link_ws.substr(8, 23);

            log_info("drs::rgbcam", "found video capture device: {}", ws2s(camera_link_ws));

            // only support cameras that start with \\?\usb
            if (!string_begins_with(camera_link_ws, L"\\\\?\\usb")) {
                return result_orig;
            }

            // get camera instance
            std::string camera_link = ws2s(camera_link_ws);
            std::string camera_instance = camera_link.substr(32, 17);

            // hook the camera
            hook_camera(camera_id, camera_instance);
        } else {
            log_warning("drs::rgbcam", "failed to open camera");
        }

        // return result
        return result_orig;
    }

    void init_rgb_camera_hook() {
        log_info("drs::rgbcam", "initializing camera hook");

        cfgmgr32hook_init(avs::game::DLL_INSTANCE);

        // camera media framework hook
        MFEnumDeviceSources_orig = detour::iat_try(
                "MFEnumDeviceSources", MFEnumDeviceSources_hook, avs::game::DLL_INSTANCE);
    }
}
