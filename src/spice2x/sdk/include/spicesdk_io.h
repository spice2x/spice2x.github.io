#pragma once
#ifndef SPICE_SDK_IO_H
#define SPICE_SDK_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bishi Bashi Channel

typedef enum SPICE_SDK_BUTTON_BBC {
    BBC_Button_Service,
    BBC_Button_Test,
    BBC_Button_P1_R,
    BBC_Button_P1_G,
    BBC_Button_P1_B,
    BBC_Button_P1_DiskMinus,
    BBC_Button_P1_DiskPlus,
    BBC_Button_P1_DiskSlowdown,
    BBC_Button_P2_R,
    BBC_Button_P2_G,
    BBC_Button_P2_B,
    BBC_Button_P2_DiskMinus,
    BBC_Button_P2_DiskPlus,
    BBC_Button_P2_DiskSlowdown,
    BBC_Button_P3_R,
    BBC_Button_P3_G,
    BBC_Button_P3_B,
    BBC_Button_P3_DiskMinus,
    BBC_Button_P3_DiskPlus,
    BBC_Button_P3_DiskSlowdown,
    BBC_Button_P4_R,
    BBC_Button_P4_G,
    BBC_Button_P4_B,
    BBC_Button_P4_DiskMinus,
    BBC_Button_P4_DiskPlus,
    BBC_Button_P4_DiskSlowdown,
} SPICE_SDK_BUTTON_BBC;

typedef enum SPICE_SDK_ANALOG_BBC {
    BBC_Analog_P1_Disk,
    BBC_Analog_P2_Disk,
    BBC_Analog_P3_Disk,
    BBC_Analog_P4_Disk,
} SPICE_SDK_ANALOG_BBC;

typedef enum SPICE_SDK_LIGHT_BBC {
    BBC_Light_P1_R,
    BBC_Light_P1_G,
    BBC_Light_P1_B,
    BBC_Light_P1_DISC_R,
    BBC_Light_P1_DISC_G,
    BBC_Light_P1_DISC_B,
    BBC_Light_P2_R,
    BBC_Light_P2_G,
    BBC_Light_P2_B,
    BBC_Light_P2_DISC_R,
    BBC_Light_P2_DISC_G,
    BBC_Light_P2_DISC_B,
    BBC_Light_P3_R,
    BBC_Light_P3_G,
    BBC_Light_P3_B,
    BBC_Light_P3_DISC_R,
    BBC_Light_P3_DISC_G,
    BBC_Light_P3_DISC_B,
    BBC_Light_P4_R,
    BBC_Light_P4_G,
    BBC_Light_P4_B,
    BBC_Light_P4_DISC_R,
    BBC_Light_P4_DISC_G,
    BBC_Light_P4_DISC_B,
} SPICE_SDK_LIGHT_BBC;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SPICE_SDK_IO_H