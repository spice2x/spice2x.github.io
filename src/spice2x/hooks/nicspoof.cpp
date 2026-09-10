#define _WIN32_WINNT 0x0601

#include "nicspoof.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "util/detour.h"
#include "util/logging.h"

namespace {
constexpr uint32_t k_local_ip = 0x0A390302u; /* 10.57.3.2 */
constexpr uint32_t k_gateway = 0x0A390301u;  /* 10.57.3.1 */
constexpr uint32_t k_dns = 0x0A390301u;      /* 10.57.3.1 */
constexpr uint32_t k_dhcp = 0x0A390301u;     /* 10.57.3.1 */
constexpr uint32_t k_mask = 0xFFFFFF00u;     /* 255.255.255.0 */
constexpr uint32_t k_subnet = 0x0A390300u;
constexpr int k_prefix_len = 24;
constexpr DWORD k_ifindex = 77;
constexpr uint8_t k_fake_mac[6] = {0x12, 0x37, 0x13, 0x37, 0x13, 0x37};
constexpr char k_hostname[] = "N1CSP00F";
constexpr char k_domain[] = "local";
constexpr char k_hostname_fqdn[] = "N1CSP00F.local";
constexpr wchar_t k_domain_w[] = L"local";
constexpr char k_adapter_name[] = "{NICSPOOF-0880-0001-4250-4E6963537066}";
constexpr char k_adapter_desc[] = "NicSpoof Virtual Ethernet";
constexpr wchar_t k_friendly_name[] = L"NicSpoof";
constexpr wchar_t k_adapter_desc_w[] = L"NicSpoof Virtual Ethernet";

#define ALIGN_UP_PTR(p, a) \
    ((BYTE *)(((ULONG_PTR)(p) + ((ULONG_PTR)(a) - 1)) & ~((ULONG_PTR)(a) - 1)))

bool g_log_adapters = true;
bool g_log_params = true;

[[maybe_unused]] decltype(GetAdaptersAddresses) *GetAdaptersAddresses_orig = nullptr;
[[maybe_unused]] decltype(GetAdaptersInfo) *GetAdaptersInfo_orig = nullptr;
[[maybe_unused]] decltype(GetNetworkParams) *GetNetworkParams_orig = nullptr;
[[maybe_unused]] decltype(getaddrinfo) *getaddrinfo_orig = nullptr;
[[maybe_unused]] decltype(freeaddrinfo) *freeaddrinfo_orig = nullptr;

void ip_to_str(uint32_t ip, char *out, size_t n) {
    if (!out || n == 0) {
        return;
    }
    snprintf(out, n, "%u.%u.%u.%u",
            (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    out[n - 1] = 0;
}

uint32_t parse_ipv4(const char *s) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    char trail = 0;
    if (!s) {
        return 0;
    }
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &trail) != 4) {
        return 0;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return 0;
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

bool name_is_local(const char *name) {
    if (!name || !name[0]) {
        return true;
    }
    if (_stricmp(name, "localhost") == 0) {
        return true;
    }
    if (_stricmp(name, k_hostname) == 0) {
        return true;
    }
    if (_stricmp(name, k_hostname_fqdn) == 0) {
        return true;
    }
    return false;
}

bool name_is_numeric_ipv4(const char *name) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    char trail = 0;
    if (!name) {
        return false;
    }
    if (sscanf(name, "%u.%u.%u.%u%c", &a, &b, &c, &d, &trail) != 4) {
        return false;
    }
    return a <= 255 && b <= 255 && c <= 255 && d <= 255;
}

DWORD WINAPI GetAdaptersInfo_hook(PIP_ADAPTER_INFO p, PULONG s) {
    ULONG need = sizeof(IP_ADAPTER_INFO) + 32;
    char ip[16];
    char gw[16];
    char dhcp[16];

    if (!s) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!p || *s < need) {
        *s = need;
        return ERROR_BUFFER_OVERFLOW;
    }
    memset(p, 0, need);
    p->Index = k_ifindex;
    strcpy(p->AdapterName, k_adapter_name);
    strcpy(p->Description, k_adapter_desc);
    p->AddressLength = 6;
    memcpy(p->Address, k_fake_mac, 6);
    p->Type = MIB_IF_TYPE_ETHERNET;
    p->DhcpEnabled = 1;
    ip_to_str(k_local_ip, ip, sizeof(ip));
    ip_to_str(k_gateway, gw, sizeof(gw));
    ip_to_str(k_dhcp, dhcp, sizeof(dhcp));
    strcpy(p->IpAddressList.IpAddress.String, ip);
    ip_to_str(k_mask, p->IpAddressList.IpMask.String,
            sizeof(p->IpAddressList.IpMask.String));
    p->IpAddressList.Context = 0;
    p->Next = nullptr;
    strcpy(p->GatewayList.IpAddress.String, gw);
    p->GatewayList.IpMask.String[0] = 0;
    p->GatewayList.Context = 0;
    p->GatewayList.Next = nullptr;
    strcpy(p->DhcpServer.IpAddress.String, dhcp);
    p->DhcpServer.IpMask.String[0] = 0;
    p->DhcpServer.Context = 0;
    p->DhcpServer.Next = nullptr;
    *s = need;
    if (g_log_adapters) {
        log_misc("network", "NIC spoof GetAdaptersInfo ip={} gw={} ifindex={}",
                ip, gw, static_cast<unsigned>(k_ifindex));
        g_log_adapters = false;
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI GetNetworkParams_hook(PFIXED_INFO p, PULONG s) {
    ULONG need = static_cast<ULONG>(sizeof(FIXED_INFO));
    char dns[16];

    if (!s) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!p || *s < need) {
        *s = need;
        return ERROR_BUFFER_OVERFLOW;
    }
    memset(p, 0, need);
    strncpy(p->HostName, k_hostname, sizeof(p->HostName) - 1);
    strncpy(p->DomainName, k_domain, sizeof(p->DomainName) - 1);
    ip_to_str(k_dns, dns, sizeof(dns));
    strcpy(p->DnsServerList.IpAddress.String, dns);
    p->DnsServerList.IpMask.String[0] = 0;
    p->DnsServerList.Context = 0;
    p->DnsServerList.Next = nullptr;
    p->CurrentDnsServer = &p->DnsServerList;
    p->NodeType = 1;
    p->EnableDns = 1;
    *s = need;
    if (g_log_params) {
        log_misc("network", "NIC spoof GetNetworkParams host={} domain={} dns={}",
                k_hostname, k_domain, dns);
        g_log_params = false;
    }
    return ERROR_SUCCESS;
}

ULONG WINAPI GetAdaptersAddresses_hook(
        ULONG Family,
        ULONG Flags,
        PVOID Reserved,
        PIP_ADAPTER_ADDRESSES AdapterAddresses,
        PULONG SizePointer) {
    enum { STR_SLOT = 64 };
    /*
     * Layout is fixed: adapter header, 4 string slots, then unicast/prefix/
     * gateway/dns + sockaddrs with alignment slack. need is computed to always
     * fit; never return BUFFER_OVERFLOW after a successful size probe.
     */
    ULONG need = static_cast<ULONG>(sizeof(IP_ADAPTER_ADDRESSES) + STR_SLOT * 4 +
            sizeof(void *) * 8 +
            sizeof(IP_ADAPTER_UNICAST_ADDRESS) +
            sizeof(IP_ADAPTER_PREFIX) +
            sizeof(IP_ADAPTER_DNS_SERVER_ADDRESS) +
            sizeof(IP_ADAPTER_GATEWAY_ADDRESS) +
            sizeof(sockaddr_in) * 4 + 64);
    BYTE *blob = nullptr;
    PIP_ADAPTER_ADDRESSES a = nullptr;
    PIP_ADAPTER_UNICAST_ADDRESS u = nullptr;
    PIP_ADAPTER_PREFIX pref = nullptr;
    PIP_ADAPTER_GATEWAY_ADDRESS gw = nullptr;
    PIP_ADAPTER_DNS_SERVER_ADDRESS dns = nullptr;
    sockaddr_in *sa = nullptr;
    sockaddr_in *sm = nullptr;
    sockaddr_in *sg = nullptr;
    sockaddr_in *ds = nullptr;

    (void)Reserved;
    (void)Flags;

    if (Family != AF_INET && Family != AF_UNSPEC) {
        if (SizePointer) {
            *SizePointer = 0;
        }
        return ERROR_NO_DATA;
    }
    if (!SizePointer) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!AdapterAddresses || *SizePointer < need) {
        *SizePointer = need;
        return ERROR_BUFFER_OVERFLOW;
    }

