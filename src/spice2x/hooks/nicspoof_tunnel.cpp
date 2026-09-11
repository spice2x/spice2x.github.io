#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WIN32_WINNT 0x0601

#include "nicspoof_tunnel.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util/detour.h"
#include "util/logging.h"

namespace nicspoof_tunnel_detail {

constexpr uint8_t MAGIC0 = 'B';
constexpr uint8_t MAGIC1 = 'P';
constexpr uint8_t MAGIC2 = 'L';
constexpr uint8_t MAGIC3 = '1';
constexpr uint8_t MSG_REGISTER = 1;
constexpr uint8_t MSG_REGISTER_ACK = 2;
constexpr uint8_t MSG_KEEPALIVE = 3;
constexpr uint8_t MSG_DATA = 4;
constexpr uint8_t ACK_OK = 0;
constexpr uint8_t KIND_UDP = 1;
constexpr uint8_t KIND_TCP_SYN = 2;
constexpr uint8_t KIND_TCP_ACCEPTED = 3;
constexpr uint8_t KIND_TCP_DATA = 4;
constexpr uint8_t KIND_TCP_FIN = 5;

constexpr int MAX_UDP_Q = 64;
constexpr int MAX_UDP_PAYLOAD = 8192;
constexpr int MAX_TCP = 32;
constexpr int MAX_LISTEN = 8;
constexpr int MAX_ACCEPT_Q = 8;
constexpr int MAX_UDP_SOCK = 32;
constexpr int KEEP_ALIVE_SEC = 5;

bool g_is_hub = false;
uint16_t g_tunnel_port = 51820;
char g_hub[256] = {};
uint8_t g_password[32] = {};
uint32_t g_local_ip = 0;
uint32_t g_peer_ip = 0;
uint32_t g_subnet = 0;
uint32_t g_mask = 0;

SOCKET g_tun_sock = INVALID_SOCKET;
sockaddr_in g_peer_addr = {};
int g_peer_valid = 0;
volatile LONG g_registered = 0;
volatile LONG g_shutdown = 0;
HANDLE g_tunnel_th = nullptr;
HANDLE g_keepalive_th = nullptr;
CRITICAL_SECTION g_peer_cs;

bool g_logged_overlay_listen = false;

decltype(sendto) *sendto_orig = nullptr;
decltype(recvfrom) *recvfrom_orig = nullptr;
decltype(bind) *bind_orig = nullptr;
decltype(connect) *connect_orig = nullptr;
decltype(WSAConnect) *WSAConnect_orig = nullptr;
decltype(listen) *listen_orig = nullptr;
decltype(accept) *accept_orig = nullptr;
decltype(closesocket) *closesocket_orig = nullptr;
decltype(ioctlsocket) *ioctlsocket_orig = nullptr;
decltype(send) *send_orig = nullptr;
decltype(recv) *recv_orig = nullptr;
decltype(getsockname) *getsockname_orig = nullptr;
decltype(select) *select_orig = nullptr;
decltype(WSASendTo) *WSASendTo_orig = nullptr;
decltype(WSARecvFrom) *WSARecvFrom_orig = nullptr;

struct UdpDgram {
    uint32_t from_ip;
    uint16_t from_port;
    int32_t len;
    uint8_t data[MAX_UDP_PAYLOAD];
};

struct UdpSock {
    SOCKET s;
    int32_t in_use;
    int32_t closing;
    int32_t bound_port;
    CRITICAL_SECTION cs;
    HANDLE event;
    int32_t q_head;
    int32_t q_tail;
    int32_t q_count;
    UdpDgram q[MAX_UDP_Q];
};

UdpSock g_udp[MAX_UDP_SOCK];
CRITICAL_SECTION g_udp_cs;

struct TcpConn {
    int32_t in_use;
    uint32_t conn_id;
    SOCKET game_sock;
    SOCKET relay_sock;
    HANDLE thread;
    volatile LONG alive;
};

struct AcceptItem {
    SOCKET client_sock;
    sockaddr_in peer;
};

struct ListenState {
    SOCKET listen_sock;
    int32_t in_use;
    int32_t closing;
    uint16_t port;
    int32_t nonblock;
    CRITICAL_SECTION cs;
    HANDLE event;
    int32_t q_count;
    AcceptItem q[MAX_ACCEPT_Q];
};

TcpConn g_tcp[MAX_TCP];
ListenState g_listen[MAX_LISTEN];
CRITICAL_SECTION g_tcp_cs;
CRITICAL_SECTION g_conn_wait_cs;

struct ConnWait {
    uint32_t id;
    HANDLE ev;
    int32_t used;
};

ConnWait g_conn_wait[MAX_TCP];
volatile LONG g_next_conn_id = 1;

struct NbState {
    SOCKET s;
    int32_t in_use;
    int32_t nonblock;
};

NbState g_nb[64];
CRITICAL_SECTION g_nb_cs;

void ip_to_str(uint32_t ip, char *out, size_t n) {
    if (!out || n == 0) {
        return;
    }
    snprintf(out, n, "%u.%u.%u.%u",
            (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    out[n - 1] = 0;
}

bool in_overlay(uint32_t ip) {
    return (ip & g_mask) == (g_subnet & g_mask);
}

bool is_bcast(uint32_t ip) {
    return ip == 0xFFFFFFFFu || ip == (g_subnet | ~g_mask);
}

uint16_t rd16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t rd32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
            (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
}

void wr16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void wr32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void peer_set(const sockaddr_in *addr, int32_t valid, uint32_t peer_ip) {
    EnterCriticalSection(&g_peer_cs);
    if (addr) {
        g_peer_addr = *addr;
    }
    g_peer_valid = valid;
    if (peer_ip) {
        g_peer_ip = peer_ip;
    }
    LeaveCriticalSection(&g_peer_cs);
}

int32_t peer_get(sockaddr_in *addr, uint32_t *peer_ip) {
    int32_t v;
    EnterCriticalSection(&g_peer_cs);
    v = g_peer_valid;
    if (addr) {
        *addr = g_peer_addr;
    }
    if (peer_ip) {
        *peer_ip = g_peer_ip;
    }
    LeaveCriticalSection(&g_peer_cs);
    return v;
}

UdpSock *udp_find(SOCKET s) {
    for (int32_t i = 0; i < MAX_UDP_SOCK; i++) {
        if (g_udp[i].in_use && !g_udp[i].closing && g_udp[i].s == s) {
            return &g_udp[i];
        }
    }
    return nullptr;
}

UdpSock *udp_add(SOCKET s) {
    UdpSock *st;
    HANDLE ev;

    EnterCriticalSection(&g_udp_cs);
    st = udp_find(s);
    if (!st) {
        for (int32_t i = 0; i < MAX_UDP_SOCK; i++) {
            if (!g_udp[i].in_use) {
                memset(&g_udp[i], 0, sizeof(g_udp[i]));
                ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!ev) {
                    LeaveCriticalSection(&g_udp_cs);
                    return nullptr;
                }
                g_udp[i].s = s;
                g_udp[i].in_use = 1;
                InitializeCriticalSection(&g_udp[i].cs);
                g_udp[i].event = ev;
                st = &g_udp[i];
                break;
            }
        }
    }
    LeaveCriticalSection(&g_udp_cs);
    return st;
}

void udp_remove(SOCKET s) {
    UdpSock *st;
    HANDLE ev = nullptr;

    EnterCriticalSection(&g_udp_cs);
    st = udp_find(s);
    if (st) {
        st->closing = 1;
        ev = st->event;
        if (ev) {
            SetEvent(ev);
        }
    }
    LeaveCriticalSection(&g_udp_cs);
    if (!st) {
        return;
    }
    EnterCriticalSection(&st->cs);
    LeaveCriticalSection(&st->cs);
    EnterCriticalSection(&g_udp_cs);
    if (st->in_use && st->s == s) {
        if (st->event) {
            CloseHandle(st->event);
        }
        DeleteCriticalSection(&st->cs);
        st->in_use = 0;
        st->closing = 0;
    }
    LeaveCriticalSection(&g_udp_cs);
}

void sock_signal_read(HANDLE ev) {
    if (ev) {
        SetEvent(ev);
    }
}

void udp_enqueue_sock(UdpSock *st, uint32_t from_ip, uint16_t from_port,
        const uint8_t *data, int32_t len) {
    if (!st || st->closing || len < 0 || len > MAX_UDP_PAYLOAD) {
        return;
    }
    EnterCriticalSection(&st->cs);
    if (st->q_count < MAX_UDP_Q) {
        UdpDgram *d = &st->q[st->q_tail];
        d->from_ip = from_ip;
        d->from_port = from_port;
        d->len = len;
        memcpy(d->data, data, static_cast<size_t>(len));
        st->q_tail = (st->q_tail + 1) % MAX_UDP_Q;
        st->q_count++;
        sock_signal_read(st->event);
    }
    LeaveCriticalSection(&st->cs);
}

void udp_enqueue(uint16_t dport, uint32_t from_ip, uint16_t from_port,
        const uint8_t *data, int32_t len) {
    int32_t matched = 0;

    if (len < 0 || len > MAX_UDP_PAYLOAD) {
        return;
    }
    EnterCriticalSection(&g_udp_cs);
    for (int32_t i = 0; i < MAX_UDP_SOCK; i++) {
        UdpSock *st = &g_udp[i];
        if (!st->in_use || st->closing) {
            continue;
        }
        if (st->bound_port == static_cast<int32_t>(dport)) {
            udp_enqueue_sock(st, from_ip, from_port, data, len);
            matched = 1;
        }
    }
    if (!matched) {
        for (int32_t i = 0; i < MAX_UDP_SOCK; i++) {
            UdpSock *st = &g_udp[i];
            if (!st->in_use || st->closing) {
                continue;
            }
            if (!st->bound_port) {
                udp_enqueue_sock(st, from_ip, from_port, data, len);
                matched = 1;
            }
        }
    }
    LeaveCriticalSection(&g_udp_cs);
    (void) matched;
}

ListenState *listen_find(SOCKET s) {
    for (int32_t i = 0; i < MAX_LISTEN; i++) {
        if (g_listen[i].in_use && !g_listen[i].closing &&
                g_listen[i].listen_sock == s) {
            return &g_listen[i];
        }
    }
    return nullptr;
}

ListenState *listen_find_port(uint16_t port) {
    for (int32_t i = 0; i < MAX_LISTEN; i++) {
        if (g_listen[i].in_use && !g_listen[i].closing &&
                g_listen[i].port == port) {
            return &g_listen[i];
        }
    }
    return nullptr;
}

void nb_set(SOCKET s, int32_t nonblock) {
    EnterCriticalSection(&g_nb_cs);
    for (int32_t i = 0; i < 64; i++) {
        if (g_nb[i].in_use && g_nb[i].s == s) {
            g_nb[i].nonblock = nonblock;
            LeaveCriticalSection(&g_nb_cs);
            return;
        }
    }
    for (int32_t i = 0; i < 64; i++) {
        if (!g_nb[i].in_use) {
            g_nb[i].in_use = 1;
            g_nb[i].s = s;
            g_nb[i].nonblock = nonblock;
            break;
        }
    }
    LeaveCriticalSection(&g_nb_cs);
}

int32_t nb_get(SOCKET s) {
    int32_t v = 0;
    EnterCriticalSection(&g_nb_cs);
    for (int32_t i = 0; i < 64; i++) {
        if (g_nb[i].in_use && g_nb[i].s == s) {
            v = g_nb[i].nonblock;
            break;
        }
    }
    LeaveCriticalSection(&g_nb_cs);
    return v;
}

void nb_clear(SOCKET s) {
    EnterCriticalSection(&g_nb_cs);
    for (int32_t i = 0; i < 64; i++) {
        if (g_nb[i].in_use && g_nb[i].s == s) {
            g_nb[i].in_use = 0;
        }
    }
    LeaveCriticalSection(&g_nb_cs);
}

int32_t sock_overlay_readable(SOCKET s) {
    UdpSock *us;
    ListenState *ls;
    int32_t n = 0;

    EnterCriticalSection(&g_udp_cs);
    us = udp_find(s);
    if (us) {
        EnterCriticalSection(&us->cs);
        n = us->q_count;
        LeaveCriticalSection(&us->cs);
        LeaveCriticalSection(&g_udp_cs);
        return n > 0;
    }
    LeaveCriticalSection(&g_udp_cs);

    EnterCriticalSection(&g_tcp_cs);
    ls = listen_find(s);
    if (ls) {
        EnterCriticalSection(&ls->cs);
        n = ls->q_count;
        LeaveCriticalSection(&ls->cs);
        LeaveCriticalSection(&g_tcp_cs);
        return n > 0;
    }
    LeaveCriticalSection(&g_tcp_cs);
    return 0;
}

int32_t fd_set_has_overlay(const fd_set *set) {
    if (!set) {
        return 0;
    }
    for (u_int i = 0; i < set->fd_count; i++) {
        if (sock_overlay_readable(set->fd_array[i])) {
            return 1;
        }
    }
    return 0;
}

int32_t fd_set_contains_tracked(const fd_set *set) {
    if (!set) {
        return 0;
    }
    EnterCriticalSection(&g_udp_cs);
    EnterCriticalSection(&g_tcp_cs);
    for (u_int i = 0; i < set->fd_count; i++) {
        SOCKET s = set->fd_array[i];
        if (udp_find(s) || listen_find(s)) {
            LeaveCriticalSection(&g_tcp_cs);
            LeaveCriticalSection(&g_udp_cs);
            return 1;
        }
    }
    LeaveCriticalSection(&g_tcp_cs);
    LeaveCriticalSection(&g_udp_cs);
    return 0;
}

int32_t merge_overlay_into_fdset(fd_set *set, const fd_set *orig) {
    int32_t added = 0;

    if (!set || !orig) {
        return 0;
    }
    for (u_int i = 0; i < orig->fd_count; i++) {
        SOCKET s = orig->fd_array[i];
        if (sock_overlay_readable(s) && !FD_ISSET(s, set)) {
            if (set->fd_count >= FD_SETSIZE) {
                break;
            }
            FD_SET(s, set);
            added++;
        }
    }
    return added;
}

int32_t tun_sendto_raw(SOCKET s, const char *buf, int32_t n,
        const sockaddr_in *to) {
    if (sendto_orig) {
        return sendto_orig(s, buf, n, 0,
                reinterpret_cast<const sockaddr *>(to), sizeof(*to));
    }
    return sendto(s, buf, n, 0,
            reinterpret_cast<const sockaddr *>(to), sizeof(*to));
}

int32_t tun_recvfrom_raw(SOCKET s, char *buf, int32_t n, sockaddr_in *from,
        int32_t *flen) {
    if (recvfrom_orig) {
        return recvfrom_orig(s, buf, n, 0,
                reinterpret_cast<sockaddr *>(from), flen);
    }
    return recvfrom(s, buf, n, 0, reinterpret_cast<sockaddr *>(from), flen);
}

int32_t tun_bind_raw(SOCKET s, const sockaddr *name, int32_t namelen) {
    if (bind_orig) {
        return bind_orig(s, name, namelen);
    }
    return bind(s, name, namelen);
}

int32_t tun_send(uint8_t type, const uint8_t *payload, int32_t plen) {
    uint8_t buf[65536];
    int32_t n;
    sockaddr_in peer;
    int32_t valid;

    if (g_tun_sock == INVALID_SOCKET) {
        return -1;
    }
    valid = peer_get(&peer, nullptr);
    if (!valid) {
        return -1;
    }
    if (5 + plen > static_cast<int32_t>(sizeof(buf))) {
        return -1;
    }
    buf[0] = MAGIC0;
    buf[1] = MAGIC1;
    buf[2] = MAGIC2;
    buf[3] = MAGIC3;
    buf[4] = type;
    if (plen > 0) {
        memcpy(buf + 5, payload, static_cast<size_t>(plen));
    }
    n = 5 + plen;
    return tun_sendto_raw(g_tun_sock, reinterpret_cast<char *>(buf), n, &peer);
}

void tun_send_udp(uint16_t sport, uint16_t dport, const uint8_t *data,
        int32_t len) {
    uint8_t buf[MAX_UDP_PAYLOAD + 8];

    if (len > MAX_UDP_PAYLOAD) {
        return;
    }
    buf[0] = KIND_UDP;
    wr16(buf + 1, sport);
    wr16(buf + 3, dport);
    memcpy(buf + 5, data, static_cast<size_t>(len));
    tun_send(MSG_DATA, buf, 5 + len);
}

void tun_send_tcp_hdr(uint8_t kind, uint32_t conn_id, const uint8_t *extra,
        int32_t elen) {
    uint8_t buf[MAX_UDP_PAYLOAD + 16];
    int32_t n = 5;

    buf[0] = kind;
    wr32(buf + 1, conn_id);
    if (extra && elen > 0) {
        if (elen > MAX_UDP_PAYLOAD) {
            elen = MAX_UDP_PAYLOAD;
        }
        memcpy(buf + 5, extra, static_cast<size_t>(elen));
        n = 5 + elen;
    }
    tun_send(MSG_DATA, buf, n);
}

void tun_send_tcp_syn(uint32_t conn_id, uint16_t dport, uint16_t sport) {
    uint8_t buf[9];

    buf[0] = KIND_TCP_SYN;
    wr32(buf + 1, conn_id);
    wr16(buf + 5, dport);
    wr16(buf + 7, sport);
    tun_send(MSG_DATA, buf, 9);
}

int32_t conn_wait_reg(uint32_t id, HANDLE ev) {
    int32_t ok = 0;

    EnterCriticalSection(&g_conn_wait_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (!g_conn_wait[i].used) {
            g_conn_wait[i].used = 1;
            g_conn_wait[i].id = id;
            g_conn_wait[i].ev = ev;
            ok = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_conn_wait_cs);
    return ok;
}

void conn_wait_clear(uint32_t id) {
    EnterCriticalSection(&g_conn_wait_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (g_conn_wait[i].used && g_conn_wait[i].id == id) {
            g_conn_wait[i].used = 0;
        }
    }
    LeaveCriticalSection(&g_conn_wait_cs);
}

void conn_wait_signal(uint32_t id) {
    EnterCriticalSection(&g_conn_wait_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (g_conn_wait[i].used && g_conn_wait[i].id == id) {
            SetEvent(g_conn_wait[i].ev);
        }
    }
    LeaveCriticalSection(&g_conn_wait_cs);
}

DWORD WINAPI tcp_relay_thread(LPVOID param) {
    TcpConn *c = static_cast<TcpConn *>(param);
    char buf[4096];
    int32_t n;
    SOCKET rs = c->relay_sock;

    while (!g_shutdown && c->alive && rs != INVALID_SOCKET) {
        fd_set rfds;
        timeval tv;
        FD_ZERO(&rfds);
        FD_SET(rs, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        n = select_orig(0, &rfds, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(rs, &rfds)) {
            n = recv_orig(rs, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            tun_send_tcp_hdr(KIND_TCP_DATA, c->conn_id,
                    reinterpret_cast<uint8_t *>(buf), n);
        }
    }
    tun_send_tcp_hdr(KIND_TCP_FIN, c->conn_id, nullptr, 0);
    InterlockedExchange(&c->alive, 0);
    return 0;
}

int32_t make_loopback_pair(SOCKET *out_game, SOCKET *out_relay) {
    SOCKET lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKET a = INVALID_SOCKET;
    SOCKET b = INVALID_SOCKET;
    sockaddr_in addr;
    int32_t alen = sizeof(addr);
    u_long nb = 1;

    if (lst == INVALID_SOCKET) {
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind_orig(lst, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        goto fail;
    }
    if (getsockname_orig(lst, reinterpret_cast<sockaddr *>(&addr), &alen) != 0) {
        goto fail;
    }
    if (listen_orig(lst, 1) != 0) {
        goto fail;
    }
    a = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (a == INVALID_SOCKET) {
        goto fail;
    }
    if (connect_orig(a, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        goto fail;
    }
    b = accept_orig(lst, nullptr, nullptr);
    if (b == INVALID_SOCKET) {
        goto fail;
    }
    closesocket_orig(lst);
    if (ioctlsocket_orig) {
        ioctlsocket_orig(a, FIONBIO, &nb);
        ioctlsocket_orig(b, FIONBIO, &nb);
    } else {
        ioctlsocket(a, FIONBIO, &nb);
        ioctlsocket(b, FIONBIO, &nb);
    }
    *out_game = a;
    *out_relay = b;
    return 0;

fail:
    if (lst != INVALID_SOCKET) {
        closesocket_orig(lst);
    }
    if (a != INVALID_SOCKET) {
        closesocket_orig(a);
    }
    if (b != INVALID_SOCKET) {
        closesocket_orig(b);
    }
    return -1;
}

TcpConn *tcp_alloc(uint32_t conn_id, SOCKET game, SOCKET relay) {
    HANDLE th;

    EnterCriticalSection(&g_tcp_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (!g_tcp[i].in_use) {
            memset(&g_tcp[i], 0, sizeof(g_tcp[i]));
            g_tcp[i].in_use = 1;
            g_tcp[i].conn_id = conn_id;
            g_tcp[i].game_sock = game;
            g_tcp[i].relay_sock = relay;
            g_tcp[i].alive = 1;
            th = CreateThread(nullptr, 0, tcp_relay_thread, &g_tcp[i], 0, nullptr);
            if (!th) {
                g_tcp[i].in_use = 0;
                LeaveCriticalSection(&g_tcp_cs);
                return nullptr;
            }
            g_tcp[i].thread = th;
            LeaveCriticalSection(&g_tcp_cs);
            return &g_tcp[i];
        }
    }
    LeaveCriticalSection(&g_tcp_cs);
    return nullptr;
}

void tcp_free_by_index(int32_t i) {
    HANDLE th;
    SOCKET rs;

    EnterCriticalSection(&g_tcp_cs);
    if (!g_tcp[i].in_use) {
        LeaveCriticalSection(&g_tcp_cs);
        return;
    }
    InterlockedExchange(&g_tcp[i].alive, 0);
    rs = g_tcp[i].relay_sock;
    th = g_tcp[i].thread;
    g_tcp[i].relay_sock = INVALID_SOCKET;
    g_tcp[i].thread = nullptr;
    LeaveCriticalSection(&g_tcp_cs);
    if (rs != INVALID_SOCKET) {
        closesocket_orig(rs);
    }
    if (th) {
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }
    EnterCriticalSection(&g_tcp_cs);
    g_tcp[i].in_use = 0;
    LeaveCriticalSection(&g_tcp_cs);
}

void tcp_free_sock(SOCKET s) {
    int32_t hit = -1;

    EnterCriticalSection(&g_tcp_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (g_tcp[i].in_use &&
                (g_tcp[i].game_sock == s || g_tcp[i].relay_sock == s)) {
            hit = i;
            break;
        }
    }
    LeaveCriticalSection(&g_tcp_cs);
    if (hit >= 0) {
        tcp_free_by_index(hit);
    }
}

void tcp_free_id(uint32_t id) {
    int32_t hit = -1;

    EnterCriticalSection(&g_tcp_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (g_tcp[i].in_use && g_tcp[i].conn_id == id) {
            hit = i;
            break;
        }
    }
    LeaveCriticalSection(&g_tcp_cs);
    if (hit >= 0) {
        tcp_free_by_index(hit);
    }
}

SOCKET tcp_relay_for_id(uint32_t id) {
    SOCKET rs = INVALID_SOCKET;

    EnterCriticalSection(&g_tcp_cs);
    for (int32_t i = 0; i < MAX_TCP; i++) {
        if (g_tcp[i].in_use && g_tcp[i].conn_id == id) {
            rs = g_tcp[i].relay_sock;
            break;
        }
    }
    LeaveCriticalSection(&g_tcp_cs);
    return rs;
}

void on_tcp_syn(uint32_t conn_id, uint16_t dport, uint16_t sport) {
    ListenState *ls;
    SOCKET game_side;
    SOCKET relay_side;
    AcceptItem item;
    uint32_t peer_ip;
    int32_t queued = 0;

    EnterCriticalSection(&g_tcp_cs);
    ls = listen_find_port(dport);
    LeaveCriticalSection(&g_tcp_cs);
    if (!ls) {
        log_warning("network", "NIC tunnel: TCP_SYN unbound port {}", dport);
        return;
    }
    if (make_loopback_pair(&game_side, &relay_side) != 0) {
        log_warning("network", "NIC tunnel: loopback pair failed");
        return;
    }
    if (!tcp_alloc(conn_id, game_side, relay_side)) {
        closesocket_orig(game_side);
        closesocket_orig(relay_side);
        return;
    }
    peer_get(nullptr, &peer_ip);
    memset(&item, 0, sizeof(item));
    item.client_sock = game_side;
    item.peer.sin_family = AF_INET;
    item.peer.sin_addr.s_addr = htonl(peer_ip);
    item.peer.sin_port = htons(sport);
    EnterCriticalSection(&ls->cs);
    if (!ls->closing && ls->q_count < MAX_ACCEPT_Q) {
        ls->q[ls->q_count++] = item;
        sock_signal_read(ls->event);
        queued = 1;
    }
    LeaveCriticalSection(&ls->cs);
    if (!queued) {
        log_warning("network",
                "NIC tunnel: TCP accept queue full dport={}", dport);
        closesocket_orig(game_side);
        tcp_free_id(conn_id);
        tun_send_tcp_hdr(KIND_TCP_FIN, conn_id, nullptr, 0);
        return;
    }
    tun_send_tcp_hdr(KIND_TCP_ACCEPTED, conn_id, nullptr, 0);
    log_info("network", "NIC tunnel: TCP accepted conn={} dport={}",
            conn_id, dport);
}

void on_data_payload(const uint8_t *p, int32_t n) {
    uint8_t kind;
    uint32_t peer_ip;

    if (n < 1) {
        return;
    }
    kind = p[0];
    peer_get(nullptr, &peer_ip);
    if (kind == KIND_UDP && n >= 5) {
        udp_enqueue(rd16(p + 3), peer_ip, rd16(p + 1), p + 5, n - 5);
        return;
    }
    if (kind == KIND_TCP_SYN && n >= 9) {
        on_tcp_syn(rd32(p + 1), rd16(p + 5), rd16(p + 7));
        return;
    }
    if (kind == KIND_TCP_ACCEPTED && n >= 5) {
        conn_wait_signal(rd32(p + 1));
        return;
    }
    if (kind == KIND_TCP_DATA && n >= 5) {
        SOCKET rs = tcp_relay_for_id(rd32(p + 1));
        if (rs != INVALID_SOCKET) {
            send_orig(rs, reinterpret_cast<const char *>(p + 5), n - 5, 0);
        }
        return;
    }
    if (kind == KIND_TCP_FIN && n >= 5) {
        tcp_free_id(rd32(p + 1));
    }
}

DWORD WINAPI keepalive_thread(LPVOID) {
    uint8_t ipb[4];

    wr32(ipb, g_local_ip);
    while (!g_shutdown) {
        for (int32_t i = 0; i < KEEP_ALIVE_SEC * 10 && !g_shutdown; i++) {
            Sleep(100);
        }
        if (g_shutdown || g_tun_sock == INVALID_SOCKET) {
            continue;
        }
        if (!g_is_hub && peer_get(nullptr, nullptr) && !g_registered) {
            uint8_t reg[36];
            uint8_t pkt[41];
            sockaddr_in peer;

            memcpy(reg, g_password, 32);
            wr32(reg + 32, g_local_ip);
            pkt[0] = MAGIC0;
            pkt[1] = MAGIC1;
            pkt[2] = MAGIC2;
            pkt[3] = MAGIC3;
            pkt[4] = MSG_REGISTER;
            memcpy(pkt + 5, reg, 36);
            if (peer_get(&peer, nullptr)) {
                tun_sendto_raw(g_tun_sock, reinterpret_cast<char *>(pkt), 41,
                        &peer);
            }
        }
        tun_send(MSG_KEEPALIVE, ipb, 4);
    }
    return 0;
}

DWORD WINAPI tunnel_thread(LPVOID) {
    uint8_t buf[65536];

    while (!g_shutdown) {
        sockaddr_in from;
        int32_t flen = sizeof(from);
        int32_t n;
        uint8_t type;
        uint8_t *payload;
        int32_t plen;

        if (g_tun_sock == INVALID_SOCKET) {
            Sleep(50);
            continue;
        }
        n = tun_recvfrom_raw(g_tun_sock, reinterpret_cast<char *>(buf),
                sizeof(buf), &from, &flen);
        if (n < 5) {
            if (g_shutdown) {
                break;
            }
            continue;
        }
        if (buf[0] != MAGIC0 || buf[1] != MAGIC1 || buf[2] != MAGIC2 ||
                buf[3] != MAGIC3) {
            continue;
        }
        type = buf[4];
        payload = buf + 5;
        plen = n - 5;
        if (type == MSG_REGISTER && g_is_hub && plen >= 36) {
            uint8_t pkt[6];

            if (memcmp(payload, g_password, 32) != 0) {
                pkt[0] = MAGIC0;
                pkt[1] = MAGIC1;
                pkt[2] = MAGIC2;
                pkt[3] = MAGIC3;
                pkt[4] = MSG_REGISTER_ACK;
                pkt[5] = 2;
                tun_sendto_raw(g_tun_sock, reinterpret_cast<char *>(pkt), 6,
                        &from);
                continue;
            }
            peer_set(&from, 1, rd32(payload + 32));
            pkt[0] = MAGIC0;
            pkt[1] = MAGIC1;
            pkt[2] = MAGIC2;
            pkt[3] = MAGIC3;
            pkt[4] = MSG_REGISTER_ACK;
            pkt[5] = ACK_OK;
            tun_sendto_raw(g_tun_sock, reinterpret_cast<char *>(pkt), 6, &from);
            InterlockedExchange(&g_registered, 1);
            {
                char peer_str[16];
                ip_to_str(rd32(payload + 32), peer_str, sizeof(peer_str));
                log_info("network",
                        "NIC tunnel: hub peer registered peer_ip={}", peer_str);
            }
            continue;
        }
        if (type == MSG_REGISTER_ACK && plen >= 1) {
            if (payload[0] == ACK_OK) {
                InterlockedExchange(&g_registered, 1);
                log_info("network", "NIC tunnel: registered ok");
            } else {
                log_warning("network", "NIC tunnel: register status={}",
                        payload[0]);
            }
            continue;
        }
        if (type == MSG_KEEPALIVE) {
            if (plen >= 4) {
                peer_set(&from, 1, rd32(payload));
            } else if (g_is_hub) {
                peer_set(&from, 1, 0);
            }
            continue;
        }
        if (type == MSG_DATA && plen > 0) {
            if (g_is_hub) {
                peer_set(&from, 1, 0);
            }
            on_data_payload(payload, plen);
        }
    }
    return 0;
}

int32_t start_tunnel() {
    WSADATA wsa;
    sockaddr_in bind_addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_warning("network", "NIC tunnel: WSAStartup failed");
        return -1;
    }
    g_tun_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_tun_sock == INVALID_SOCKET) {
        log_warning("network", "NIC tunnel: socket create failed");
        return -1;
    }
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (g_is_hub) {
        bind_addr.sin_port = htons(g_tunnel_port);
        if (tun_bind_raw(g_tun_sock, reinterpret_cast<sockaddr *>(&bind_addr),
                sizeof(bind_addr)) != 0) {
            log_warning("network", "NIC tunnel: hub bind fail err={}",
                    WSAGetLastError());
            return -1;
        }
        log_info("network", "NIC tunnel: hub listen UDP {}", g_tunnel_port);
    } else {
        sockaddr_in peer;
        char host[128];
        char *colon;
        int32_t port = g_tunnel_port;
        size_t hl;

        bind_addr.sin_port = 0;
        tun_bind_raw(g_tun_sock, reinterpret_cast<sockaddr *>(&bind_addr),
                sizeof(bind_addr));
        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        colon = strchr(g_hub, ':');
        if (colon) {
            hl = static_cast<size_t>(colon - g_hub);
            if (hl >= sizeof(host)) {
                hl = sizeof(host) - 1;
            }
            memcpy(host, g_hub, hl);
            host[hl] = 0;
            port = atoi(colon + 1);
        } else {
            strncpy(host, g_hub, sizeof(host) - 1);
            host[sizeof(host) - 1] = 0;
        }
        peer.sin_port = htons(static_cast<u_short>(port));
        peer.sin_addr.s_addr = inet_addr(host);
        if (peer.sin_addr.s_addr == INADDR_NONE) {
            hostent *he = gethostbyname(host);
            if (!he) {
                log_warning("network",
                        "NIC tunnel: failed to resolve hub host");
                return -1;
            }
            memcpy(&peer.sin_addr, he->h_addr, 4);
        }
        peer_set(&peer, 1, 0);
        {
            uint8_t reg[36];
            uint8_t pkt[41];

            memcpy(reg, g_password, 32);
            wr32(reg + 32, g_local_ip);
            pkt[0] = MAGIC0;
            pkt[1] = MAGIC1;
            pkt[2] = MAGIC2;
            pkt[3] = MAGIC3;
            pkt[4] = MSG_REGISTER;
            memcpy(pkt + 5, reg, 36);
            tun_sendto_raw(g_tun_sock, reinterpret_cast<char *>(pkt), 41, &peer);
            log_info("network", "NIC tunnel: client register -> {}", g_hub);
        }
    }
    g_tunnel_th = CreateThread(nullptr, 0, tunnel_thread, nullptr, 0, nullptr);
    g_keepalive_th = CreateThread(nullptr, 0, keepalive_thread, nullptr, 0,
            nullptr);
    if (!g_tunnel_th || !g_keepalive_th) {
        log_warning("network", "NIC tunnel: thread create failed");
        return -1;
    }
    return 0;
}

uint16_t sock_local_port(SOCKET s) {
    sockaddr_in a;
    int32_t alen = sizeof(a);

    memset(&a, 0, sizeof(a));
    if (getsockname_orig &&
            getsockname_orig(s, reinterpret_cast<sockaddr *>(&a), &alen) == 0) {
        return ntohs(a.sin_port);
    }
    return 0;
}

int WSAAPI bind_hook(SOCKET s, const sockaddr *name, int namelen) {
    int32_t r;
    UdpSock *us;
    int32_t type = 0;
    int32_t tlen = sizeof(type);

    r = bind_orig(s, name, namelen);
    getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &tlen);
    if (type == SOCK_DGRAM) {
        us = udp_add(s);
        if (us) {
            uint16_t p = sock_local_port(s);
            us->bound_port = static_cast<int32_t>(p);
        }
    }
    (void) namelen;
    return r;
}

int WSAAPI sendto_hook(SOCKET s, const char *buf, int len, int flags,
        const sockaddr *to, int tolen) {
    if (s == g_tun_sock) {
        return sendto_orig(s, buf, len, flags, to, tolen);
    }
    if (to && to->sa_family == AF_INET && len >= 0) {
        const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(to);
        uint32_t dip = ntohl(in->sin_addr.s_addr);

        if (is_bcast(dip) || in_overlay(dip)) {
            uint16_t sport = sock_local_port(s);
            uint16_t dport = ntohs(in->sin_port);
            UdpSock *us = udp_add(s);
            int32_t to_self = (dip == g_local_ip);

            if (us && !us->bound_port) {
                us->bound_port = static_cast<int32_t>(sport);
            }
            if (to_self) {
                udp_enqueue(dport, g_local_ip, sport,
                        reinterpret_cast<const uint8_t *>(buf), len);
                return len;
            }
            tun_send_udp(sport, dport, reinterpret_cast<const uint8_t *>(buf),
                    len);
            return len;
        }
    }
    return sendto_orig(s, buf, len, flags, to, tolen);
}

int WSAAPI WSASendTo_hook(SOCKET s, LPWSABUF b, DWORD n, LPDWORD sent,
        DWORD flags, const sockaddr *to, int tolen, LPWSAOVERLAPPED ov,
        LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    if (s == g_tun_sock) {
        return WSASendTo_orig(s, b, n, sent, flags, to, tolen, ov, cr);
    }
    if (!ov && !cr && n == 1 && b && to && to->sa_family == AF_INET) {
        const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(to);
        uint32_t dip = ntohl(in->sin_addr.s_addr);

        if (is_bcast(dip) || in_overlay(dip)) {
            int32_t r = sendto_hook(s, reinterpret_cast<const char *>(b[0].buf),
                    static_cast<int>(b[0].len), 0, to, tolen);
            if (sent) {
                *sent = static_cast<DWORD>(r < 0 ? 0 : r);
            }
            return r < 0 ? SOCKET_ERROR : 0;
        }
    }
    return WSASendTo_orig(s, b, n, sent, flags, to, tolen, ov, cr);
}

int32_t pop_udp(UdpSock *us, char *buf, int32_t len, sockaddr *from,
        int32_t *fromlen) {
    UdpDgram d;

    if (!us || len < 0) {
        return -1;
    }
    EnterCriticalSection(&us->cs);
    if (us->closing || us->q_count <= 0) {
        LeaveCriticalSection(&us->cs);
        return -1;
    }
    d = us->q[us->q_head];
    us->q_head = (us->q_head + 1) % MAX_UDP_Q;
    us->q_count--;
    LeaveCriticalSection(&us->cs);
    if (len > d.len) {
        len = d.len;
    }
    memcpy(buf, d.data, static_cast<size_t>(len));
    if (from && fromlen && *fromlen >= static_cast<int32_t>(sizeof(sockaddr_in))) {
        sockaddr_in *in = reinterpret_cast<sockaddr_in *>(from);
        memset(in, 0, sizeof(*in));
        in->sin_family = AF_INET;
        in->sin_addr.s_addr = htonl(d.from_ip);
        in->sin_port = htons(d.from_port);
        *fromlen = sizeof(*in);
    }
    return len;
}

int WSAAPI recvfrom_hook(SOCKET s, char *buf, int len, int flags,
        sockaddr *from, int *fromlen) {
    UdpSock *us;
    int32_t r;

    if (s == g_tun_sock) {
        return recvfrom_orig(s, buf, len, flags, from, fromlen);
    }
    if (len < 0) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    EnterCriticalSection(&g_udp_cs);
    us = udp_find(s);
    LeaveCriticalSection(&g_udp_cs);
    if (us) {
        r = pop_udp(us, buf, len, from, fromlen);
        if (r >= 0) {
            return r;
        }
        {
            fd_set rfds;
            timeval tv;
            FD_ZERO(&rfds);
            FD_SET(s, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            if (select_orig(0, &rfds, nullptr, nullptr, &tv) > 0) {
                return recvfrom_orig(s, buf, len, flags, from, fromlen);
            }
        }
        if (us->event && WaitForSingleObject(us->event, 0) == WAIT_OBJECT_0) {
            r = pop_udp(us, buf, len, from, fromlen);
            if (r >= 0) {
                return r;
            }
        }
        if (us->closing) {
            WSASetLastError(WSAENOTSOCK);
            return SOCKET_ERROR;
        }
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }
    return recvfrom_orig(s, buf, len, flags, from, fromlen);
}

int WSAAPI WSARecvFrom_hook(SOCKET s, LPWSABUF b, DWORD n, LPDWORD recvd,
        LPDWORD flags, sockaddr *from, LPINT fromlen, LPWSAOVERLAPPED ov,
        LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    if (s == g_tun_sock) {
        return WSARecvFrom_orig(s, b, n, recvd, flags, from, fromlen, ov, cr);
    }
    if (!ov && !cr && n == 1 && b) {
        int32_t fl = fromlen ? *fromlen : 0;
        int32_t r = recvfrom_hook(s, reinterpret_cast<char *>(b[0].buf),
                static_cast<int>(b[0].len), flags ? *flags : 0, from,
                fromlen ? &fl : nullptr);
        if (fromlen) {
            *fromlen = fl;
        }
        if (r == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        if (recvd) {
            *recvd = static_cast<DWORD>(r);
        }
        return 0;
    }
    return WSARecvFrom_orig(s, b, n, recvd, flags, from, fromlen, ov, cr);
}

int WSAAPI select_hook(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, const timeval *timeout) {
    fd_set orig_r;
    int32_t tracked = 0;
    DWORD start;
    DWORD budget_ms;

    if (readfds) {
        orig_r = *readfds;
        tracked = fd_set_contains_tracked(&orig_r);
    }
    if (!tracked) {
        return select_orig(nfds, readfds, writefds, exceptfds, timeout);
    }

    start = GetTickCount();
    if (!timeout) {
        budget_ms = INFINITE;
    } else {
        budget_ms = static_cast<DWORD>(
                timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
        if (timeout->tv_sec == 0 && timeout->tv_usec == 0) {
            budget_ms = 0;
        }
    }

    for (;;) {
        fd_set r;
        fd_set w;
        fd_set e;
        fd_set *pr = nullptr;
        fd_set *pw = nullptr;
        fd_set *pe = nullptr;
        timeval tv;
        int32_t n;
        int32_t added;
        DWORD elapsed;
        DWORD remain;
        int32_t slice_ms;

        if (budget_ms == INFINITE) {
            slice_ms = 50;
        } else {
            elapsed = GetTickCount() - start;
            if (elapsed >= budget_ms) {
                if (readfds) {
                    FD_ZERO(readfds);
                }
                if (writefds) {
                    FD_ZERO(writefds);
                }
                if (exceptfds) {
                    FD_ZERO(exceptfds);
                }
                if (fd_set_has_overlay(&orig_r)) {
                    if (readfds) {
                        FD_ZERO(readfds);
                        merge_overlay_into_fdset(readfds, &orig_r);
                    }
                    return 1;
                }
                return 0;
            }
            remain = budget_ms - elapsed;
            slice_ms = static_cast<int32_t>(remain > 50 ? 50 : remain);
        }

        if (fd_set_has_overlay(&orig_r)) {
            slice_ms = 0;
        }
        tv.tv_sec = slice_ms / 1000;
        tv.tv_usec = (slice_ms % 1000) * 1000;

        if (readfds) {
            r = orig_r;
            pr = &r;
        }
        if (writefds) {
            w = *writefds;
            pw = &w;
        }
        if (exceptfds) {
            e = *exceptfds;
            pe = &e;
        }

        n = select_orig(nfds, pr, pw, pe, &tv);
        if (n == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        added = 0;
        if (pr) {
            added = merge_overlay_into_fdset(pr, &orig_r);
        }
        if (n + added > 0) {
            if (readfds) {
                *readfds = r;
            }
            if (writefds) {
                *writefds = w;
            }
            if (exceptfds) {
                *exceptfds = e;
            }
            return n + added;
        }
        if (budget_ms == 0) {
            return 0;
        }
    }
}

int WSAAPI listen_hook(SOCKET s, int backlog) {
    int32_t r;
    uint16_t port;
    HANDLE ev;

    r = listen_orig(s, backlog);
    port = sock_local_port(s);
    if (r == 0 && port) {
        EnterCriticalSection(&g_tcp_cs);
        for (int32_t i = 0; i < MAX_LISTEN; i++) {
            if (!g_listen[i].in_use) {
                memset(&g_listen[i], 0, sizeof(g_listen[i]));
                ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!ev) {
                    break;
                }
                g_listen[i].in_use = 1;
                g_listen[i].listen_sock = s;
                g_listen[i].port = port;
                g_listen[i].nonblock = nb_get(s);
                InitializeCriticalSection(&g_listen[i].cs);
                g_listen[i].event = ev;
                if (!g_logged_overlay_listen) {
                    log_misc("network",
                            "NIC tunnel: overlay listen port={} nb={}",
                            port, g_listen[i].nonblock);
                    g_logged_overlay_listen = true;
                }
                break;
            }
        }
        LeaveCriticalSection(&g_tcp_cs);
    }
    return r;
}

int WSAAPI ioctlsocket_hook(SOCKET s, long cmd, u_long *argp) {
    int32_t r = ioctlsocket_orig(s, cmd, argp);

    if (r == 0 && cmd == FIONBIO && argp) {
        ListenState *ls;
        nb_set(s, (*argp) ? 1 : 0);
        EnterCriticalSection(&g_tcp_cs);
        ls = listen_find(s);
        if (ls) {
            ls->nonblock = (*argp) ? 1 : 0;
        }
        LeaveCriticalSection(&g_tcp_cs);
    }
    return r;
}

SOCKET WSAAPI accept_hook(SOCKET s, sockaddr *addr, int *addrlen) {
    ListenState *ls;

    EnterCriticalSection(&g_tcp_cs);
    ls = listen_find(s);
    LeaveCriticalSection(&g_tcp_cs);
    if (ls) {
        for (;;) {
            EnterCriticalSection(&ls->cs);
            if (ls->closing) {
                LeaveCriticalSection(&ls->cs);
                WSASetLastError(WSAENOTSOCK);
                return INVALID_SOCKET;
            }
            if (ls->q_count > 0) {
                AcceptItem it = ls->q[0];
                for (int32_t i = 1; i < ls->q_count; i++) {
                    ls->q[i - 1] = ls->q[i];
                }
                ls->q_count--;
                LeaveCriticalSection(&ls->cs);
                if (addr && addrlen &&
                        *addrlen >= static_cast<int>(sizeof(it.peer))) {
                    memcpy(addr, &it.peer, sizeof(it.peer));
                    *addrlen = sizeof(it.peer);
                }
                return it.client_sock;
            }
            LeaveCriticalSection(&ls->cs);
            if (ls->nonblock || nb_get(s)) {
                WSASetLastError(WSAEWOULDBLOCK);
                return INVALID_SOCKET;
            }
            if (!ls->event || WaitForSingleObject(ls->event, 500) != WAIT_OBJECT_0) {
                EnterCriticalSection(&g_tcp_cs);
                if (!listen_find(s) || ls->closing) {
                    LeaveCriticalSection(&g_tcp_cs);
                    WSASetLastError(WSAENOTSOCK);
                    return INVALID_SOCKET;
                }
                LeaveCriticalSection(&g_tcp_cs);
            }
        }
    }
    return accept_orig(s, addr, addrlen);
}

int WSAAPI connect_hook(SOCKET s, const sockaddr *name, int namelen) {
    if (name && name->sa_family == AF_INET) {
        const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(name);
        uint32_t dip = ntohl(in->sin_addr.s_addr);

        if (in_overlay(dip) && dip != g_local_ip) {
            uint32_t cid = static_cast<uint32_t>(
                    InterlockedIncrement(&g_next_conn_id));
            uint16_t dport = ntohs(in->sin_port);
            uint16_t sport;
            HANDLE ev;
            sockaddr_in la;
            int32_t was_nb = nb_get(s);
            u_long zero = 0;
            u_long one = 1;
            char dip_str[16];

            sport = sock_local_port(s);
            if (!sport) {
                memset(&la, 0, sizeof(la));
                la.sin_family = AF_INET;
                la.sin_addr.s_addr = htonl(INADDR_ANY);
                la.sin_port = 0;
                bind_orig(s, reinterpret_cast<sockaddr *>(&la), sizeof(la));
                sport = sock_local_port(s);
            }
            ev = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            if (!ev) {
                WSASetLastError(WSAENOBUFS);
                return SOCKET_ERROR;
            }
            if (!conn_wait_reg(cid, ev)) {
                CloseHandle(ev);
                WSASetLastError(WSAENOBUFS);
                return SOCKET_ERROR;
            }
            tun_send_tcp_syn(cid, dport, sport);
            ip_to_str(dip, dip_str, sizeof(dip_str));
            log_info("network",
                    "NIC tunnel: TCP connect -> {}:{} conn={}",
                    dip_str, dport, cid);
            if (WaitForSingleObject(ev, 10000) != WAIT_OBJECT_0) {
                conn_wait_clear(cid);
                CloseHandle(ev);
                WSASetLastError(WSAETIMEDOUT);
                return SOCKET_ERROR;
            }
            conn_wait_clear(cid);
            CloseHandle(ev);
            {
                SOCKET lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                SOCKET rel;
                sockaddr_in addr;
                int32_t alen = sizeof(addr);
                int32_t cr;
                int32_t err;

                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addr.sin_port = 0;
                if (ioctlsocket_orig) {
                    ioctlsocket_orig(s, FIONBIO, &zero);
                } else {
                    ioctlsocket(s, FIONBIO, &zero);
                }
                bind_orig(lst, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
                getsockname_orig(lst, reinterpret_cast<sockaddr *>(&addr), &alen);
                listen_orig(lst, 1);
                cr = connect_orig(s, reinterpret_cast<sockaddr *>(&addr),
                        sizeof(addr));
                err = (cr != 0) ? WSAGetLastError() : 0;
                if (cr != 0 && err == WSAEWOULDBLOCK) {
                    fd_set wfds;
                    timeval tv;
                    FD_ZERO(&wfds);
                    FD_SET(s, &wfds);
                    tv.tv_sec = 2;
                    tv.tv_usec = 0;
                    if (select_orig(0, nullptr, &wfds, nullptr, &tv) > 0) {
                        cr = 0;
                        err = 0;
                    }
                }
                if (cr != 0) {
                    log_warning("network",
                            "NIC tunnel: TCP loopback connect fail "
                            "conn={} err={}",
                            cid, err);
                    closesocket_orig(lst);
                    if (was_nb) {
                        if (ioctlsocket_orig) {
                            ioctlsocket_orig(s, FIONBIO, &one);
                        } else {
                            ioctlsocket(s, FIONBIO, &one);
                        }
                    }
                    WSASetLastError(err ? err : WSAECONNREFUSED);
                    return SOCKET_ERROR;
                }
                rel = accept_orig(lst, nullptr, nullptr);
                closesocket_orig(lst);
                if (rel == INVALID_SOCKET) {
                    log_warning("network",
                            "NIC tunnel: TCP loopback accept fail "
                            "conn={} err={}",
                            cid, WSAGetLastError());
                    if (was_nb) {
                        if (ioctlsocket_orig) {
                            ioctlsocket_orig(s, FIONBIO, &one);
                        } else {
                            ioctlsocket(s, FIONBIO, &one);
                        }
                    }
                    return SOCKET_ERROR;
                }
                {
                    u_long nbr = 1;
                    if (ioctlsocket_orig) {
                        ioctlsocket_orig(rel, FIONBIO, &nbr);
                    } else {
                        ioctlsocket(rel, FIONBIO, &nbr);
                    }
                }
                if (was_nb) {
                    if (ioctlsocket_orig) {
                        ioctlsocket_orig(s, FIONBIO, &one);
                    } else {
                        ioctlsocket(s, FIONBIO, &one);
                    }
                    nb_set(s, 1);
                } else {
                    nb_set(s, 0);
                }
                if (!tcp_alloc(cid, s, rel)) {
                    closesocket_orig(rel);
                    return SOCKET_ERROR;
                }
                log_info("network",
                        "NIC tunnel: TCP connected conn={}", cid);
                return 0;
            }
        }
    }
    return connect_orig(s, name, namelen);
}

int WSAAPI WSAConnect_hook(SOCKET s, const sockaddr *name, int namelen,
        LPWSABUF caller, LPWSABUF callee, LPQOS sqos, LPQOS gqos) {
    if (name && name->sa_family == AF_INET) {
        const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(name);
        uint32_t dip = ntohl(in->sin_addr.s_addr);

        if (in_overlay(dip) && dip != g_local_ip) {
            return connect_hook(s, name, namelen);
        }
    }
    if (WSAConnect_orig) {
        return WSAConnect_orig(s, name, namelen, caller, callee, sqos, gqos);
    }
    return connect_orig(s, name, namelen);
}

void listen_close(SOCKET s) {
    ListenState *ls = nullptr;
    AcceptItem drain[MAX_ACCEPT_Q];
    int32_t dn = 0;
    HANDLE ev = nullptr;

    EnterCriticalSection(&g_tcp_cs);
    for (int32_t i = 0; i < MAX_LISTEN; i++) {
        if (g_listen[i].in_use && g_listen[i].listen_sock == s) {
            ls = &g_listen[i];
            break;
        }
    }
    if (ls) {
        ls->closing = 1;
        ev = ls->event;
        if (ev) {
            SetEvent(ev);
        }
        EnterCriticalSection(&ls->cs);
        dn = ls->q_count;
        for (int32_t i = 0; i < dn; i++) {
            drain[i] = ls->q[i];
        }
        ls->q_count = 0;
        LeaveCriticalSection(&ls->cs);
    }
    LeaveCriticalSection(&g_tcp_cs);
    for (int32_t i = 0; i < dn; i++) {
        tcp_free_sock(drain[i].client_sock);
        closesocket_orig(drain[i].client_sock);
    }
    EnterCriticalSection(&g_tcp_cs);
    if (ls && ls->listen_sock == s) {
        if (ls->event) {
            CloseHandle(ls->event);
        }
        DeleteCriticalSection(&ls->cs);
        ls->in_use = 0;
        ls->closing = 0;
    }
    LeaveCriticalSection(&g_tcp_cs);
}

int WSAAPI closesocket_hook(SOCKET s) {
    udp_remove(s);
    tcp_free_sock(s);
    nb_clear(s);
    listen_close(s);
    return closesocket_orig(s);
}

void install_hooks() {
    bool ok = true;

    ok &= detour::trampoline_try("ws2_32.dll", "bind",
            bind_hook, &bind_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "sendto",
            sendto_hook, &sendto_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "WSASendTo",
            WSASendTo_hook, &WSASendTo_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "recvfrom",
            recvfrom_hook, &recvfrom_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "WSARecvFrom",
            WSARecvFrom_hook, &WSARecvFrom_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "select",
            select_hook, &select_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "listen",
            listen_hook, &listen_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "accept",
            accept_hook, &accept_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "connect",
            connect_hook, &connect_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "WSAConnect",
            WSAConnect_hook, &WSAConnect_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "ioctlsocket",
            ioctlsocket_hook, &ioctlsocket_orig);
    ok &= detour::trampoline_try("ws2_32.dll", "closesocket",
            closesocket_hook, &closesocket_orig);

    {
        HMODULE ws = GetModuleHandleA("ws2_32.dll");
        if (!ws) {
            ws = LoadLibraryA("ws2_32.dll");
        }
        if (ws) {
            send_orig = reinterpret_cast<decltype(send) *>(
                    GetProcAddress(ws, "send"));
            recv_orig = reinterpret_cast<decltype(recv) *>(
                    GetProcAddress(ws, "recv"));
            getsockname_orig = reinterpret_cast<decltype(getsockname) *>(
                    GetProcAddress(ws, "getsockname"));
        }
    }

    if (!ok) {
        log_warning("network",
                "NIC tunnel: one or more divert hooks failed to install");
    } else {
        log_info("network",
                "NIC tunnel: divert hooks installed "
                "(bind/sendto/recvfrom/select/listen/accept/connect/...)");
    }
}

} // namespace nicspoof_tunnel_detail

void nicspoof_tunnel_init(const NicSpoofConfig &cfg) {
    using namespace nicspoof_tunnel_detail;

    static bool done = false;
    char ipstr[16];
    size_t pw_len;

    if (done) {
        return;
    }
    done = true;

    if (cfg.mode != NicSpoofMode::TunnelHost &&
            cfg.mode != NicSpoofMode::TunnelClient) {
        return;
    }

    g_is_hub = (cfg.mode == NicSpoofMode::TunnelHost);
    g_tunnel_port = cfg.tunnel_port ? cfg.tunnel_port : 51820;

    if (!g_is_hub && cfg.hub_host.empty()) {
        log_warning("network",
                "NIC tunnel: TunnelClient requires hub host; not starting");
        return;
    }

    g_local_ip = nicspoof_local_ip();
    g_mask = nicspoof_mask();
    g_subnet = nicspoof_subnet();
    g_peer_ip = 0;

    memset(g_password, 0, sizeof(g_password));
    pw_len = cfg.password.size();
    if (pw_len > sizeof(g_password)) {
        pw_len = sizeof(g_password);
    }
    if (pw_len > 0) {
        memcpy(g_password, cfg.password.data(), pw_len);
    }

    memset(g_hub, 0, sizeof(g_hub));
    if (!cfg.hub_host.empty()) {
        strncpy(g_hub, cfg.hub_host.c_str(), sizeof(g_hub) - 1);
    }

    InitializeCriticalSection(&g_peer_cs);
    InitializeCriticalSection(&g_udp_cs);
    InitializeCriticalSection(&g_tcp_cs);
    InitializeCriticalSection(&g_conn_wait_cs);
    InitializeCriticalSection(&g_nb_cs);

    ip_to_str(g_local_ip, ipstr, sizeof(ipstr));
    if (g_is_hub) {
        log_info("network",
                "NIC tunnel init role=host local={} port={}",
                ipstr, g_tunnel_port);
    } else {
        log_info("network",
                "NIC tunnel init role=client local={} port={} hub={}",
                ipstr, g_tunnel_port, g_hub);
    }

    install_hooks();
    if (start_tunnel() != 0) {
        log_warning("network", "NIC tunnel: start failed");
        return;
    }
}
