#pragma once

#include <cstdint>
#include <string>

enum class NicSpoofMode {
    Off,
    Offline,
    TunnelHost,
    TunnelClient,
};

struct NicSpoofConfig {
    NicSpoofMode mode = NicSpoofMode::Off;
    uint32_t local_ip = 0x0A64640Au; /* 10.100.100.10 */
    uint16_t tunnel_port = 51820;
    std::string hub_host;
    std::string password;
};

void nicspoof_configure(const NicSpoofConfig &cfg);
void nicspoof_init();

/*! True when mode is TunnelHost or TunnelClient (divert hooks own ws2 sendto/etc.). */
bool nicspoof_tunnel_enabled();

uint32_t nicspoof_local_ip();
uint32_t nicspoof_mask();
uint32_t nicspoof_subnet();
