// GetDisplayConfigBufferSizes etc is Vista+
#define _WIN32_WINNT 0x0601

#include "sysutils.h"

#include <cstdlib>
#include <mutex>

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "hooks/graphics/graphics.h"
#include "avs/game.h"
#include "util/detour.h"
#include "util/flags_helper.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#if 0
#define log_dbug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_dbug(module, format_str, ...)
#endif

namespace sysutils {

#pragma pack(push)
#pragma pack(1)

    // from https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemfirmwaretable
    typedef struct _RawSMBIOSData {
        BYTE Used20CallingMethod;
        BYTE SMBIOSMajorVersion;
        BYTE SMBIOSMinorVersion;
        BYTE DmiRevision;
        DWORD Length;
        PBYTE SMBIOSTableData;
    } RawSMBIOSData, *PRawSMBIOSData;

    // SMBIOS
    // https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.4.0.pdf
    typedef struct _SMBIOS_STRUCT_HEADER {
        uint8_t Type;
        uint8_t Length;
        uint16_t Handle;
    } SMBIOS_STRUCT_HEADER, *PSMBIOS_STRUCT_HEADER;

    typedef struct _SMBIOS_TYPE_1 {
        SMBIOS_STRUCT_HEADER Header;
        uint8_t Manufacturer;
        uint8_t ProductName;
        uint8_t Version;
        uint8_t SerialNumber;
        uint8_t UUID[16];
        uint8_t WakeUpType;
        uint8_t SKUNumber;
        uint8_t Family;
    } SMBIOS_TYPE_1, *PSMBIOS_TYPE_1;

    typedef struct _SMBIOS_TYPE_2 {
        SMBIOS_STRUCT_HEADER Header;
        uint8_t Manufacturer;
        uint8_t Product;
        uint8_t Version;
        uint8_t SerialNumber;
        uint8_t AssetTag;
        uint8_t FeatureFlags;
        uint8_t LocationInChassis;
        uint16_t ChassisHandle;
        uint8_t BoardType;
        uint8_t NumObjHandle;
        uint16_t *ObjHandles;
    } SMBIOS_TYPE_2, *PSMBIOS_TYPE_2;

#pragma pack(pop)

    static void dump_smbios_section(PBYTE smbios, DWORD length);
    static void dump_smbios_section_1(PSMBIOS_TYPE_1 table);
    static void dump_smbios_section_2(PSMBIOS_TYPE_2 table);
    static const char *find_string_after_struct(PSMBIOS_STRUCT_HEADER table, uint8_t str_number);

    typedef UINT (WINAPI *GetSystemFirmwareTable_t)(
        DWORD FirmwareTableProviderSignature,
        DWORD FirmwareTableID,
        PVOID pFirmwareTableBuffer,
        DWORD BufferSize
    );
    static GetSystemFirmwareTable_t GetSystemFirmwareTable = nullptr;

    std::string SECOND_MONITOR_OVERRIDE = "";
    static decltype(EnumDisplayDevicesA) *EnumDisplayDevicesA_orig = nullptr;

    void print_smbios() {
        DWORD bytes_written = 0;
        DWORD table_size = 0;
        LPBYTE table = nullptr;

        if (GetSystemFirmwareTable == nullptr) {
            auto k32 = libutils::try_module("kernel32.dll");
            if (k32 != nullptr) {
                GetSystemFirmwareTable = libutils::try_proc<GetSystemFirmwareTable_t>(
                        k32, "GetSystemFirmwareTable");
            }
        }

        if (GetSystemFirmwareTable == nullptr) {
            log_warning("smbios", "GetSystemFirmwareTable not found");
            return;
        }

        // calculate how big of a buffer is needed
        const uint32_t RSMB = 0x52534D42;
        table_size = GetSystemFirmwareTable(RSMB, 0, NULL, 0);
        if (table_size == 0){
            log_warning("smbios", "initial call to GetSystemFirmwareTable failed, GLE:{}", GetLastError());
            return;
        }

        // allocate memory
        table = (LPBYTE)malloc(table_size);
        if (table == nullptr) {
            log_warning("smbios", "malloc failed");
            return;
        }

        // actually get the table
        bytes_written = GetSystemFirmwareTable(RSMB, 0, table, table_size);
        if (bytes_written != table_size) {
            log_warning("smbios", "call to GetSystemFirmwareTable failed, GLE:{}", GetLastError());
            return;
        }

        // dump to console
        const PRawSMBIOSData raw_smbios = (PRawSMBIOSData)table;
        dump_smbios_section((PBYTE)&raw_smbios->SMBIOSTableData, raw_smbios->Length);

        // clean up
        if (table != nullptr) {
            free(table);
            table = nullptr;
        }
    }

