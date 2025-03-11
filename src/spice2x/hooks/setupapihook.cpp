#include "setupapihook.h"

#include <windows.h>
#include <setupapi.h>

#include "util/detour.h"
#include "util/utils.h"

// constants
static const GUID GUID_ZERO = { 0x0, 0x0, 0x0, { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };

// state
static std::vector<SETUPAPI_SETTINGS> SETUPAPI_SETTINGS_LIST;

// original function pointers
static decltype(SetupDiDestroyDeviceInfoList) *SetupDiDestroyDeviceInfoList_real = nullptr;
static decltype(SetupDiEnumDeviceInfo) *SetupDiEnumDeviceInfo_real = nullptr;
static decltype(SetupDiEnumDeviceInterfaces) *SetupDiEnumDeviceInterfaces_real = nullptr;
static decltype(SetupDiGetClassDevsA) *SetupDiGetClassDevsA_real = nullptr;
static decltype(SetupDiGetClassDevsW) *SetupDiGetClassDevsW_real = nullptr;
static decltype(SetupDiGetDeviceInfoListDetailA) *SetupDiGetDeviceInfoListDetailA_real = nullptr;
static decltype(SetupDiGetDeviceInstanceIdA) *SetupDiGetDeviceInstanceIdA_real = nullptr;
static decltype(SetupDiGetDeviceInterfaceDetailA) *SetupDiGetDeviceInterfaceDetailA_real = nullptr;
static decltype(SetupDiGetDeviceInterfaceDetailW) *SetupDiGetDeviceInterfaceDetailW_real = nullptr;
static decltype(SetupDiGetDeviceRegistryPropertyA) *SetupDiGetDeviceRegistryPropertyA_real = nullptr;

static HDEVINFO WINAPI SetupDiGetClassDevsA_hook(
    const GUID *ClassGuid,
    LPCSTR Enumerator,
    HWND hwndParent,
    DWORD Flags
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        const SETUPAPI_SETTINGS *settings = &SETUPAPI_SETTINGS_LIST[i];
        const GUID *guid = ClassGuid ? ClassGuid : &GUID_ZERO;

        if (!memcmp(guid, settings->class_guid, 16)) {
            SetLastError(ERROR_SUCCESS);
            return (HDEVINFO) &SETUPAPI_SETTINGS_LIST[i];
        }
    }

    // fallback
    return SetupDiGetClassDevsA_real(ClassGuid, Enumerator, hwndParent, Flags);
}

static HDEVINFO WINAPI SetupDiGetClassDevsW_hook(
    const GUID *ClassGuid,
    LPCWSTR Enumerator,
    HWND hwndParent,
    DWORD Flags
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        const SETUPAPI_SETTINGS *settings = &SETUPAPI_SETTINGS_LIST[i];

        if (ClassGuid && !memcmp(ClassGuid, settings->class_guid, 16)) {
            SetLastError(ERROR_SUCCESS);
            return (HDEVINFO) &SETUPAPI_SETTINGS_LIST[i];
        }
    }

    // fallback
    return SetupDiGetClassDevsW_real(ClassGuid, Enumerator, hwndParent, Flags);
}

static BOOL WINAPI SetupDiEnumDeviceInfo_hook(
    HDEVINFO DeviceInfoSet,
    DWORD MemberIndex,
    PSP_DEVINFO_DATA DeviceInfoData
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        if (DeviceInfoSet == &SETUPAPI_SETTINGS_LIST[i]) {
            if (MemberIndex) {
                SetLastError(ERROR_NO_MORE_ITEMS);
                return false;
            } else {
                SetLastError(ERROR_SUCCESS);
                return true;
            }
        }
    }

    // fallback
    return SetupDiEnumDeviceInfo_real(DeviceInfoSet, MemberIndex, DeviceInfoData);
}

static BOOL WINAPI SetupDiEnumDeviceInterfaces_hook(
    HDEVINFO DeviceInfoSet,
    PSP_DEVINFO_DATA DeviceInfoData,
    const GUID *InterfaceClassGuid,
    DWORD MemberIndex,
    PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        if (DeviceInfoSet == &SETUPAPI_SETTINGS_LIST[i]) {
            SetLastError(ERROR_SUCCESS);
            return MemberIndex == 0;
        }
    }

    // fallback
    return SetupDiEnumDeviceInterfaces_real(DeviceInfoSet, DeviceInfoData, InterfaceClassGuid, MemberIndex,
            DeviceInterfaceData);
}

