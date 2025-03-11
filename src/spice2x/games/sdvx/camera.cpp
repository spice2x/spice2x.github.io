
// set version to Windows 7 to enable Media Foundation functions
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601

// turn on C-style COM objects
#define CINTERFACE

#include "camera.h"

#include <mfobjects.h>
#include <mfidl.h>

#include "avs/game.h"
#include "hooks/cfgmgr32hook.h"
#include "util/detour.h"
#include "util/memutils.h"
#include "util/utils.h"

static VTBL_TYPE(IMFActivate, GetAllocatedString) GetAllocatedString_orig = nullptr;

static decltype(MFEnumDeviceSources) *MFEnumDeviceSources_orig = nullptr;

namespace games::sdvx {

    static std::wstring CAMERA0_ID;

    static HRESULT WINAPI GetAllocatedString_hook(IMFActivate* This, REFGUID guidKey, LPWSTR *ppwszValue,
                                                  UINT32 *pcchLength) {
        // call the original
        HRESULT result = GetAllocatedString_orig(This, guidKey, ppwszValue, pcchLength);

        // try first camera
        wchar_t *pwc = nullptr;
        if (CAMERA0_ID.length() == 23)
            pwc = wcsstr(*ppwszValue, CAMERA0_ID.c_str());

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
        }

        // return original result
        return result;
    }

    static void hook_camera(IMFActivate* camera, size_t no, std::wstring camera_id, std::string camera_instance) {

        // don't hook if camera 0 is already hooked
        if (CAMERA0_ID.length() > 0)
            return;

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

        // save original method for later use
        if (GetAllocatedString_orig == nullptr) {
            GetAllocatedString_orig = camera->lpVtbl->GetAllocatedString;
        }

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
        for (size_t cam_num = 0; cam_num < *pcSourceActivate && cam_hook_num < 1; cam_num++) {

            // flip
            size_t cam_num_flipped = cam_num;

            // get camera link
            IMFActivate *camera = (*pppSourceActivate)[cam_num_flipped];
            LPWSTR camera_link_lpwstr;
            UINT32 camera_link_length;
            if (SUCCEEDED(camera->lpVtbl->GetAllocatedString(
                    camera,
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &camera_link_lpwstr,
                    &camera_link_length))) {

                // cut name to make ID
                std::wstring camera_link_ws = std::wstring(camera_link_lpwstr);
                std::wstring camera_id = camera_link_ws.substr(8, 23);

                // get camera instance
                std::string camera_link = ws2s(camera_link_ws);
                std::string camera_instance = camera_link.substr(32, 17);

                // hook the camera
                hook_camera(camera, cam_hook_num, camera_id, camera_instance);

                // increase camera hook number
                cam_hook_num++;

            }
        }

        // return result
        return result_orig;
    }

    void camera_init() {

        // camera media framework hook
        MFEnumDeviceSources_orig = detour::iat_try(
                "MFEnumDeviceSources", MFEnumDeviceSources_hook, avs::game::DLL_INSTANCE);
    }
}
