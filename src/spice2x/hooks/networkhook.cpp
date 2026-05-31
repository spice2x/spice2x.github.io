#include <winsock2.h>

#include <windows.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <string>
#include <mutex>
#include <stddef.h>

#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
#include "util/logging.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/deferlog.h"
#include "hooks/icmphook_net.h"

// hooking related stuff
static decltype(GetAdaptersInfo) *GetAdaptersInfo_orig = nullptr;
static decltype(GetIpAddrTable) *GetIpAddrTable_orig = nullptr;
static decltype(bind) *bind_orig = nullptr;

// settings
std::string NETWORK_ADDRESS = "10.9.0.0";
std::string NETWORK_SUBNET = "255.255.0.0";
static bool GetAdaptersInfo_log = true;
static bool GetIpAddrTable_log = true;

// network structs
static struct in_addr network;
static struct in_addr prefix;
static struct in_addr subnet;

static void defer_network_adapter_error() {
    static std::once_flag printed;
    std::call_once(printed, []() {
        deferredlogs::defer_error_messages({
            "network adapter issue detected!",
            "    ensure you have at least one network adapter with a valid IPv4 address",
            "    the IPv4 address can be external or internal, it just needs to be valid",
            "    the network adapter can be a wired or wireless connection",
            "    you still need to do this even if you are connecting to a local server!",
            });
    });
}

static bool is_valid_ipaddr_row(const MIB_IPADDRROW &row) {
    static const auto loopback = inet_addr("127.0.0.1");

    return row.dwAddr != 0 && row.dwAddr != loopback;
}

static MIB_IPADDRROW *find_preferred_ipaddr_row(PMIB_IPADDRTABLE table) {

    if (table == nullptr || table->dwNumEntries == 0) {
        return nullptr;
    }

    // prefer the row matching -adapternetwork/-adaptersubnet
    for (DWORD i = 0; i < table->dwNumEntries; i++) {
        auto &row = table->table[i];
        if (!is_valid_ipaddr_row(row)) {
            continue;
        }

        auto row_prefix = row.dwAddr & row.dwMask;
        if (row_prefix == prefix.s_addr && row.dwMask == subnet.s_addr) {
            return &row;
        }
    }

    // fall back to the interface Windows would route through by default
    PMIB_IPFORWARDTABLE pIpForwardTable = (MIB_IPFORWARDTABLE *) malloc(sizeof(MIB_IPFORWARDTABLE));
    DWORD dwSize = 0;
    if (GetIpForwardTable(pIpForwardTable, &dwSize, TRUE) == ERROR_INSUFFICIENT_BUFFER) {
        free(pIpForwardTable);
        pIpForwardTable = (MIB_IPFORWARDTABLE *) malloc(dwSize);
    }
    if (GetIpForwardTable(pIpForwardTable, &dwSize, TRUE) != NO_ERROR || pIpForwardTable->dwNumEntries == 0) {
        free(pIpForwardTable);
        return nullptr;
    }

    DWORD best = pIpForwardTable->table[0].dwForwardIfIndex;
    free(pIpForwardTable);

    for (DWORD i = 0; i < table->dwNumEntries; i++) {
        auto &row = table->table[i];
        if (row.dwIndex == best && is_valid_ipaddr_row(row)) {
            return &row;
        }
    }

    // last resort: keep a deterministic valid row instead of exposing all adapters
    for (DWORD i = 0; i < table->dwNumEntries; i++) {
        auto &row = table->table[i];
        if (is_valid_ipaddr_row(row)) {
            return &row;
        }
    }

    return nullptr;
}

static void keep_only_ipaddr_row(PMIB_IPADDRTABLE table, MIB_IPADDRROW *row) {
    if (table == nullptr || row == nullptr) {
        return;
    }

    if (GetIpAddrTable_log) {
        in_addr addr {};
        addr.s_addr = row->dwAddr;
        log_info("network", "Using preferred IP address row: {}", inet_ntoa(addr));
    }

    table->table[0] = *row;
    table->dwNumEntries = 1;
    GetIpAddrTable_log = false;
}