static BOOL WINAPI SetupDiGetDeviceRegistryPropertyA_hook(
    HDEVINFO DeviceInfoSet,
    PSP_DEVINFO_DATA DeviceInfoData,
    DWORD Property,
    PDWORD PropertyRegDataType,
    PBYTE PropertyBuffer,
    DWORD PropertyBufferSize,
    PDWORD RequiredSize
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        const SETUPAPI_SETTINGS *settings = &SETUPAPI_SETTINGS_LIST[i];

        if (DeviceInfoSet == settings) {

            // dummy data
            const void *property_data = nullptr;
            DWORD property_size = 0;
            static int property_index = 0;

            // get property data
            switch (Property) {
                case SPDRP_DEVICEDESC:
                    property_data = settings->property_devicedesc;
                    property_size = (DWORD) (strlen(settings->property_devicedesc) + 1);
                    break;
                case SPDRP_HARDWAREID:
                    property_data = settings->property_hardwareid;
                    property_size = (DWORD) (strlen(settings->property_hardwareid) + 1);
                    break;
                case SPDRP_ADDRESS:
                    /*
                     * The property index thing is just because IIDX 25+ requires two different addresses to
                     * be spoofed for cameras. Doing this in a clean way requires a complete hook rewrite.
                     */
                    property_data = &settings->property_address[property_index];
                    property_size = sizeof(DWORD);
                    property_index = (property_index + 1) & 1;
                    break;
                case SPDRP_DRIVER:
                    // hides warnings from IIDX
                    property_data = "";
                    property_size = 0;
                    break;
                default:
                    log_warning("setupapi", "unknown property type: {}", Property);

                    SetLastError(ERROR_INVALID_DATA);

                    return false;
            }

            // check buffer size
            if (PropertyBufferSize < property_size) {
                if (RequiredSize) {
                    *RequiredSize = property_size;
                }
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }

            // write property
            if (PropertyRegDataType) {
                *PropertyRegDataType = 1;
            }
            memcpy(PropertyBuffer, property_data, property_size);
            SetLastError(ERROR_SUCCESS);
            return true;
        }
    }

    // fallback
    return SetupDiGetDeviceRegistryPropertyA_real(DeviceInfoSet, DeviceInfoData, Property, PropertyRegDataType,
            PropertyBuffer, PropertyBufferSize, RequiredSize);
}

static BOOL WINAPI SetupDiGetDeviceInterfaceDetailA_hook(
    HDEVINFO DeviceInfoSet,
    PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A DeviceInterfaceDetailData,
    DWORD DeviceInterfaceDetailDataSize,
    PDWORD RequiredSize,
    PSP_DEVINFO_DATA DeviceInfoData
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        const SETUPAPI_SETTINGS *settings = &SETUPAPI_SETTINGS_LIST[i];

        if (DeviceInfoSet == settings) {

            // get data length
            DWORD data_size = (DWORD) (strlen(settings->interface_detail) + 1);

            // check size
            if (DeviceInterfaceDetailDataSize < data_size) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                *RequiredSize = sizeof(DeviceInterfaceDetailData->cbSize) + data_size;
                return false;
            }

            // clear error
            SetLastError(ERROR_SUCCESS);

            // copy data size + data
            DeviceInterfaceDetailData->cbSize = data_size;
            memcpy(DeviceInterfaceDetailData->DevicePath, settings->interface_detail, data_size);

            // success
            return true;
        }
    }

    // fallback
    return SetupDiGetDeviceInterfaceDetailA_real(DeviceInfoSet, DeviceInterfaceData, DeviceInterfaceDetailData,
            DeviceInterfaceDetailDataSize, RequiredSize, DeviceInfoData);
}

static BOOL WINAPI SetupDiGetDeviceInterfaceDetailW_hook(
    HDEVINFO DeviceInfoSet,
    PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,
    DWORD DeviceInterfaceDetailDataSize,
    PDWORD RequiredSize,
    PSP_DEVINFO_DATA DeviceInfoData
) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        const SETUPAPI_SETTINGS *settings = &SETUPAPI_SETTINGS_LIST[i];

        if (DeviceInfoSet == settings) {

            // convert to wstring
            std::wstring ws = s2ws(std::string(settings->interface_detail));

            // get data length
            DWORD data_size = (DWORD) (ws.length() * 2 + 2);
            DWORD struct_size = data_size + sizeof(DWORD);

            // check size
            if (DeviceInterfaceDetailDataSize < struct_size) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                *RequiredSize = sizeof(DeviceInterfaceDetailData->cbSize) + struct_size;
                return false;
            }

            // clear error
            SetLastError(ERROR_SUCCESS);

            // copy data size + data
            DeviceInterfaceDetailData->cbSize = struct_size;
            memcpy(DeviceInterfaceDetailData->DevicePath, ws.c_str(), data_size);

            // success
            return true;
        }
    }

    // fallback
    return SetupDiGetDeviceInterfaceDetailW_real(DeviceInfoSet, DeviceInterfaceData, DeviceInterfaceDetailData,
            DeviceInterfaceDetailDataSize, RequiredSize, DeviceInfoData);
}

