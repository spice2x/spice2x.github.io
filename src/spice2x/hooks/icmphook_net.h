#pragma once

#include <winsock2.h>

bool icmphook_is_emulated_socket(SOCKET s);

/*!
 * Handle bind() for emulated ICMP sockets: records interface address and succeeds without kernel bind.
 * Returns true if this socket was handled (caller should return *out_result).
 */
bool icmphook_try_bind(SOCKET s, const struct sockaddr *name, int namelen, int *out_result);

/*!
 * Divert-path helpers for when NIC tunnel owns the MinHook slots on these APIs.
 * Return true if the socket is an emulated ICMP socket and was fully handled.
 */
bool icmphook_try_sendto(SOCKET s, const char *buf, int len, int flags,
        const sockaddr *to, int tolen, int *out_result);
bool icmphook_try_recvfrom(SOCKET s, char *buf, int len, int flags,
        sockaddr *from, int *fromlen, int *out_result);
bool icmphook_try_WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount,
        LPDWORD lpNumberOfBytesSent, DWORD dwFlags, const sockaddr *lpTo,
        int iTolen, LPWSAOVERLAPPED lpOverlapped,
        LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, int *out_result);
bool icmphook_try_WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount,
        LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, sockaddr *lpFrom,
        LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped,
        LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, int *out_result);
bool icmphook_try_ioctlsocket(SOCKET s, long cmd, u_long *argp, int *out_result);
bool icmphook_try_closesocket(SOCKET s, int *out_result);

void icmphook_net_init();
