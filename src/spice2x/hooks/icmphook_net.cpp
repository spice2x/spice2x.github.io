#include "icmphook_net.h"

#include "util/detour.h"
#include "util/logging.h"

#include <iphlpapi.h>
#include <mutex>
#include <deque>
#include <vector>
#include <chrono>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <unordered_map>

void icmphook_iphlpapi_install();

#ifndef SPICE64
using socket_uint = unsigned int;
#else
using socket_uint = unsigned long long;
#endif

namespace icmphook_internal {

constexpr uint32_t k_loopback_host = 0x7f000001u;

#pragma pack(push, 1)
struct IcmpHeaderView {
    uint8_t type;
    uint8_t code;
    uint16_t checksum_be;
};
#pragma pack(pop)

bool icmp_packet_valid(const uint8_t *icmp, size_t len) {
    if (len < sizeof(IcmpHeaderView)) {
        return false;
    }
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (static_cast<unsigned>(icmp[i]) << 8) | icmp[i + 1];
        sum = (sum >> 16) + (sum & 0xFFFF);
    }
    if (len & 1) {
        sum += static_cast<unsigned>(icmp[len - 1]) << 8;
        sum = (sum >> 16) + (sum & 0xFFFF);
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    return sum == 0xFFFFu || sum == 0;
}

bool icmp_build_echo_reply(std::vector<uint8_t> &out, const uint8_t *req, size_t req_len) {
    if (req_len < sizeof(IcmpHeaderView)) {
        return false;
    }
    out.resize(req_len);
    memcpy(out.data(), req, req_len);
    auto *hdr = reinterpret_cast<IcmpHeaderView *>(out.data());
    hdr->type = 0;
    hdr->code = 0;
    hdr->checksum_be = 0;
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < out.size(); i += 2) {
        sum += (static_cast<unsigned>(out[i]) << 8) | out[i + 1];
        sum = (sum >> 16) + (sum & 0xFFFF);
    }
    if (out.size() & 1) {
        sum += static_cast<unsigned>(out[out.size() - 1]) << 8;
        sum = (sum >> 16) + (sum & 0xFFFF);
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    uint16_t csum = static_cast<uint16_t>(~sum);
    hdr->checksum_be = htons(csum);
    return true;
}

bool icmp_fix_pointer(
        const uint8_t *base,
        size_t total_len,
        const uint8_t **icmp_start,
        size_t *icmp_len) {
    if (total_len >= 20 && (base[0] >> 4) == 4) {
        size_t ihl = (base[0] & 0x0Fu) * 4;
        if (ihl < 20 || ihl > total_len || base[9] != IPPROTO_ICMP) {
            return false;
        }
        *icmp_start = base + ihl;
        *icmp_len = total_len - ihl;
        return true;
    }
    *icmp_start = base;
    *icmp_len = total_len;
    return true;
}

bool ipv4_encode_datagram(
        std::vector<uint8_t> &out,
        uint32_t src_host_order,
        uint32_t dst_host_order,
        const uint8_t *payload,
        size_t payload_len) {
    const size_t hdr_len = 20;
    if (payload_len > 0xFFFFu - hdr_len) {
        return false;
    }
    const auto total_len = static_cast<uint16_t>(hdr_len + payload_len);
    out.resize(hdr_len + payload_len);
    uint8_t *p = out.data();
    memset(p, 0, hdr_len);
    p[0] = 0x45;
    p[1] = 0;
    p[2] = static_cast<uint8_t>(total_len >> 8);
    p[3] = static_cast<uint8_t>(total_len & 0xFF);
    p[6] = 0x40;
    p[8] = 1;
    p[9] = IPPROTO_ICMP;
    uint32_t src_wire = htonl(src_host_order);
    uint32_t dst_wire = htonl(dst_host_order);
    memcpy(p + 12, &src_wire, 4);
    memcpy(p + 16, &dst_wire, 4);
    unsigned sum = 0;
    for (size_t i = 0; i < hdr_len; i += 2) {
        sum += (static_cast<unsigned>(p[i]) << 8) | p[i + 1];
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    }
    uint16_t hdr_csum = static_cast<uint16_t>(~sum);
    p[10] = static_cast<uint8_t>(hdr_csum >> 8);
    p[11] = static_cast<uint8_t>(hdr_csum & 0xFF);
    memcpy(p + hdr_len, payload, payload_len);
    return true;
}

uint32_t local_ipv4_for_peer(
        uint32_t peer_host_order,
        uint32_t bind_pref_host_order,
        bool has_bind) {
    if (peer_host_order == k_loopback_host) {
        return k_loopback_host;
    }
    if (has_bind && bind_pref_host_order != 0) {
        return bind_pref_host_order;
    }
    ULONG buf_len = 0;
    GetAdaptersInfo(nullptr, &buf_len);
    if (buf_len == 0) {
        return peer_host_order;
    }
    std::vector<uint8_t> buf(buf_len);
    auto *info = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
    if (GetAdaptersInfo(info, &buf_len) != ERROR_SUCCESS) {
        return peer_host_order;
    }
    for (PIP_ADAPTER_INFO a = info; a != nullptr; a = a->Next) {
        uint32_t ip = ntohl(inet_addr(a->IpAddressList.IpAddress.String));
        uint32_t mask = ntohl(inet_addr(a->IpAddressList.IpMask.String));
        if (mask == 0) {
            continue;
        }
        if ((peer_host_order & mask) == (ip & mask)) {
            return ip;
        }
    }
    return peer_host_order;
}

struct EmuSock {
    std::deque<std::vector<uint8_t>> queue;
    std::condition_variable_any cv;
    DWORD rx_timeout_ms = INFINITE;
    bool nonblocking = false;
    bool has_bind = false;
    uint32_t bind_ip_host = 0;
};

std::recursive_mutex g_mu;
std::unordered_map<SOCKET, std::unique_ptr<EmuSock>> g_socks;
std::atomic<uint32_t> g_seq{1};

// Fast-path flag so the bind() hook in networkhook.cpp can short-circuit
// without taking g_mu when the ICMP feature was never enabled.
std::atomic<bool> g_installed{false};

decltype(socket) *socket_orig = nullptr;
decltype(WSASocketW) *WSASocketW_orig = nullptr;
decltype(WSASocketA) *WSASocketA_orig = nullptr;
decltype(closesocket) *closesocket_orig = nullptr;
decltype(bind) *bind_trampoline_orig = nullptr;
decltype(sendto) *sendto_orig = nullptr;
decltype(recvfrom) *recvfrom_orig = nullptr;
decltype(WSASendTo) *WSASendTo_orig = nullptr;
decltype(WSARecvFrom) *WSARecvFrom_orig = nullptr;
decltype(ioctlsocket) *ioctlsocket_orig = nullptr;
decltype(setsockopt) *setsockopt_orig = nullptr;

EmuSock *lookup(SOCKET s) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        return nullptr;
    }
    return it->second.get();
}