    memset(AdapterAddresses, 0, need);
    a = AdapterAddresses;
    blob = reinterpret_cast<BYTE *>(a + 1);

    a->Length = sizeof(IP_ADAPTER_ADDRESSES);
    a->IfIndex = k_ifindex;
    a->AdapterName = reinterpret_cast<PCHAR>(blob);
    strcpy(reinterpret_cast<char *>(blob), k_adapter_name);
    blob += STR_SLOT;
    a->FriendlyName = reinterpret_cast<PWCHAR>(blob);
    wcscpy(reinterpret_cast<wchar_t *>(blob), k_friendly_name);
    blob += STR_SLOT;
    a->Description = reinterpret_cast<PWCHAR>(blob);
    wcscpy(reinterpret_cast<wchar_t *>(blob), k_adapter_desc_w);
    blob += STR_SLOT;
    a->PhysicalAddressLength = 6;
    memcpy(a->PhysicalAddress, k_fake_mac, 6);
    a->Flags = IP_ADAPTER_DHCP_ENABLED;
    a->Mtu = 1500;
    a->IfType = IF_TYPE_ETHERNET_CSMACD;
    a->OperStatus = IfOperStatusUp;
    a->Ipv4Enabled = 1;
    a->DnsSuffix = reinterpret_cast<PWCHAR>(blob);
    wcsncpy(reinterpret_cast<wchar_t *>(blob), k_domain_w,
            (STR_SLOT / sizeof(wchar_t)) - 1);
    reinterpret_cast<wchar_t *>(blob)[(STR_SLOT / sizeof(wchar_t)) - 1] = 0;
    blob += STR_SLOT;

