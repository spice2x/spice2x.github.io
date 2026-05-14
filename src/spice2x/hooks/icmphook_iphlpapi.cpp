#include <winsock2.h>
#include <windows.h>
#include <iptypes.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "util/detour.h"
#include "util/logging.h"

namespace {

#pragma pack(push, 1)
struct IcmpHdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum_be;
};
#pragma pack(pop)

decltype(IcmpSendEcho) *IcmpSendEcho_orig = nullptr;

DWORD WINAPI IcmpSendEcho_hook(
        HANDLE IcmpHandle,
        IPAddr DestinationAddress,
        LPVOID RequestData,
        WORD RequestSize,
        PIP_OPTION_INFORMATION RequestOptions,
        LPVOID ReplyBuffer,
        DWORD ReplySize,
        DWORD Timeout) {
    if (!IcmpSendEcho_orig) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return 0;
    }
    DWORD n = IcmpSendEcho_orig(
            IcmpHandle,
            DestinationAddress,
            RequestData,
            RequestSize,
            RequestOptions,
            ReplyBuffer,
            ReplySize,
            Timeout);
    if (n != 0) {
        return n;
    }
    if (ReplyBuffer == nullptr || ReplySize < sizeof(ICMP_ECHO_REPLY) || RequestData == nullptr) {
        return 0;
    }
    if (RequestSize < sizeof(IcmpHdr)) {
        return 0;
    }
    const auto *req = reinterpret_cast<const uint8_t *>(RequestData);
    const auto *ih = reinterpret_cast<const IcmpHdr *>(req);
    if (ih->type != 8 || ih->code != 0) {
        return 0;
    }

    memset(ReplyBuffer, 0, ReplySize);
    auto *reply = reinterpret_cast<ICMP_ECHO_REPLY *>(ReplyBuffer);
    const size_t data_off = offsetof(ICMP_ECHO_REPLY, Data);
    const size_t avail = ReplySize > data_off ? ReplySize - data_off : 0;
    const size_t copy_len = (std::min)(static_cast<size_t>(RequestSize), avail);
    reply->Address = DestinationAddress;
    reply->Status = IP_SUCCESS;
    reply->RoundTripTime = 1;
    reply->DataSize = static_cast<USHORT>(copy_len);
    if (copy_len > 0) {
        memcpy(reinterpret_cast<uint8_t *>(ReplyBuffer) + data_off, RequestData, copy_len);
    }
    SetLastError(ERROR_SUCCESS);
    return 1;
}

} // namespace

void icmphook_iphlpapi_install() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    if (!detour::trampoline_try(
            "iphlpapi.dll",
            "IcmpSendEcho",
            (void *) IcmpSendEcho_hook,
            (void **) &IcmpSendEcho_orig)) {
        log_warning("network", "ICMP emulation: IcmpSendEcho hook was not installed");
    }
}