SOCKET alloc_icmp_socket() {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    for (;;) {
        uint32_t x = g_seq.fetch_add(1, std::memory_order_relaxed);
        SOCKET s = (SOCKET)(socket_uint)(0xE0000000u | (x & 0x0FFFFFFFu));
        if (s == INVALID_SOCKET) {
            continue;
        }
        if (g_socks.find(s) == g_socks.end()) {
            g_socks[s] = std::make_unique<EmuSock>();
            return s;
        }
    }
}

void release_icmp_socket(SOCKET s) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto it = g_socks.find(s);
    if (it != g_socks.end()) {
        it->second->cv.notify_all();
        g_socks.erase(it);
    }
}

bool process_icmp_send(SOCKET s, const uint8_t *buf, int len, const sockaddr *to, int tolen) {
    if (len <= 0 || tolen < (int) sizeof(sockaddr_in) || !buf || !to) {
        return true;
    }
    auto *sin = reinterpret_cast<const sockaddr_in *>(to);
    if (sin->sin_family != AF_INET) {
        return true;
    }
    uint32_t peer_host = ntohl(sin->sin_addr.s_addr);

    const uint8_t *icmp_ptr = nullptr;
    size_t icmp_len = 0;
    const auto *raw = reinterpret_cast<const uint8_t *>(buf);
    if (!icmp_fix_pointer(raw, static_cast<size_t>(len), &icmp_ptr, &icmp_len)) {
        return true;
    }
    if (!icmp_packet_valid(icmp_ptr, icmp_len)) {
        return true;
    }
    if (icmp_len < sizeof(IcmpHeaderView)) {
        return true;
    }
    const auto *ih = reinterpret_cast<const IcmpHeaderView *>(icmp_ptr);
    if (ih->type != 8 || ih->code != 0) {
        return true;
    }

    std::vector<uint8_t> icmp_reply;
    if (!icmp_build_echo_reply(icmp_reply, icmp_ptr, icmp_len)) {
        return true;
    }

    std::vector<uint8_t> packet;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mu);
        auto it = g_socks.find(s);
        if (it == g_socks.end()) {
            return true;
        }
        EmuSock *es = it->second.get();
        uint32_t local_host = local_ipv4_for_peer(peer_host, es->bind_ip_host, es->has_bind);
        if (!ipv4_encode_datagram(
                packet, peer_host, local_host, icmp_reply.data(), icmp_reply.size())) {
            return true;
        }
        es->queue.push_back(std::move(packet));
        es->cv.notify_all();
    }
    return true;
}