    static void dump_smbios_section(PBYTE smbios, DWORD length) {
        PBYTE curr = smbios;
        size_t tables_dumped = 0;
        log_misc("smbios", "dumping SMBIOS information...");
        while (curr < (smbios + length)) {
            PSMBIOS_STRUCT_HEADER header = (PSMBIOS_STRUCT_HEADER)curr;

            log_dbug("smbios", "table: type {}; {} bytes", header->Type, header->Length);

            // spec-defined End-of-Table
            if (header->Type == 127 && header->Length == 4) {
                break;
            }

            if (header->Type == 1) {
                dump_smbios_section_1((PSMBIOS_TYPE_1)curr);
                tables_dumped += 1;
            } else if (header->Type == 2) {
                dump_smbios_section_2((PSMBIOS_TYPE_2)curr);
                tables_dumped += 1;
            }

            // stop once type 1 and type 2 are dumped
            if (2 <= tables_dumped) {
                break;
            }

            curr += header->Length;

            // skip over the string area, indicated by double null
            while (!(*curr == 0 && *(curr+1) == 0)) {
                curr += 1;
                if (curr >= (smbios + length)) {
                    break;
                }
            }
            curr += 2;
        }
    }

    static void dump_smbios_section_1(PSMBIOS_TYPE_1 table) {
        const char* manufacturer = find_string_after_struct(&table->Header, table->Manufacturer);
        const char* product_name = find_string_after_struct(&table->Header, table->ProductName);
        const char* sku = find_string_after_struct(&table->Header, table->SKUNumber);

        log_misc("smbios", "system manufacturer : {}", manufacturer);
        log_misc("smbios", "system product name : {}", product_name);
        log_misc("smbios", "system SKU          : {}", sku);

        return;
    }

    static void dump_smbios_section_2(PSMBIOS_TYPE_2 table) {
        const char* manufacturer = find_string_after_struct(&table->Header, table->Manufacturer);
        const char* product_name = find_string_after_struct(&table->Header, table->Product);

        log_misc("smbios", "baseboard manufacturer : {}", manufacturer);
        log_misc("smbios", "baseboard product name : {}", product_name);

        return;
    }

    static const char *find_string_after_struct(PSMBIOS_STRUCT_HEADER table, uint8_t str_number) {
        const char* strings = (const char*)((PBYTE)table + table->Length);
        // string numbers are 1-based in this spec (first string is at 1)
        if (str_number == 0 || *strings == 0) {
            return "";
        }
        for (uint8_t i = 1; i < str_number; i++) {
            strings += strlen((char*)strings);
            strings += 1;
        }
        return strings;
    }

    static std::string device_flags_to_string(DWORD flags) {
        FLAGS_START(flags);
        FLAG(flags, DISPLAY_DEVICE_ATTACHED_TO_DESKTOP);
        FLAG(flags, DISPLAY_DEVICE_MULTI_DRIVER);
        FLAG(flags, DISPLAY_DEVICE_PRIMARY_DEVICE);
        FLAG(flags, DISPLAY_DEVICE_MIRRORING_DRIVER);
        FLAG(flags, DISPLAY_DEVICE_VGA_COMPATIBLE);
        FLAG(flags, DISPLAY_DEVICE_REMOVABLE);

#if !defined(DISPLAY_DEVICE_ACC_DRIVER)
#define DISPLAY_DEVICE_ACC_DRIVER               0x00000040
#endif
        FLAG(flags, DISPLAY_DEVICE_ACC_DRIVER);
       
        FLAG(flags, DISPLAY_DEVICE_MODESPRUNED);
        FLAG(flags, DISPLAY_DEVICE_RDPUDD);
        FLAG(flags, DISPLAY_DEVICE_REMOTE);
        FLAG(flags, DISPLAY_DEVICE_DISCONNECT);
        FLAG(flags, DISPLAY_DEVICE_TS_COMPATIBLE);

#if !defined(DISPLAY_DEVICE_UNSAFE_MODES_ON)
#define DISPLAY_DEVICE_UNSAFE_MODES_ON          0x00080000
#endif
        FLAG(flags, DISPLAY_DEVICE_UNSAFE_MODES_ON);
        FLAGS_END(flags);
    }

