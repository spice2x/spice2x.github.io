#include "sysutils.h"

#include <cstdlib>
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "util/libutils.h"
#include "util/logging.h"

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

    static void print_adapter(DWORD index, PDISPLAY_DEVICEA adapter, bool is_monitor) {
        if (adapter->StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) {
            return;
        }
        if (!(adapter->StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            return;
        }
        std::string prefix("device");
        if (is_monitor) {
            prefix = "    adapter";
        }
        log_misc("gpuinfo", "{} {} device name   : {}", prefix.c_str(), index, adapter->DeviceName);
        log_misc("gpuinfo", "{} {} device string : {}", prefix.c_str(), index, adapter->DeviceString);
        log_dbug("gpuinfo", "{} {} flags         : 0x{:x}", prefix.c_str(), index, adapter->StateFlags);

        if (!is_monitor)  {
            DEVMODEA devmode = {};
            devmode.dmSize = sizeof(devmode);
            if (EnumDisplaySettingsA(adapter->DeviceName, ENUM_CURRENT_SETTINGS, &devmode)) {
                log_misc(
                    "gpuinfo",
                    "{} {} resolution    : {}px * {}px @ {}Hz",
                    prefix.c_str(),
                    index,
                    devmode.dmPelsWidth, devmode.dmPelsHeight,
                    devmode.dmDisplayFrequency);
            } else {
                log_misc("gpuinfo", "EnumDisplaySettingsA failed");
            }
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
}
