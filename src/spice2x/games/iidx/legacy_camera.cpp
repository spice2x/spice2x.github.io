#include "games/iidx/legacy_camera.h"

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
#include "hooks/setupapihook.h"
#include "util/detour.h"
#include "util/memutils.h"
#include "util/logging.h"
#include "util/utils.h"

static VTBL_TYPE(IMFActivate, GetAllocatedString) GetAllocatedString_orig = nullptr;
static decltype(MFEnumDeviceSources) *MFEnumDeviceSources_orig = nullptr;

static bool should_flip_cams = false;

namespace games::iidx {

    /*
     * Camera related stuff
     */
    static std::wstring CAMERA0_ID;
    static std::wstring CAMERA1_ID;

    static HRESULT WINAPI GetAllocatedString_hook(IMFActivate* This, REFGUID guidKey, LPWSTR *ppwszValue,
                                                  UINT32 *pcchLength)
    {
        // call the original
        HRESULT result = GetAllocatedString_orig(This, guidKey, ppwszValue, pcchLength);

        // log the cam name
        log_misc("iidx::cam", "obtained {}", ws2s(*ppwszValue));

        // try first camera
        wchar_t *pwc = nullptr;
        if (CAMERA0_ID.length() == 23) {
            pwc = wcsstr(*ppwszValue, CAMERA0_ID.c_str());
        }

        // try second camera if first wasn't found
        if (!pwc && CAMERA1_ID.length() == 23) {
            pwc = wcsstr(*ppwszValue, CAMERA1_ID.c_str());
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

            log_misc("iidx::cam", "replaced {}", ws2s(*ppwszValue));
        }

        // return original result
        return result;
    }

    static void hook_camera(size_t no, std::wstring camera_id, std::string camera_instance) {

        // logic based on camera no
        if (no == 0) {

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
            camera_setting.device_id = "USB\\VEN_1022&DEV_7908";
            camera_setting.device_node_id = "USB\\VID_288C&PID_0002&MI_00\\?&????????&?&????";
            if (camera_instance.length() == 17) {
                for (int i = 0; i < 17; i++) {
                    camera_setting.device_node_id[28 + i] = camera_instance[i];
                }
            }
            cfgmgr32hook_add(camera_setting);

            log_info("iidx::cam", "hooked camera 1 @ {}", ws2s(camera_id));
        }
        else if (no == 1) {

            // don't hook if camera 1 is already hooked
            if (CAMERA1_ID.length() > 0) {
                return;
            }

            // save the camera ID
            CAMERA1_ID = camera_id;

            // cfgmgr hook
            CFGMGR32_HOOK_SETTING camera_setting;
            camera_setting.device_instance = 0xBEEFDEAD;
            camera_setting.parent_instance = ~camera_setting.device_instance;
            camera_setting.device_id = "USB\\VEN_1022&DEV_7914";
            camera_setting.device_node_id = "USB\\VID_288C&PID_0002&MI_00\\?&????????&?&????";
            if (camera_instance.length() == 17) {
                for (int i = 0; i < 17; i++) {
                    camera_setting.device_node_id[28 + i] = camera_instance[i];
                }
            }
            cfgmgr32hook_add(camera_setting);

            log_info("iidx::cam", "hooked camera 2 @ {}", ws2s(camera_id));
        }
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

        // iterate cameras
        size_t cam_hook_num = 0;
        for (size_t cam_num = 0; cam_num < *pcSourceActivate && cam_hook_num < 2; cam_num++) {

            // flip
            size_t cam_num_flipped = cam_num;
            if (should_flip_cams) {
                cam_num_flipped = *pcSourceActivate - cam_num - 1;
            }

            // get camera
            IMFActivate *camera = (*pppSourceActivate)[cam_num_flipped];

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

                log_info("iidx::cam", "found video capture device: {}", ws2s(camera_link_ws));

                // only support cameras that start with \\?\usb
                if (!string_begins_with(camera_link_ws, L"\\\\?\\usb")) {
                    continue;
                }

                // get camera instance
                std::string camera_link = ws2s(camera_link_ws);
                std::string camera_instance = camera_link.substr(32, 17);

                // hook the camera
                hook_camera(cam_hook_num, camera_id, camera_instance);

                // increase camera hook number
                cam_hook_num++;

            } else {
                log_warning("iidx::cam", "failed to open camera {}", cam_num_flipped);
            }
        }

        // return result
        return result_orig;
    }

    void init_legacy_camera_hook(bool flip_cams) {
        should_flip_cams = flip_cams;

        // camera media framework hook
        MFEnumDeviceSources_orig = detour::iat_try(
                "MFEnumDeviceSources", MFEnumDeviceSources_hook, avs::game::DLL_INSTANCE);

        // camera settings
        SETUPAPI_SETTINGS settings3 {};
        settings3.class_guid[0] = 0x00000000;
        settings3.class_guid[1] = 0x00000000;
        settings3.class_guid[2] = 0x00000000;
        settings3.class_guid[3] = 0x00000000;
        const char property3[] = "USB Composite Device";
        memcpy(settings3.property_devicedesc, property3, sizeof(property3));
        settings3.property_address[0] = 1;
        settings3.property_address[1] = 7;
        setupapihook_add(settings3);
    }

}