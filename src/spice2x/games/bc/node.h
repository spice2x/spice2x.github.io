#pragma once

#include <cstdint>

#include "io.h"

#include "acio2emu/firmware/bi2x.h"

#include "misc/eamuse.h"
#include "util/logging.h"

namespace games::bc {
    class TBSNode : public acio2emu::firmware::BI2XNode {
    private:
        uint8_t coins_ = 0;

    public:
        void read_firmware_version(std::vector<uint8_t> &buffer) {
            static constexpr uint8_t ver[] = {
                0x0D, 0x06, 0x00, 0x01, 
                0x00, 0x01, 0x02, 0x08, 
                0x42, 0x49, 0x32, 0x58, 
                0x01, 0x94, 0xF1, 0x8E,
                0x00, 0x00, 0x01, 0x0B, 
                0x00, 0x00, 0x00, 0x00, 
                0x00, 0x00, 0x00, 0x00,
                0xA8, 0x24, 0x8E, 0xE2,
            };

            buffer.insert(buffer.end(), ver, &ver[sizeof(ver)]);
        }

        bool read_input(std::vector<uint8_t> &buffer) {
            auto &buttons = get_buttons();
            auto &analogs = get_analogs();
            coins_ += eamuse_coin_consume_stock();
            
            buffer.reserve(buffer.size() + 10);
            buffer.push_back(0);
            buffer.push_back(0);
            buffer.push_back(coins_);

            uint8_t b = 0;
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Test])) {
                b |= 1;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Service])) {
                b |= (1 << 2);
            }
            buffer.push_back(b);

            buffer.push_back(0);

            int16_t joy = INT16_MAX;
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Left])) {
                joy += INT16_MAX;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Right])) {
                joy -= INT16_MAX;
            }
            if (analogs[Analogs::StickX].isSet()) {
                joy = 65535 - (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::StickX]) * 65535);
            }
            buffer.push_back(static_cast<uint8_t>(joy >> 8));
            buffer.push_back(static_cast<uint8_t>(joy));

            joy = INT16_MAX;
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Down])) {
                joy += INT16_MAX;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Up])) {
                joy -= INT16_MAX;
            }
            if (analogs[Analogs::StickY].isSet()) {
                joy = 65535 - (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analogs[Analogs::StickY]) * 65535);
            }
            buffer.push_back(static_cast<uint8_t>(joy >> 8));
            buffer.push_back(static_cast<uint8_t>(joy));

            b = 0;
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::JoystickButton])) {
                b |= 1;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Trigger1])) {
                b |= (1 << 1);
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Trigger2])) {
                b |= (1 << 2);
            } 
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button1])) {
                b |= (1 << 3);
            } 
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button2])) {
                b |= (1 << 4);
            } 
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button3])) {
                b |= (1 << 5);
            } 
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::Button4])) {
                b |= (1 << 6);
            } 
            buffer.push_back(b);

            return true;
        }

        int write_output(std::span<const uint8_t> buffer) {
            return 8;
        }
    };
}