SOCKET WINAPI socket_hook(int af, int type, int protocol) {
    if (af == AF_INET && type == SOCK_RAW && protocol == IPPROTO_ICMP) {
        SOCKET s = alloc_icmp_socket();
        log_info("network", "ICMP emulation: allocated raw socket {}", (socket_uint) s);
        return s;
    }
    return socket_orig(af, type, protocol);
}

SOCKET WINAPI WSASocketW_hook(
        int af,
        int type,
        int protocol,
        LPWSAPROTOCOL_INFOW lpProtocolInfo,
        GROUP g,
        DWORD dwFlags) {
    const bool is_raw_icmp =
            af == AF_INET && type == SOCK_RAW && protocol == IPPROTO_ICMP;
    if (lpProtocolInfo == nullptr && is_raw_icmp) {
        SOCKET s = alloc_icmp_socket();
        log_info("network", "ICMP emulation: allocated raw WSASocketW {}", (socket_uint) s);
        return s;
    }
    return WSASocketW_orig(af, type, protocol, lpProtocolInfo, g, dwFlags);
}

SOCKET WINAPI WSASocketA_hook(
        int af,
        int type,
        int protocol,
        LPWSAPROTOCOL_INFOA lpProtocolInfo,
        GROUP g,
        DWORD dwFlags) {
    const bool is_raw_icmp =
            af == AF_INET && type == SOCK_RAW && protocol == IPPROTO_ICMP;
    if (lpProtocolInfo == nullptr && is_raw_icmp) {
        SOCKET s = alloc_icmp_socket();
        log_info("network", "ICMP emulation: allocated raw WSASocketA {}", (socket_uint) s);
        return s;
    }
    return WSASocketA_orig(af, type, protocol, lpProtocolInfo, g, dwFlags);
}

int WINAPI closesocket_hook(SOCKET s) {
    if (lookup(s)) {
        release_icmp_socket(s);
        return 0;
    }
    return closesocket_orig(s);
}

int WINAPI bind_hook_ws2(SOCKET s, const sockaddr *name, int namelen) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_mu);
        auto it = g_socks.find(s);
        if (it != g_socks.end()) {
            if (namelen < (int) sizeof(sockaddr_in)) {
                WSASetLastError(WSAEINVAL);
                return SOCKET_ERROR;
            }
            auto *in = reinterpret_cast<const sockaddr_in *>(name);
            if (in->sin_family != AF_INET) {
                WSASetLastError(WSAEAFNOSUPPORT);
                return SOCKET_ERROR;
            }
            it->second->has_bind = true;
            it->second->bind_ip_host = ntohl(in->sin_addr.s_addr);
            return 0;
        }
    }
    return bind_trampoline_orig(s, name, namelen);
}

