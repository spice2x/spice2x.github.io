#pragma once

#include "rawinput/rawinput.h"
#include "overlay/window.h"

namespace overlay::windows {

    struct MIDIData {
        rawinput::Device *device;
        uint8_t cmd, ch;
        uint8_t b1, b2;
    };

    class MIDIWindow : public Window {
    public:

        MIDIWindow(SpiceOverlay *overlay);
        ~MIDIWindow() override;

        void build_content() override;
        static void midi_hook(void *user, rawinput::Device *device,
                uint8_t cmd, uint8_t ch, uint8_t b1, uint8_t b2);

    private:

        std::vector<MIDIData> midi_data;
        bool autoscroll = true;
        bool autoscroll_apply = false;
    };
}
