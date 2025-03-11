#pragma once

#include <cstring>
#include <ctime>
#include <mutex>
#include <optional>
#include <thread>

#include "device.h"
#include "hooks/sleephook.h"
#include "reader/crypt.h"

namespace acioemu {
    extern bool ICCA_DEVICE_HACK;

    class ICCADevice : public ACIODeviceEmu {
    private:
        bool type_new;
        bool flip_order;
        std::thread *keypad_thread;
        std::mutex keypad_mutex;
        uint8_t **cards;
        time_t *cards_time;
        uint8_t *status;
        bool *accept;
        bool *hold;
        uint8_t *keydown;
        uint16_t *keypad;
        bool **keypad_last;
        uint8_t *keypad_capture;
        std::optional<Crypt> *crypt;
        uint8_t *counter;

    public:
        explicit ICCADevice(bool flip_order, bool keypad_thread, uint8_t node_count);
        ~ICCADevice() override;

        bool parse_msg(MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) override;

        void update_card(int unit);
        void update_keypad(int unit, bool update_edge);
        void update_status(int unit);
    };
}