static ULONG WINAPI GetAdaptersInfo_hook(PIP_ADAPTER_INFO pAdapterInfo, PULONG pOutBufLen) {

    // call orig
    ULONG ret = GetAdaptersInfo_orig(pAdapterInfo, pOutBufLen);
    if (ret != ERROR_SUCCESS) {

        // workaround for QMA not having enough buffer space
        if (pAdapterInfo != nullptr && avs::game::is_model({ "LMA", "MMA" })) {

            // allocate the output buffer size
            auto pAdapterInfo2 = (PIP_ADAPTER_INFO) malloc(*pOutBufLen);

            // call ourself with an appropriate buffer size
            ret = GetAdaptersInfo_hook(pAdapterInfo2, pOutBufLen);
            if (ret != ERROR_SUCCESS) {
                return ret;
            }

            // copy best interface
            memcpy(pAdapterInfo, pAdapterInfo2, sizeof(*pAdapterInfo));
            pAdapterInfo->Next = nullptr;

            // free our allocated memory
            free(pAdapterInfo2);
        }

        if (ret != ERROR_SUCCESS && ret != ERROR_BUFFER_OVERFLOW) {
            defer_network_adapter_error();
            log_warning(
                "network",
                "GetAdaptersInfo failed with {}; "
                "check if you have at least one network adapter with a valid IPv4 address!",
                ret);
        }

        return ret;
    }

    // set the best network adapter
    PIP_ADAPTER_INFO info = pAdapterInfo;
    while (info != nullptr) {

        // set subnet
        struct in_addr info_subnet;
        info_subnet.s_addr = inet_addr(info->IpAddressList.IpMask.String);

        // set prefix
        struct in_addr info_prefix;
        info_prefix.s_addr = inet_addr(info->IpAddressList.IpAddress.String) & info_subnet.s_addr;

        // check base IP and subnet
        bool isCorrectBaseIp = prefix.s_addr == info_prefix.s_addr;
        bool isCorrectSubnetMask = subnet.s_addr == info_subnet.s_addr;

        // check if requirements are met
        if (isCorrectBaseIp && isCorrectSubnetMask) {

            // log adapter
            if (GetAdaptersInfo_log)
                log_info("network", "Using preferred network adapter: {}, {}",
                        info->AdapterName,
                        info->Description);

            // set adapter information
            memcpy(pAdapterInfo, info, sizeof(*info));
            pAdapterInfo->Next = nullptr;

            // we're done
            GetAdaptersInfo_log = false;
            return ret;
        }

        // iterate
        info = info->Next;
    }

    // get IP forward table
    PMIB_IPFORWARDTABLE pIpForwardTable = (MIB_IPFORWARDTABLE *) malloc(sizeof(MIB_IPFORWARDTABLE));
    DWORD dwSize = 0;
    if (GetIpForwardTable(pIpForwardTable, &dwSize, 1) == ERROR_INSUFFICIENT_BUFFER) {
        free(pIpForwardTable);
        pIpForwardTable = (MIB_IPFORWARDTABLE *) malloc(dwSize);
    }
    if (GetIpForwardTable(pIpForwardTable, &dwSize, 1) != NO_ERROR || pIpForwardTable->dwNumEntries == 0) {
        defer_network_adapter_error();
        if (GetAdaptersInfo_log) {
            log_warning("network", "GetIpForwardTable failed");
        }
        return ret;
    }

    // determine best interface
    DWORD best = pIpForwardTable->table[0].dwForwardIfIndex;
    free(pIpForwardTable);

    // find fallback adapter
    info = pAdapterInfo;
    while (info != nullptr) {

        // check if this the adapter we search for
        if (info->Index == best) {

            // log information
            if (GetAdaptersInfo_log)
                log_info("network", "Using fallback network adapter: {}, {}",
                        info->AdapterName,
                        info->Description);


            // set adapter information
            memcpy(pAdapterInfo, info, sizeof(*info));
            pAdapterInfo->Next = nullptr;

            // exit the loop
            break;
        }

        // iterate
        info = info->Next;
    }

    if (pAdapterInfo->IpAddressList.IpAddress.String[0] == 0 ||
        pAdapterInfo->IpAddressList.IpAddress.String[0] == '0') {
        defer_network_adapter_error();
        if (GetAdaptersInfo_log) {
            log_warning(
                "network",
                "invalid IPv4 address for adapter {}, {} = {}; "
                "ensure you have at least one network adapter with valid IPv4 address!",
                pAdapterInfo->AdapterName,
                pAdapterInfo->Description,
                pAdapterInfo->IpAddressList.IpAddress.String);
        }
    }

    // return original value
    GetAdaptersInfo_log = false;
    return ret;
}