    static std::string monitor_flags_to_string(DWORD flags) {
        FLAGS_START(flags);
        FLAG(flags, DISPLAY_DEVICE_ACTIVE);
        FLAG(flags, DISPLAY_DEVICE_ATTACHED);
        FLAGS_END(flags);
    }

    static void print_adapter(DWORD index, PDISPLAY_DEVICEA adapter, bool is_monitor) {
        if (!is_monitor) {
            log_misc("gpuinfo", "-------- device {} --------", index);
        }

        std::string prefix("device");
        if (is_monitor) {
            prefix = "    adapter";
        }
   
        if (!is_monitor)  {
            // get extended info for better friendly name of monitors
            std::string friendly_name = "unknown";
            const auto &monitors = enumerate_monitors();
            for (const auto& monitor : monitors) {
                if (monitor.display_name == adapter->DeviceName) {
                    friendly_name = ws2s(monitor.friendly_name.c_str());
                    break;
                }
            }

            log_misc("gpuinfo", "{} {} name       : {} ({}) @ {}",
                prefix.c_str(), index,
                adapter->DeviceName, friendly_name, adapter->DeviceString);

            // resolution, refresh rate
            DEVMODEA devmode = {};
            devmode.dmSize = sizeof(devmode);
            if (EnumDisplaySettingsA(adapter->DeviceName, ENUM_CURRENT_SETTINGS, &devmode)) {
                log_misc(
                    "gpuinfo", "{} {} resolution : {}px * {}px @ {}Hz",
                    prefix.c_str(),
                    index,
                    devmode.dmPelsWidth, devmode.dmPelsHeight,
                    devmode.dmDisplayFrequency);
            }
            
            log_misc("gpuinfo", "{} {} flags      : {}",
                prefix.c_str(), index, device_flags_to_string(adapter->StateFlags));

        } else {
            log_misc("gpuinfo", "{} {} name  : {} ({})",
                prefix.c_str(), index,
                adapter->DeviceName, adapter->DeviceString);

            log_misc("gpuinfo", "{} {} flags : {}",
                prefix.c_str(), index, monitor_flags_to_string(adapter->StateFlags));
        }
    }

    void print_gpus() {
        DWORD device_index = 0;
        DISPLAY_DEVICEA device = {};
        device.cb = sizeof(device);
        log_misc(
            "gpuinfo",
            "dumping GPU/monitor information... "
            "(note: these are current values **before** launching the game)");
        while (EnumDisplayDevicesA(nullptr, device_index, &device, 0)) {
            print_adapter(device_index, &device, false);

            DWORD monitor_index = 0;
            DISPLAY_DEVICEA monitor = {};
            monitor.cb = sizeof(monitor);
            while (EnumDisplayDevicesA((PCHAR)device.DeviceName, monitor_index, &monitor, 0)) {
                print_adapter(monitor_index, &monitor, true);
                monitor_index++;
            }
            device_index++;
        }
    }

    typedef void (WINAPI *RtlGetNtVersionNumbers_t)(
        PDWORD pNtMajorVersion,
        PDWORD pNtMinorVersion,
        PDWORD pNtBuildNumber
    );
    static RtlGetNtVersionNumbers_t RtlGetNtVersionNumbers = nullptr;

