#include "p4io.h"
#include "util/logging.h"
#include "util/detour.h"
#include "misc/eamuse.h"
#include "acio/icca/icca.h"
#include "rawinput/rawinput.h"
#include "io.h"


namespace games::otoca {

    // button mapping
    static size_t MAPPING[] {
            ~0u,
            Buttons::Test,
            Buttons::Service,
            Buttons::CoinMech,
            ~0u,
            Buttons::LeverUp,
            Buttons::LeverDown,
            Buttons::LeverLeft,
            Buttons::LeverRight,
            Buttons::ButtonLeft,
            Buttons::ButtonRight,
            ~0u,
            ~0u,
    };
    static uint8_t INPUT_STATE[std::size(MAPPING)] {};
    static uint8_t *CARD_DATA = nullptr;

    static int __cdecl P4io_Boot_step() {
        return 1;
    }

    static int __cdecl P4ioInputIsOn(int input_no) {
        if (input_no < (int) std::size(MAPPING)) {
            return INPUT_STATE[input_no] > 0 && INPUT_STATE[input_no] < 3;
        }
        return 0;
    }

    static bool __cdecl P4ioInputIsOnTrg(int input_no) {
        if (input_no < (int) std::size(MAPPING)) {
            return INPUT_STATE[input_no] == 1;
        }
        return 0;
    }

    static bool __cdecl P4ioInputIsOffTrg(int input_no) {
        if (input_no < (int) std::size(MAPPING)) {
            return INPUT_STATE[input_no] == 3;
        }
        return 0;
    }

    static void __cdecl P4ioInput_Polling() {
        auto &buttons = get_buttons();

        for (size_t i = 0; i < std::size(MAPPING); i++) {
            if (MAPPING[i] != ~0u) {

                // reset release event
                if (INPUT_STATE[i] == 3) {
                    INPUT_STATE[i] = 0;
                }

                // apply new button state
                auto state = GameAPI::Buttons::getState(RI_MGR, buttons.at(MAPPING[i]));
                if (state && INPUT_STATE[i] < 2) {
                    INPUT_STATE[i]++;
                } else if (!state && INPUT_STATE[i] > 0 && INPUT_STATE[i] < 3) {
                    INPUT_STATE[i] = 3;
                }
            }
        }
    }

    static int __cdecl P4ioOutputPolling(uint32_t data) {
        auto &lights = get_lights();

        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::LeftButton), (data & 1) ? 1.f : 0.f);
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::RightButton), (data & 2) ? 1.f : 0.f);

        RI_MGR->devices_flush_output();

        return 0;
    }

    static int __cdecl P4ioCoinCount() {
        return eamuse_coin_get_stock();
    }

    static bool __cdecl P4ioInputIsICCardHold() {
        bool kb_insert_press = false;

        // eamio keypress
        kb_insert_press |= static_cast<bool>(eamuse_get_keypad_state(0) & (1 << EAM_IO_INSERT));

        // check for card
        if (CARD_DATA == nullptr && (eamuse_card_insert_consume(1, 0) || kb_insert_press)) {
            auto card = new uint8_t[8];
            if (!eamuse_get_card(1, 0, card)) {

                // invalid card found
                delete[] card;

            } else {
                CARD_DATA = card;
            }
        }

        // check if hold
        return CARD_DATA != nullptr;
    }

    static void __cdecl P4ioInputIsICCardRead(uint8_t *data) {
        if (CARD_DATA != nullptr) {
            memcpy(data, CARD_DATA, 8);
        }
    }

    void p4io_hook() {
        detour::iat_try("?P4io_Boot_step@@YA?AW4P4IO_STATUS@@XZ", P4io_Boot_step);
        detour::iat_try("?P4ioInputIsOn@@YAHH@Z", P4ioInputIsOn);
        detour::iat_try("?P4ioInputIsOnTrg@@YAHH@Z", P4ioInputIsOnTrg);
        detour::iat_try("?P4ioInputIsOffTrg@@YAHH@Z", P4ioInputIsOffTrg);
        detour::iat_try("?P4ioInput_Polling@@YAXXZ", P4ioInput_Polling);
        detour::iat_try("?P4ioOutputPolling@@YAHU_P4IO_POLLING_DATA_t@@@Z", P4ioOutputPolling);
        detour::iat_try("?P4ioCoinCount@@YAHXZ", P4ioCoinCount);
        detour::iat_try("?P4ioInputIsICCardHold@@YA_NXZ", (void*) P4ioInputIsICCardHold);
        detour::iat_try("?P4ioInputIsICCardRead@@YAXPAU_P4IO_ICCARDDATA@@@Z", P4ioInputIsICCardRead);
    }
}