int WINAPI sendto_hook(
        SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int tolen) {
    EmuSock *es = lookup(s);
    if (!es) {
        return sendto_orig(s, buf, len, flags, to, tolen);
    }
    if (!buf || !to) {
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    process_icmp_send(s, reinterpret_cast<const uint8_t *>(buf), len, to, tolen);
    return len;
}

int WINAPI WSASendTo_hook(
        SOCKET s,
        LPWSABUF lpBuffers,
        DWORD dwBufferCount,
        LPDWORD lpNumberOfBytesSent,
        DWORD dwFlags,
        const sockaddr *lpTo,
        int iTolen,
        LPWSAOVERLAPPED lpOverlapped,
        LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
    EmuSock *es = lookup(s);
    if (!es) {
        return WSASendTo_orig(
                s,
                lpBuffers,
                dwBufferCount,
                lpNumberOfBytesSent,
                dwFlags,
                lpTo,
                iTolen,
                lpOverlapped,
                lpCompletionRoutine);
    }
    if (lpOverlapped != nullptr) {
        WSASetLastError(WSAEOPNOTSUPP);
        return SOCKET_ERROR;
    }
    if (dwBufferCount != 1 || !lpBuffers || !lpNumberOfBytesSent) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    int len = (int) lpBuffers[0].len;
    process_icmp_send(
            s,
            reinterpret_cast<const uint8_t *>(lpBuffers[0].buf),
            len,
            lpTo,
            iTolen);
    *lpNumberOfBytesSent = static_cast<DWORD>(len);
    return 0;
}

int WINAPI recvfrom_hook(
        SOCKET s, char *buf, int len, int flags, sockaddr *from, int *fromlen) {
    if (!buf || len <= 0) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    std::unique_lock<std::recursive_mutex> lock(g_mu);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) {
        lock.unlock();
        return recvfrom_orig(s, buf, len, flags, from, fromlen);
    }
    EmuSock *es = it->second.get();
    auto wait_pred = [&] { return !es->queue.empty(); };

    if (es->nonblocking) {
        if (es->queue.empty()) {
            WSASetLastError(WSAEWOULDBLOCK);
            return SOCKET_ERROR;
        }
    } else if (es->rx_timeout_ms == 0) {
        if (es->queue.empty()) {
            WSASetLastError(WSAETIMEDOUT);
            return SOCKET_ERROR;
        }
    } else if (es->rx_timeout_ms == INFINITE) {
        es->cv.wait(lock, wait_pred);
    } else {
        const auto timeout = std::chrono::milliseconds(es->rx_timeout_ms);
        if (!es->cv.wait_for(lock, timeout, wait_pred)) {
            WSASetLastError(WSAETIMEDOUT);
            return SOCKET_ERROR;
        }
    }

    std::vector<uint8_t> pkt = std::move(es->queue.front());
    es->queue.pop_front();
    lock.unlock();

    const int take = (len < (int) pkt.size()) ? len : (int) pkt.size();
    memcpy(buf, pkt.data(), static_cast<size_t>(take));
    if (from && fromlen && *fromlen >= (int) sizeof(sockaddr_in)) {
        auto *out = reinterpret_cast<sockaddr_in *>(from);
        memset(out, 0, sizeof(*out));
        out->sin_family = AF_INET;
        if (pkt.size() >= 20) {
            memcpy(&out->sin_addr, pkt.data() + 12, 4);
        } else {
            out->sin_addr.s_addr = htonl(k_loopback_host);
        }
        *fromlen = sizeof(sockaddr_in);
    }
    return take;
}

int WINAPI WSARecvFrom_hook(
        SOCKET s,
        LPWSABUF lpBuffers,
        DWORD dwBufferCount,
        LPDWORD lpNumberOfBytesRecvd,
        LPDWORD lpFlags,
        sockaddr *lpFrom,
        LPINT lpFromlen,
        LPWSAOVERLAPPED lpOverlapped,
        LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
    EmuSock *es = lookup(s);
    if (!es) {
        return WSARecvFrom_orig(
                s,
                lpBuffers,
                dwBufferCount,
                lpNumberOfBytesRecvd,
                lpFlags,
                lpFrom,
                lpFromlen,
                lpOverlapped,
                lpCompletionRoutine);
    }
    if (lpOverlapped != nullptr) {
        WSASetLastError(WSAEOPNOTSUPP);
        return SOCKET_ERROR;
    }
    if (dwBufferCount != 1 || !lpBuffers || !lpNumberOfBytesRecvd) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    char stack_buf[65536];
    sockaddr_in from_tmp{};
    int fromlen_tmp = sizeof(from_tmp);
    int r = recvfrom_hook(
            s,
            stack_buf,
            sizeof(stack_buf),
            0,
            reinterpret_cast<sockaddr *>(&from_tmp),
            &fromlen_tmp);
    if (r == SOCKET_ERROR) {
        return SOCKET_ERROR;
    }
    int max_copy = (int) lpBuffers[0].len;
    if (r > max_copy) {
        WSASetLastError(WSAEMSGSIZE);
        return SOCKET_ERROR;
    }
    memcpy(lpBuffers[0].buf, stack_buf, static_cast<size_t>(r));
    *lpNumberOfBytesRecvd = static_cast<DWORD>(r);
    if (lpFlags) {
        *lpFlags = 0;
    }
    if (lpFrom && lpFromlen && *lpFromlen >= (int) sizeof(sockaddr_in)) {
        *reinterpret_cast<sockaddr_in *>(lpFrom) = from_tmp;
        *lpFromlen = sizeof(sockaddr_in);
    }
    return 0;
}

