#include "cfgmgr32hook.h"

#include <vector>
#include <windows.h>
#include <cfgmgr32.h>

#include "util/detour.h"

typedef CONFIGRET (WINAPI *CM_Locate_DevNodeA_t)(PDEVINST, DEVINSTID_A, ULONG);
typedef CONFIGRET (WINAPI *CM_Get_Parent_t)(PDEVINST, DEVINST, ULONG);
typedef CONFIGRET (WINAPI *CM_Get_Device_IDA_t)(DEVINST, PSTR, ULONG, ULONG);

static CM_Locate_DevNodeA_t CM_Locate_DevNodeA_real = nullptr;
static CM_Get_Parent_t CM_Get_Parent_real = nullptr;
static CM_Get_Device_IDA_t CM_Get_Device_IDA_real = nullptr;

static std::vector<CFGMGR32_HOOK_SETTING> CFGMGR32_HOOK_SETTINGS;

static CONFIGRET WINAPI CM_Locate_DevNodeA_hook(PDEVINST pdnDevInst, DEVINSTID_A pDeviceID, ULONG ulFlags) {

    // check device ID
    if (!pDeviceID)
        return CM_Locate_DevNodeA_real(pdnDevInst, pDeviceID, ulFlags);

    // custom
    for (auto &setting : CFGMGR32_HOOK_SETTINGS) {
        if (_stricmp(pDeviceID, setting.device_node_id.c_str()) == 0) {
            *pdnDevInst = (DEVINST) setting.device_instance;
            return CR_SUCCESS;
        }
    }

    // fallback
    return CM_Locate_DevNodeA_real(pdnDevInst, pDeviceID, ulFlags);
}

static CONFIGRET WINAPI CM_Get_Parent_hook(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags) {

    // custom
    for (auto &setting : CFGMGR32_HOOK_SETTINGS) {
        if (dnDevInst == (DEVINST) setting.device_instance) {
            *pdnDevInst = (DEVINST) setting.parent_instance;
            return CR_SUCCESS;
        }
    }

    // fallback
    return CM_Get_Parent_real(pdnDevInst, dnDevInst, ulFlags);
}

static CONFIGRET WINAPI CM_Get_Device_IDA_hook(DEVINST dnDevInst, PSTR Buffer, ULONG BufferLen, ULONG ulFlags) {

    // custom
    for (auto &setting : CFGMGR32_HOOK_SETTINGS) {
        if (dnDevInst == (DEVINST) setting.parent_instance) {

            // check buffer size
            if (BufferLen <= setting.device_id.length())
                return CR_BUFFER_SMALL;

            // copy device ID to buffer
            memcpy(Buffer, setting.device_id.c_str(), setting.device_id.length() + 1);
            return CR_SUCCESS;
        }
    }

    // fallback
    return CM_Get_Device_IDA_real(dnDevInst, Buffer, BufferLen, ulFlags);
}

void cfgmgr32hook_init(HINSTANCE module) {

    // hook functions
    CM_Locate_DevNodeA_real = (CM_Locate_DevNodeA_t) detour::iat_try(
            "CM_Locate_DevNodeA", (void *) &CM_Locate_DevNodeA_hook, module);
    CM_Get_Parent_real = (CM_Get_Parent_t) detour::iat_try(
            "CM_Get_Parent", (void *) &CM_Get_Parent_hook, module);
    CM_Get_Device_IDA_real = (CM_Get_Device_IDA_t) detour::iat_try(
            "CM_Get_Device_IDA", (void *) &CM_Get_Device_IDA_hook, module);
}

void cfgmgr32hook_add(CFGMGR32_HOOK_SETTING setting) {
    CFGMGR32_HOOK_SETTINGS.push_back(setting);
}