static DWORD WINAPI GetIpAddrTable_hook(PMIB_IPADDRTABLE pIpAddrTable, PULONG pdwSize, BOOL bOrder) {

    auto input_size = pdwSize != nullptr ? *pdwSize : 0;
    auto ret = GetIpAddrTable_orig(pIpAddrTable, pdwSize, bOrder);

    if (ret == NO_ERROR) {
        auto row = find_preferred_ipaddr_row(pIpAddrTable);
        if (row != nullptr) {
            keep_only_ipaddr_row(pIpAddrTable, row);
        }
        return ret;
    }

    if (ret != ERROR_INSUFFICIENT_BUFFER || pIpAddrTable == nullptr || pdwSize == nullptr) {
        return ret;
    }

    // If the caller's buffer is large enough for the filtered single-row table,
    // satisfy the call even when Windows needed more room for all adapters.
    const auto one_row_size = offsetof(MIB_IPADDRTABLE, table) + sizeof(MIB_IPADDRROW);
    if (input_size < one_row_size) {
        return ret;
    }

    auto full_size = *pdwSize;
    auto table = (PMIB_IPADDRTABLE) malloc(full_size);
    if (table == nullptr) {
        return ret;
    }

    auto full_ret = GetIpAddrTable_orig(table, &full_size, bOrder);
    if (full_ret == NO_ERROR) {
        auto row = find_preferred_ipaddr_row(table);
        if (row != nullptr) {
            keep_only_ipaddr_row(pIpAddrTable, row);
            *pdwSize = one_row_size;
            ret = NO_ERROR;
        }
    }

    free(table);
    return ret;
}

static int WINAPI bind_hook(SOCKET s, const struct sockaddr *name, int namelen) {

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
#endif

    int icmp_bind_result = 0;
    if (icmphook_try_bind(s, name, namelen, &icmp_bind_result)) {
        return icmp_bind_result;
    }

    // cast to sockaddr_in
    struct sockaddr_in *in_name = (struct sockaddr_in *) name;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

    // override bind to allow all hosts
    in_name->sin_addr.s_addr = inet_addr("0.0.0.0");

    // call original
    int ret = bind_orig(s, name, namelen);
    if (ret != 0) {
        log_warning("network", "bind failed: {}", WSAGetLastError());
    }

    // return result
    return ret;
}

void networkhook_init() {

    // announce init
    log_info("network", "SpiceTools Network");

    // set some same defaults
    network.s_addr = inet_addr(NETWORK_ADDRESS.c_str());
    subnet.s_addr = inet_addr(NETWORK_SUBNET.c_str());
    prefix.s_addr = network.s_addr & subnet.s_addr;

    // inet_ntoa(...) reuses the same char array so the results must be copied
    char s_network[17]{}, s_subnet[17]{}, s_prefix[17]{};
    strncpy(s_network, inet_ntoa(network), 16);
    strncpy(s_subnet, inet_ntoa(subnet), 16);
    strncpy(s_prefix, inet_ntoa(prefix), 16);

    // log preferences
    // log_info("network", "Network preferences: {}, {}, {}", s_network, s_subnet, s_prefix);

    // GetAdaptersInfo hook
    auto orig_addr = detour::iat_try(
        "GetAdaptersInfo", GetAdaptersInfo_hook, nullptr);
    if (!orig_addr) {
        log_warning("network", "Could not hook GetAdaptersInfo");
    } else if (GetAdaptersInfo_orig == nullptr) {
        GetAdaptersInfo_orig = orig_addr;
    }

    // GetIpAddrTable hook
    auto ip_addr_table_orig_addr = detour::iat_try(
        "GetIpAddrTable", GetIpAddrTable_hook, nullptr);
    if (!ip_addr_table_orig_addr) {
        log_warning("network", "Could not hook GetIpAddrTable");
    } else if (GetIpAddrTable_orig == nullptr) {
        GetIpAddrTable_orig = ip_addr_table_orig_addr;
    }

    /*
     * Bind Hook
     */
    bool bind_hook_enabled = true;

    // disable hook for DDR A since the bind hook crashes there for some reason
    if (fileutils::file_exists(MODULE_PATH / "gamemdx.dll")) {
        bind_hook_enabled = false;
    }

    // hook bind
    if (bind_hook_enabled) {

        // hook by name
        auto new_bind_orig = detour::iat_try("bind", bind_hook, nullptr);
        if (bind_orig == nullptr) {
            bind_orig = new_bind_orig;
        }

        // hook ESS by ordinal
        HMODULE ess = libutils::try_module("ess.dll");
        if (ess) {
            auto new_bind_orig2 = detour::iat_try_ordinal("WS2_32.dll", 2, bind_hook, ess);

            // try to get some valid pointer
            if (bind_orig == nullptr && new_bind_orig2 != nullptr) {
                bind_orig = new_bind_orig2;
            }
        }
    }
}