    blob = ALIGN_UP_PTR(blob, sizeof(void *));
    u = reinterpret_cast<PIP_ADAPTER_UNICAST_ADDRESS>(blob);
    blob += sizeof(*u);
    memset(u, 0, sizeof(*u));
    u->Length = sizeof(*u);
    u->DadState = IpDadStatePreferred;
    u->OnLinkPrefixLength = static_cast<UCHAR>(k_prefix_len);
    sa = reinterpret_cast<sockaddr_in *>(blob);
    blob += sizeof(*sa);
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(k_local_ip);
    u->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sa);
    u->Address.iSockaddrLength = sizeof(*sa);
    a->FirstUnicastAddress = u;

    blob = ALIGN_UP_PTR(blob, sizeof(void *));
    pref = reinterpret_cast<PIP_ADAPTER_PREFIX>(blob);
    blob += sizeof(*pref);
    memset(pref, 0, sizeof(*pref));
    pref->Length = sizeof(*pref);
    pref->PrefixLength = static_cast<ULONG>(k_prefix_len);
    sm = reinterpret_cast<sockaddr_in *>(blob);
    blob += sizeof(*sm);
    memset(sm, 0, sizeof(*sm));
    sm->sin_family = AF_INET;
    sm->sin_addr.s_addr = htonl(k_subnet);
    pref->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sm);
    pref->Address.iSockaddrLength = sizeof(*sm);
    a->FirstPrefix = pref;

    blob = ALIGN_UP_PTR(blob, sizeof(void *));
    gw = reinterpret_cast<PIP_ADAPTER_GATEWAY_ADDRESS>(blob);
    blob += sizeof(*gw);
    memset(gw, 0, sizeof(*gw));
    gw->Length = sizeof(*gw);
    sg = reinterpret_cast<sockaddr_in *>(blob);
    blob += sizeof(*sg);
    memset(sg, 0, sizeof(*sg));
    sg->sin_family = AF_INET;
    sg->sin_addr.s_addr = htonl(k_gateway);
    gw->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sg);
    gw->Address.iSockaddrLength = sizeof(*sg);
    a->FirstGatewayAddress = gw;

    blob = ALIGN_UP_PTR(blob, sizeof(void *));
    dns = reinterpret_cast<PIP_ADAPTER_DNS_SERVER_ADDRESS>(blob);
    blob += sizeof(*dns);
    memset(dns, 0, sizeof(*dns));
    ds = reinterpret_cast<sockaddr_in *>(blob);
    blob += sizeof(*ds);
    memset(ds, 0, sizeof(*ds));
    ds->sin_family = AF_INET;
    ds->sin_addr.s_addr = htonl(k_dns);
    dns->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(ds);
    dns->Address.iSockaddrLength = sizeof(*ds);
    dns->Next = nullptr;
    a->FirstDnsServerAddress = dns;

    a->Next = nullptr;
    *SizePointer = need;
    return ERROR_SUCCESS;
}

