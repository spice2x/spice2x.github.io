#ifndef SPICEAPI_WRAPPERS_H
#define SPICEAPI_WRAPPERS_H

#include <vector>
#include <string>
#include "connection.h"

namespace spiceapi {

    struct AnalogState {
        std::string name;
        float value;
    };

    struct ButtonState {
        std::string name;
        float value;
    };

    struct LightState {
        std::string name;
        float value;
    };

    struct InfoAvs {
        std::string model, dest, spec, rev, ext;
    };

    struct InfoLauncher {
        std::string version;
        std::string compile_date, compile_time, system_time;
        std::vector<std::string> args;
    };

    struct InfoMemory {
        uint64_t mem_total, mem_total_used, mem_used;
        uint64_t vmem_total, vmem_total_used, vmem_used;
    };

    struct TouchState {
        uint64_t id;
        int64_t x, y;
    };

    struct LCDInfo {
        bool enabled;
        std::string csm;
        uint8_t bri, con, bl, red, green, blue;
    };

    uint64_t msg_gen_id();

    bool analogs_read(Connection &con, std::vector<AnalogState> &states);
    bool analogs_write(Connection &con, std::vector<AnalogState> &states);
    bool analogs_write_reset(Connection &con, std::vector<AnalogState> &states);

    bool buttons_read(Connection &con, std::vector<ButtonState> &states);
    bool buttons_write(Connection &con, std::vector<ButtonState> &states);
    bool buttons_write_reset(Connection &con, std::vector<ButtonState> &states);

    bool card_insert(Connection &con, size_t index, const char *card_id);

    bool coin_get(Connection &con, int &coins);
    bool coin_set(Connection &con, int coins);
    bool coin_insert(Connection &con, int coins=1);
    bool coin_blocker_get(Connection &con, bool &closed);

    bool control_raise(Connection &con, const char *signal);
    bool control_exit(Connection &con);
    bool control_exit(Connection &con, int exit_code);
    bool control_restart(Connection &con);
    bool control_session_refresh(Connection &con);
    bool control_shutdown(Connection &con);
    bool control_reboot(Connection &con);

    bool iidx_ticker_get(Connection &con, char *ticker);
    bool iidx_ticker_set(Connection &con, const char *ticker);
    bool iidx_ticker_reset(Connection &con);

    bool info_avs(Connection &con, InfoAvs &info);
    bool info_launcher(Connection &con, InfoLauncher &info);
    bool info_memory(Connection &con, InfoMemory &info);

    bool keypads_write(Connection &con, unsigned int keypad, const char *input);
    bool keypads_set(Connection &con, unsigned int keypad, std::vector<char> &keys);
    bool keypads_get(Connection &con, unsigned int keypad, std::vector<char> &keys);

    bool lights_read(Connection &con, std::vector<LightState> &states);
    bool lights_write(Connection &con, std::vector<LightState> &states);
    bool lights_write_reset(Connection &con, std::vector<LightState> &states);

    bool memory_write(Connection &con, const char *dll_name, const char *hex, uint32_t offset);
    bool memory_read(Connection &con, const char *dll_name, uint32_t offset, uint32_t size, std::string &hex);
    bool memory_signature(Connection &con, const char *dll_name, const char *signature, const char *replacement,
            uint32_t offset, uint32_t usage, uint32_t &file_offset);

    bool touch_read(Connection &con, std::vector<TouchState> &states);
    bool touch_write(Connection &con, std::vector<TouchState> &states);
    bool touch_write_reset(Connection &con, std::vector<TouchState> &states);

    bool lcd_info(Connection &con, LCDInfo &info);
}

#endif //SPICEAPI_WRAPPERS_H
