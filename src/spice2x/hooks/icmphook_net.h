#pragma once

#include <winsock2.h>

bool icmphook_is_emulated_socket(SOCKET s);

/*!
 * Handle bind() for emulated ICMP sockets: records interface address and succeeds without kernel bind.
 * Returns true if this socket was handled (caller should return 0).
 */
bool icmphook_try_bind(SOCKET s, const struct sockaddr *name, int namelen, int *out_result);

void icmphook_net_init();
