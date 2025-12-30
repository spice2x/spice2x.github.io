#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::gitadora {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            Coin,
            Headphone,
            GuitarP1Start,
            GuitarP1Up,
            GuitarP1Down,
            GuitarP1Left,
            GuitarP1Right,
            GuitarP1Help,
            GuitarP1Effect1,
            GuitarP1Effect2,
            GuitarP1Effect3,
            GuitarP1EffectPedal,
            GuitarP1ButtonExtra1,
            GuitarP1ButtonExtra2,
            GuitarP1PickUp,
            GuitarP1PickDown,
            GuitarP1R,
            GuitarP1G,
            GuitarP1B,
            GuitarP1Y,
            GuitarP1P,
            GuitarP1KnobUp,
            GuitarP1KnobDown,
            GuitarP1WailUp,
            GuitarP1WailDown,
            GuitarP2Start,
            GuitarP2Up,
            GuitarP2Down,
            GuitarP2Left,
            GuitarP2Right,
            GuitarP2Help,
            GuitarP2Effect1,
            GuitarP2Effect2,
            GuitarP2Effect3,
            GuitarP2EffectPedal,
            GuitarP2ButtonExtra1,
            GuitarP2ButtonExtra2,
            GuitarP2PickUp,
            GuitarP2PickDown,
            GuitarP2R,
            GuitarP2G,
            GuitarP2B,
            GuitarP2Y,
            GuitarP2P,
            GuitarP2KnobUp,
            GuitarP2KnobDown,
            GuitarP2WailUp,
            GuitarP2WailDown,
            DrumStart,
            DrumUp,
            DrumDown,
            DrumLeft,
            DrumRight,
            DrumHelp,
            DrumButtonExtra1,
            DrumButtonExtra2,
            // note: this is the order they appear in the test menu
            DrumHiHat,
            DrumHiHatClosed,
            DrumHiHatHalfOpen,
            DrumSnare,
            DrumHiTom,
            DrumLowTom,
            DrumRightCymbal,
            DrumBassPedal,
            DrumLeftCymbal,
            DrumLeftPedal,
            DrumFloorTom
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            GuitarP1WailX,
            GuitarP1WailY,
            GuitarP1WailZ,
            GuitarP1Knob,
            GuitarP2WailX,
            GuitarP2WailY,
            GuitarP2WailZ,
            GuitarP2Knob
        };
    }

    // all lights in correct order
    namespace Lights {
        typedef enum {
            // vibration motors (DX only)
            GuitarP1Motor,
            GuitarP2Motor,

            // P1
            P1MenuStart,
            // DX only
            P1MenuUpDown,
            P1MenuLeftRight,
            P1MenuHelp,

            // P2
            P2MenuStart,
            // DX only
            P2MenuUpDown,
            P2MenuLeftRight,
            P2MenuHelp,

            // drums
            DrumLeftCymbal,
            DrumHiHat,
            DrumSnare,
            DrumHighTom,
            DrumLowTom,
            DrumFloorTom,
            DrumRightCymbal,

            // drum woofer
            DrumWooferR,
            DrumWooferG,
            DrumWooferB,

            // drum stage, DX only
            DrumStageR,
            DrumStageG,
            DrumStageB,

            // main spotlights, DX only
            SpotFrontLeft,
            SpotFrontRight,
            SpotCenterLeft,
            SpotCenterRight,

            // drum rear spotlights, DX only
            DrumSpotRearLeft,
            DrumSpotRearRight,

            // guitar center lower RGB, DX only
            GuitarLowerLeftR,
            GuitarLowerLeftG,
            GuitarLowerLeftB,
            GuitarLowerRightR,
            GuitarLowerRightG,
            GuitarLowerRightB,

            // guitar left speaker, DX only
            GuitarLeftSpeakerUpperR,
            GuitarLeftSpeakerUpperG,
            GuitarLeftSpeakerUpperB,
            GuitarLeftSpeakerMidUpLeftR,
            GuitarLeftSpeakerMidUpLeftG,
            GuitarLeftSpeakerMidUpLeftB,
            GuitarLeftSpeakerMidUpRightR,
            GuitarLeftSpeakerMidUpRightG,
            GuitarLeftSpeakerMidUpRightB,
            GuitarLeftSpeakerMidLowLeftR,
            GuitarLeftSpeakerMidLowLeftG,
            GuitarLeftSpeakerMidLowLeftB,
            GuitarLeftSpeakerMidLowRightR,
            GuitarLeftSpeakerMidLowRightG,
            GuitarLeftSpeakerMidLowRightB,
            GuitarLeftSpeakerLowerR,
            GuitarLeftSpeakerLowerG,
            GuitarLeftSpeakerLowerB,

            // guitar right speaker, DX only
            GuitarRightSpeakerUpperR,
            GuitarRightSpeakerUpperG,
            GuitarRightSpeakerUpperB,
            GuitarRightSpeakerMidUpLeftR,
            GuitarRightSpeakerMidUpLeftG,
            GuitarRightSpeakerMidUpLeftB,
            GuitarRightSpeakerMidUpRightR,
            GuitarRightSpeakerMidUpRightG,
            GuitarRightSpeakerMidUpRightB,
            GuitarRightSpeakerMidLowLeftR,
            GuitarRightSpeakerMidLowLeftG,
            GuitarRightSpeakerMidLowLeftB,
            GuitarRightSpeakerMidLowRightR,
            GuitarRightSpeakerMidLowRightG,
            GuitarRightSpeakerMidLowRightB,
            GuitarRightSpeakerLowerR,
            GuitarRightSpeakerLowerG,
            GuitarRightSpeakerLowerB
        } gitadora_lights_t;
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
