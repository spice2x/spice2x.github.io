#pragma once

#include <thread>
#include <windows.h>
#include <winusb.h>
#include <setupapi.h>
#include <string>
#include "util/libutils.h"
#include "util/logging.h"
#include "device.h"

// This is the GUID for the USB device class
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

#pragma comment (lib, "setupapi.lib")
#pragma comment (lib , "winusb.lib" )

namespace rawinput {

    class PIUIO
    {
    public:

        static const size_t PACKET_SIZE_BYTES = 8;
        static const size_t SENSOR_NUM = 4;
        static const size_t USB_ENABLE_BIT = 27;
        static const UCHAR PIUIO_CTL_REQ = 0xAE;
        const char *VENDOR_PROD_ID = "vid_0547&pid_1002";

        const static std::string LIGHT_NAMES[];
        static const int PIUIO_MAX_NUM_OF_LIGHTS = 32;
        static const int PIUIO_MAX_NUM_OF_INPUTS = 32;

        bool is_connected;
        Device *device;

        bool IsPressed(int iKey);
        bool WasPressed(int iKey);
        void SetLight(int iLight, bool bState);

        PIUIO(Device* device);
        virtual ~PIUIO();

        bool Init();
        bool Stop();

        void UpdateSingleButtonState();

        void SetUSBPortState(bool bState);

    private:
        bool LoadWinUSBFunctions();

        uint32_t light_data;
        uint32_t input_data[SENSOR_NUM];
        uint8_t current_sensor;
        uint32_t button_state, prev_button_state;

        uint8_t prev_timeout;

        LPCWSTR device_path;
        WINUSB_INTERFACE_HANDLE win_usb_handle = INVALID_HANDLE_VALUE;
        HANDLE device_handle = INVALID_HANDLE_VALUE;

        bool Open();
        PSP_DEVICE_INTERFACE_DETAIL_DATA GetDevicePath();

        void IOThread();

        uint32_t Read();
        bool Write(uint32_t p_Data);

        unsigned char * Serialize(uint32_t p_Data);
        uint32_t Deserialize(unsigned char * p_Data);

        std::thread* IOThreadObject;
    };
}