static BOOL WINAPI SetupDiGetDeviceInstanceIdA_hook(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, PSTR DeviceInstanceId, DWORD DeviceInstanceIdSize, PDWORD RequiredSize) {
    log_misc("setupapi", "SetupDiGetDeviceInstanceIdA hook hit ({}, {}, {}, {}, {})", fmt::ptr(DeviceInfoSet), fmt::ptr(DeviceInfoData), fmt::ptr(DeviceInstanceId), DeviceInstanceIdSize, fmt::ptr(RequiredSize));

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        const SETUPAPI_SETTINGS *settings = &SETUPAPI_SETTINGS_LIST[i];

        if (DeviceInfoSet == settings) {
            log_misc("setupapi", "returning device instance: {}", settings->property_hardwareid);

            strncpy(DeviceInstanceId, settings->property_hardwareid, DeviceInstanceIdSize);

            if (RequiredSize) {
                *RequiredSize = 0;
            }

            return true;
        }
    }

    return SetupDiGetDeviceInstanceIdA_real(DeviceInfoSet, DeviceInfoData, DeviceInstanceId, DeviceInstanceIdSize, RequiredSize);
}

static BOOL WINAPI SetupDiDestroyDeviceInfoList_hook(HDEVINFO DeviceInfoSet) {

    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        if (DeviceInfoSet == &SETUPAPI_SETTINGS_LIST[i]) {
            return true;
        }
    }

    // fallback
    return SetupDiDestroyDeviceInfoList_real(DeviceInfoSet);
}

static BOOL WINAPI SetupDiGetDeviceInfoListDetailA_hook(HDEVINFO DeviceInfoSet,
                                                        PSP_DEVINFO_LIST_DETAIL_DATA DeviceInfoSetDetailData) {
    // custom
    for (size_t i = 0; i < SETUPAPI_SETTINGS_LIST.size(); i++) {
        if (DeviceInfoSet == &SETUPAPI_SETTINGS_LIST[i]) {
            return true;
        }
    }

    // fallback
    return SetupDiGetDeviceInfoListDetailA_real(DeviceInfoSet, DeviceInfoSetDetailData);
}

void setupapihook_init(HINSTANCE module) {

    // check for skip
    if (!detour::iat_find("SetupDiGetClassDevsA", module) && !detour::iat_find("SetupDiGetClassDevsW", module)) {
        log_info("setupapi", "no usage detected in module");
        return;
    }

    // hook functions
    SetupDiDestroyDeviceInfoList_real = detour::iat_try(
            "SetupDiDestroyDeviceInfoList", SetupDiDestroyDeviceInfoList_hook, module);
    SetupDiEnumDeviceInfo_real = detour::iat_try(
            "SetupDiEnumDeviceInfo", SetupDiEnumDeviceInfo_hook, module);
    SetupDiEnumDeviceInterfaces_real = detour::iat_try(
            "SetupDiEnumDeviceInterfaces", SetupDiEnumDeviceInterfaces_hook, module);
    SetupDiGetClassDevsA_real = detour::iat_try(
            "SetupDiGetClassDevsA", SetupDiGetClassDevsA_hook, module);
    SetupDiGetClassDevsW_real = detour::iat_try(
            "SetupDiGetClassDevsW", SetupDiGetClassDevsW_hook, module);
    SetupDiGetDeviceInfoListDetailA_real = detour::iat_try(
            "SetupDiGetDeviceInfoListDetailA", SetupDiGetDeviceInfoListDetailA_hook, module);
    SetupDiGetDeviceInstanceIdA_real = detour::iat_try(
            "SetupDiGetDeviceInstanceIdA", SetupDiGetDeviceInstanceIdA_hook, module);
    SetupDiGetDeviceInterfaceDetailA_real = detour::iat_try(
            "SetupDiGetDeviceInterfaceDetailA", SetupDiGetDeviceInterfaceDetailA_hook, module);
    SetupDiGetDeviceInterfaceDetailW_real = detour::iat_try(
            "SetupDiGetDeviceInterfaceDetailW", SetupDiGetDeviceInterfaceDetailW_hook, module);
    SetupDiGetDeviceRegistryPropertyA_real = detour::iat_try(
            "SetupDiGetDeviceRegistryPropertyA", SetupDiGetDeviceRegistryPropertyA_hook, module);
}

void setupapihook_add(SETUPAPI_SETTINGS settings) {
    SETUPAPI_SETTINGS_LIST.push_back(settings);
}