int WINAPI ioctlsocket_hook(SOCKET s, long cmd, u_long *argp) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_mu);
        auto it = g_socks.find(s);
        if (it != g_socks.end()) {
            EmuSock *es = it->second.get();
            if (argp && static_cast<u_long>(cmd) == FIONBIO) {
                es->nonblocking = (*argp != 0);
                return 0;
            }
            WSASetLastError(WSAEOPNOTSUPP);
            return SOCKET_ERROR;
        }
    }
    return ioctlsocket_orig(s, cmd, argp);
}

int WINAPI setsockopt_hook(
        SOCKET s, int level, int optname, const char *optval, int optlen) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_mu);
        auto it = g_socks.find(s);
        if (it != g_socks.end()) {
            EmuSock *es = it->second.get();
            if (level == SOL_SOCKET && optlen >= (int) sizeof(DWORD)) {
                auto v = reinterpret_cast<const DWORD *>(optval);
                if (optname == SO_RCVTIMEO) {
                    es->rx_timeout_ms = *v;
                    return 0;
                }
                if (optname == SO_SNDTIMEO) {
                    return 0;
                }
            }
            WSASetLastError(WSAEOPNOTSUPP);
            return SOCKET_ERROR;
        }
    }
    return setsockopt_orig(s, level, optname, optval, optlen);
}

void install_icmphook_hooks() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    bool ok = true;
    ok &= detour::trampoline_try(
            "ws2_32.dll", "socket",
            (void *) socket_hook, (void **) &socket_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "WSASocketW",
            (void *) WSASocketW_hook, (void **) &WSASocketW_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "WSASocketA",
            (void *) WSASocketA_hook, (void **) &WSASocketA_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "closesocket",
            (void *) closesocket_hook, (void **) &closesocket_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "bind",
            (void *) bind_hook_ws2, (void **) &bind_trampoline_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "sendto",
            (void *) sendto_hook, (void **) &sendto_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "recvfrom",
            (void *) recvfrom_hook, (void **) &recvfrom_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "WSASendTo",
            (void *) WSASendTo_hook, (void **) &WSASendTo_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "WSARecvFrom",
            (void *) WSARecvFrom_hook, (void **) &WSARecvFrom_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "ioctlsocket",
            (void *) ioctlsocket_hook, (void **) &ioctlsocket_orig);
    ok &= detour::trampoline_try(
            "ws2_32.dll", "setsockopt",
            (void *) setsockopt_hook, (void **) &setsockopt_orig);

    if (!ok) {
        log_warning(
                "network",
                "ICMP emulation: one or more ws2_32 hooks failed to install");
    } else {
        log_info("network", "ICMP emulation hooks installed (raw ICMP sockets)");
    }

    g_installed.store(true, std::memory_order_release);
}

} // namespace icmphook_internal

bool icmphook_is_emulated_socket(SOCKET s) {
    if (!icmphook_internal::g_installed.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(icmphook_internal::g_mu);
    return icmphook_internal::g_socks.find(s) != icmphook_internal::g_socks.end();
}

bool icmphook_try_bind(SOCKET s, const struct sockaddr *name, int namelen, int *out_result) {
    if (!icmphook_internal::g_installed.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(icmphook_internal::g_mu);
    if (icmphook_internal::g_socks.find(s) == icmphook_internal::g_socks.end()) {
        return false;
    }
    *out_result = icmphook_internal::bind_hook_ws2(s, name, namelen);
    return true;
}

void icmphook_net_init() {
    icmphook_internal::install_icmphook_hooks();
    icmphook_iphlpapi_install();
}