    void print_os() {
        if (RtlGetNtVersionNumbers == nullptr) {
            auto ntdll = libutils::try_module("ntdll.dll");
            if (ntdll != nullptr) {
                RtlGetNtVersionNumbers = libutils::try_proc<RtlGetNtVersionNumbers_t>(
                        ntdll, "RtlGetNtVersionNumbers");

                if (RtlGetNtVersionNumbers == nullptr) {
                    log_warning("sysutils", "RtlGetNtVersionNumbers not found");
                }
            }
        }

        if (RtlGetNtVersionNumbers != nullptr) {
            // OS major/minor can no longer be relied on
            // https://learn.microsoft.com/en-us/windows/win32/sysinfo/operating-system-version
            // there are other ways to obtain better strings (WMI, registry, winbrand.dll)
            // but they either don't work in Win7 or too expensive to link against
            DWORD buildnum = 0;
            RtlGetNtVersionNumbers(nullptr, nullptr, &buildnum);
            buildnum = buildnum & 0x0FFFFFFF;
            log_misc("sysutils", "Windows OS build number: {}", buildnum);
            return;
        }
    }

    static std::vector<MonitorEntry> enumerate_monitors_internal() {
        // for WinXP, since these are Vista+ or 7+ APIs
        const auto user32 = LoadLibraryA("user32.dll");
        if (!user32) {
            log_warning("graphics", "can't find user32.dll???");
            return {};
        }
        const auto GetDisplayConfigBufferSizes_addr =
            reinterpret_cast<decltype(GetDisplayConfigBufferSizes) *>(
                GetProcAddress(user32, "GetDisplayConfigBufferSizes"));
        const auto QueryDisplayConfig_addr =
            reinterpret_cast<decltype(QueryDisplayConfig) *>(
                GetProcAddress(user32, "QueryDisplayConfig"));
        const auto DisplayConfigGetDeviceInfo_addr =
            reinterpret_cast<decltype(DisplayConfigGetDeviceInfo) *>(
                GetProcAddress(user32, "DisplayConfigGetDeviceInfo"));
        if (GetDisplayConfigBufferSizes_addr == nullptr ||
            QueryDisplayConfig_addr == nullptr ||
            DisplayConfigGetDeviceInfo_addr == nullptr) {
            log_warning("sysutils", "OS does not support display config APIs");
            return {};
        }

        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        auto status = GetDisplayConfigBufferSizes_addr(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count);
        if (status != ERROR_SUCCESS) {
            log_warning("sysutils", "GetDisplayConfigBufferSizes failed: {}", status);
            return {};
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        status = QueryDisplayConfig_addr(
                QDC_ONLY_ACTIVE_PATHS,
                &path_count,
                paths.data(),
                &mode_count,
                modes.data(),
                nullptr);
        if (status != ERROR_SUCCESS) {
            log_warning("sysutils", "QueryDisplayConfig failed: {}", status);
            return {};
        }

        // shrink to actual returned items
        paths.resize(path_count);
        modes.resize(mode_count);

        std::vector<MonitorEntry> result;
        for (const auto& path : paths) {
            MonitorEntry entry = {};
            entry.adapter_id_LowPart = path.targetInfo.adapterId.LowPart;
            entry.adapter_id_HighPart = path.targetInfo.adapterId.HighPart;
            entry.id = path.targetInfo.id;

            // primary monitor?
            for (const auto& mode : modes) {
                if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
                    continue;
                }

                if (mode.adapterId.HighPart == path.sourceInfo.adapterId.HighPart &&
                    mode.adapterId.LowPart == path.sourceInfo.adapterId.LowPart &&
                    mode.id == path.sourceInfo.id) {
                    if (mode.sourceMode.position.x == 0 && mode.sourceMode.position.y == 0) {
                        entry.is_primary = true;
                    }
                    break;
                }
            }

            // device ID (\\.\DISPLAYn)
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name = {};
            source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source_name.header.size = sizeof(source_name);
            source_name.header.adapterId = path.sourceInfo.adapterId;
            source_name.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo_addr(&source_name.header) != ERROR_SUCCESS) {
                continue;
            }
            entry.display_name = ws2s(std::wstring(source_name.viewGdiDeviceName));

            // friendly name
            DISPLAYCONFIG_TARGET_DEVICE_NAME target_name = {};
            target_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target_name.header.size = sizeof(target_name);
            target_name.header.adapterId = path.targetInfo.adapterId;
            target_name.header.id = path.targetInfo.id;
            if (DisplayConfigGetDeviceInfo_addr(&target_name.header) == ERROR_SUCCESS) {
                entry.friendly_name = target_name.monitorFriendlyDeviceName;
            } else {
                entry.friendly_name = L"(unknown)";
            }

            // done
            result.emplace_back(entry);
        }

