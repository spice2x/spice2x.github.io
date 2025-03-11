#include <winsock2.h>

#include <windows.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <string>

#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"
#include "util/logging.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"

// hooking related stuff
static decltype(GetAdaptersInfo) *GetAdaptersInfo_orig = nullptr;
static decltype(bind) *bind_orig = nullptr;

// settings
std::string NETWORK_ADDRESS = "10.9.0.0";
std::string NETWORK_SUBNET = "255.255.0.0";
static bool GetAdaptersInfo_log = true;

// network structs
static struct in_addr network;
static struct in_addr prefix;
static struct in_addr subnet;

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
    if (GetIpForwardTable(pIpForwardTable, &dwSize, 1) != NO_ERROR || pIpForwardTable->dwNumEntries == 0)
        return ret;

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

    // return original value
    GetAdaptersInfo_log = false;
    return ret;
}

static int WINAPI bind_hook(SOCKET s, const struct sockaddr *name, int namelen) {

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

    // cast to sockaddr_in
    struct sockaddr_in *in_name = (struct sockaddr_in *) name;

#pragma clang diagnostic pop

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
    log_info("network", "Network preferences: {}", s_network, s_subnet, s_prefix);

    // GetAdaptersInfo hook
    auto orig_addr = detour::iat_try(
        "GetAdaptersInfo", GetAdaptersInfo_hook, nullptr);
    if (!orig_addr) {
        log_warning("network", "Could not hook GetAdaptersInfo");
    } else if (GetAdaptersInfo_orig == nullptr) {
        GetAdaptersInfo_orig = orig_addr;
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