constexpr uint32_t AI_MAGIC = 0x4E535041u; /* 'NSPA' */

struct SpoofAiHdr {
    uint32_t magic;
    ADDRINFOA ai;
};

SpoofAiHdr *ai_hdr_from_ai(ADDRINFOA *ai) {
    SpoofAiHdr *h = nullptr;
    if (!ai) {
        return nullptr;
    }
    h = reinterpret_cast<SpoofAiHdr *>(
            reinterpret_cast<BYTE *>(ai) - offsetof(SpoofAiHdr, ai));
    if (h->magic != AI_MAGIC) {
        return nullptr;
    }
    return h;
}

/* Wine packed getaddrinfo: sockaddr lives immediately after addrinfo. */
bool ai_is_wine_packed(ADDRINFOA *ai) {
    BYTE *p = reinterpret_cast<BYTE *>(ai);
    BYTE *addr = nullptr;
    if (!ai || !ai->ai_addr) {
        return false;
    }
    addr = reinterpret_cast<BYTE *>(ai->ai_addr);
    return addr >= p + sizeof(*ai) &&
            addr < p + sizeof(*ai) + sizeof(SOCKADDR_STORAGE) + 64;
}

/*
 * Dual-safe free for Wine + native Windows:
 * - Our results: magic header + separate mallocs for addr/canon.
 * - Wine packed foreign: single free(ai).
 * - Else (native Windows foreign): free canon, addr, node per link.
 */
void WSAAPI freeaddrinfo_hook(PADDRINFOA ai) {
    while (ai) {
        ADDRINFOA *next = ai->ai_next;
        SpoofAiHdr *hdr = ai_hdr_from_ai(ai);
        if (hdr) {
            free(ai->ai_canonname);
            free(ai->ai_addr);
            free(hdr);
        } else if (ai_is_wine_packed(ai)) {
            free(ai);
            break;
        } else {
            free(ai->ai_canonname);
            free(ai->ai_addr);
            free(ai);
        }
        ai = next;
    }
}

INT WSAAPI getaddrinfo_hook(
        PCSTR pNodeName,
        PCSTR pServiceName,
        const ADDRINFOA *pHints,
        PADDRINFOA *ppResult) {
    SpoofAiHdr *hdr = nullptr;
    ADDRINFOA *ai = nullptr;
    sockaddr_in *sa = nullptr;
    char *canon = nullptr;
    const char *name_src = nullptr;
    size_t name_len = 0;
    uint32_t ip = 0;
    int port = 0;

    if (!ppResult) {
        return EAI_FAIL;
    }
    *ppResult = nullptr;

    if (pHints && pHints->ai_family == AF_INET6) {
        return EAI_FAMILY;
    }

    if (!pNodeName || !pNodeName[0] || name_is_local(pNodeName)) {
        if (pNodeName && (_stricmp(pNodeName, k_hostname) == 0 ||
                _stricmp(pNodeName, k_hostname_fqdn) == 0)) {
            ip = k_local_ip;
        } else {
            ip = 0x7F000001u;
        }
    } else if (name_is_numeric_ipv4(pNodeName)) {
        ip = parse_ipv4(pNodeName);
    } else {
        /* offline=1: map external names to local_ip */
        ip = k_local_ip;
    }

    if (pServiceName && pServiceName[0]) {
        port = atoi(pServiceName);
        if (port < 0 || port > 65535) {
            port = 0;
        }
    }

    name_src = (pNodeName && pNodeName[0]) ? pNodeName : "localhost";
    name_len = strlen(name_src) + 1;
    if (name_len > 256) {
        name_len = 256;
    }

    hdr = static_cast<SpoofAiHdr *>(calloc(1, sizeof(*hdr)));
    sa = static_cast<sockaddr_in *>(calloc(1, sizeof(*sa)));
    canon = static_cast<char *>(malloc(name_len));
    if (!hdr || !sa || !canon) {
        free(hdr);
        free(sa);
        free(canon);
        return EAI_MEMORY;
    }
    hdr->magic = AI_MAGIC;
    ai = &hdr->ai;
    memcpy(canon, name_src, name_len - 1);
    canon[name_len - 1] = 0;

    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(ip);
    if (port > 0) {
        sa->sin_port = htons(static_cast<u_short>(port));
    }

    ai->ai_family = AF_INET;
    ai->ai_socktype = pHints && pHints->ai_socktype ?
            pHints->ai_socktype : SOCK_STREAM;
    ai->ai_protocol = pHints && pHints->ai_protocol ?
            pHints->ai_protocol : IPPROTO_TCP;
    ai->ai_addrlen = sizeof(*sa);
    ai->ai_addr = reinterpret_cast<struct sockaddr *>(sa);
    ai->ai_canonname = canon;
    ai->ai_next = nullptr;
    *ppResult = ai;
    return 0;
}