        return result;
    }

    const std::vector<MonitorEntry>& enumerate_monitors() {
        static const std::vector<MonitorEntry> monitors = enumerate_monitors_internal();
        return monitors;
    }

    static
    BOOL
    __stdcall
    EnumDisplayDevicesA_hook(
        LPCSTR lpDevice,
        DWORD iDevNum,
        PDISPLAY_DEVICEA lpDisplayDevice,
        DWORD dwFlags
    ) {
        if (EnumDisplayDevicesA_orig == nullptr) {
            return false;
        }

        // caller is enumerating monitors (adapters), not devices
        if (lpDevice != nullptr) {
            log_misc("sysutils", "EnumDisplayDevicesA: returning original results for device {} [{}]", lpDevice, iDevNum);
            return EnumDisplayDevicesA_orig(lpDevice, iDevNum, lpDisplayDevice, dwFlags);
        }

        // call the original first
        const auto result_orig = EnumDisplayDevicesA_orig(nullptr, iDevNum, lpDisplayDevice, dwFlags);
        if (!result_orig) {
            return result_orig;
        }

        log_misc(
            "sysutils",
            "EnumDisplayDevicesA_orig: {} [{}], StateFlags={:#x}",
            lpDisplayDevice->DeviceName,
            iDevNum,
            lpDisplayDevice->StateFlags);

        // this one is not relevant
        if ((lpDisplayDevice->StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
            return result_orig;
        }

        // if this is for the primary device, just return it
        if (lpDisplayDevice->StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            log_misc(
                "sysutils",
                "EnumDisplayDevicesA: returning original results for primary monitor [{}], {}, result={}",
                iDevNum,
                lpDisplayDevice->DeviceName,
                result_orig);
            return result_orig;
        }

        // this is not 100% confirmed, but hope that this helps in cases where second monitors get used
        // even though NumberOfAdaptersInGroup was set to 1 on hybrid laptops
        if (GRAPHICS_FORCE_SINGLE_ADAPTER) {
            lpDisplayDevice->StateFlags &= ~DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
            log_info(
                "sysutils",
                "hiding this monitor, {} @ index {} (-graphics-single-adapter)",
                lpDisplayDevice->DeviceName,
                iDevNum);
            return result_orig;
        }

        // for the second device (subscreen)...
        if (SECOND_MONITOR_OVERRIDE.empty()) {
            // if there is no user override, we find the device with lowest index (that isn't primary)
            for (DWORD i = 0;; i++) {
                DISPLAY_DEVICEA device = {};
                device.cb = sizeof(device);
                const auto result = EnumDisplayDevicesA_orig(nullptr, i, &device, 0);
                if (!result) {
                    break;
                }
                if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
                    // primary, skip this
                    continue;
                }
                if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
                    // disconnected, skip this as well
                    continue;
                }
                if (std::string(lpDisplayDevice->DeviceName) == device.DeviceName) {
                    log_info(
                        "sysutils",
                        "returning second monitor {} @ index {}",
                        lpDisplayDevice->DeviceName,
                        iDevNum);
                    return result_orig;
                } else {
                    // otherwise, fall through and hide this monitor
                    break;
                }
            }
        } else if (SECOND_MONITOR_OVERRIDE == lpDisplayDevice->DeviceName) {
            // if there is one preferred by the user, use this one, and hide others
            log_info(
                "sysutils",
                "returning second monitor, {} @ index {} (-sysutilssubmonitor)",
                lpDisplayDevice->DeviceName,
                iDevNum);
            return result_orig;
        }

        // this device should not be used; pretend that it's not connected
        log_info(
            "sysutils",
            "hiding this monitor, {} @ index {}",
            lpDisplayDevice->DeviceName,
            iDevNum);
        lpDisplayDevice->StateFlags &= ~DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        return result_orig;
    }

    void hook_EnumDisplayDevicesA() {
        EnumDisplayDevicesA_orig = detour::iat_try(
             "EnumDisplayDevicesA", EnumDisplayDevicesA_hook, avs::game::DLL_INSTANCE);
    }
}