void install_nicspoof_hooks() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    char ipstr[16];
    char maskstr[16];
    char gwstr[16];
    ip_to_str(k_local_ip, ipstr, sizeof(ipstr));
    ip_to_str(k_mask, maskstr, sizeof(maskstr));
    ip_to_str(k_gateway, gwstr, sizeof(gwstr));
    log_info("network",
            "NIC spoof enabled ip={} mask={}/{} gw={} "
            "mac={:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X} host={} ifindex={}",
            ipstr, maskstr, k_prefix_len, gwstr,
            static_cast<unsigned>(k_fake_mac[0]),
            static_cast<unsigned>(k_fake_mac[1]),
            static_cast<unsigned>(k_fake_mac[2]),
            static_cast<unsigned>(k_fake_mac[3]),
            static_cast<unsigned>(k_fake_mac[4]),
            static_cast<unsigned>(k_fake_mac[5]),
            k_hostname, static_cast<unsigned>(k_ifindex));

    bool ok = true;
    ok &= detour::trampoline_try(
            "iphlpapi.dll", "GetAdaptersAddresses",
            (void *) GetAdaptersAddresses_hook,
            (void **) &GetAdaptersAddresses_orig);
    ok &= detour::trampoline_try(
            "iphlpapi.dll", "GetAdaptersInfo",
            (void *) GetAdaptersInfo_hook,
            (void **) &GetAdaptersInfo_orig);
    ok &= detour::trampoline_try(
            "iphlpapi.dll", "GetNetworkParams",
            (void *) GetNetworkParams_hook,
            (void **) &GetNetworkParams_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "getaddrinfo",
            (void *) getaddrinfo_hook,
            (void **) &getaddrinfo_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "freeaddrinfo",
            (void *) freeaddrinfo_hook,
            (void **) &freeaddrinfo_orig);

    /* FreeAddrInfoA is often the same address; only hook if distinct. */
    {
        HMODULE ws = GetModuleHandleA("ws2_32.dll");
        void *faa = nullptr;
        void *fai = nullptr;
        if (ws) {
            faa = reinterpret_cast<void *>(GetProcAddress(ws, "FreeAddrInfoA"));
            fai = reinterpret_cast<void *>(GetProcAddress(ws, "freeaddrinfo"));
        }
        if (faa && faa != fai) {
            decltype(freeaddrinfo) *FreeAddrInfoA_orig = nullptr;
            if (!detour::trampoline_try(
                    "ws2_32.dll", "FreeAddrInfoA",
                    (void *) freeaddrinfo_hook,
                    (void **) &FreeAddrInfoA_orig)) {
                log_warning("network",
                        "NIC spoof: FreeAddrInfoA hook was not installed");
            }
        }
    }

    if (!ok) {
        log_warning("network",
                "NIC spoof: one or more hooks failed to install");
    } else {
        log_info("network",
                "NIC spoof hooks installed "
                "(GAA/GAI/GNP/getaddrinfo/freeaddrinfo)");
    }
}

} // namespace

void nicspoof_init() {
    install_nicspoof_hooks();
}
