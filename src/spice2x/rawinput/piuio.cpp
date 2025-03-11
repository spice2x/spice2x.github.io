#include "piuio.h"
#include <iostream>
#include <thread>

using namespace std;

// it's 2019 and I'm writing code to avoid issues with Windows XP.
static HINSTANCE WINUSB_INSTANCE = nullptr;
static std::string WINUSB_NAME = "winusb.dll";

static std::string WinUsb_InitializeStr = "WinUsb_Initialize";
typedef BOOL(__stdcall * WinUsb_Initialize_t)(
    _In_  HANDLE DeviceHandle,
    _Out_ PWINUSB_INTERFACE_HANDLE InterfaceHandle
);

static std::string WinUsb_ControlTransferStr = "WinUsb_ControlTransfer";
typedef BOOL(__stdcall * WinUsb_ControlTransfer_t)(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  WINUSB_SETUP_PACKET SetupPacket,
    _Out_writes_bytes_to_opt_(BufferLength, *LengthTransferred) PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_  LPOVERLAPPED Overlapped
);

static WinUsb_ControlTransfer_t pWinUsb_ControlTransfer = nullptr;
static WinUsb_Initialize_t pWinUsb_Initialize = nullptr;

const string rawinput::PIUIO::LIGHT_NAMES[rawinput::PIUIO::PIUIO_MAX_NUM_OF_LIGHTS] = {
    "Unknown",
    "Unknown",
    "P2 Up",
    "P2 Down",
    "P2 Left",
    "P2 Right",
    "P2 Center",
    "Unknown",
    "Unknown",
    "Unknown",
    "Neon",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "P1 Up",
    "P1 Down",
    "P1 Left",
    "P1 Right",
    "P1 Center",
    "Marquee Upper Left",
    "Marquee Lower Right",
    "Marquee Lower Left",
    "Marquee Upper Right",
    "USB Enable",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown"
};

rawinput::PIUIO::PIUIO(Device *device) {
    this->device = device;
    button_state = 0;
    light_data = 0;
    current_sensor = 0;
    prev_timeout = 0;
}

bool rawinput::PIUIO::LoadWinUSBFunctions() {
    static bool functions_loaded = false;

    if (functions_loaded)
        return true;

    // see if we even have winusb available.
    WINUSB_INSTANCE = libutils::try_library(WINUSB_NAME);
    
    // if windows didn't load it just now, then it's not on the system.
    if (WINUSB_INSTANCE != nullptr) {

        // load winusb functions
        pWinUsb_Initialize = (WinUsb_Initialize_t) libutils::get_proc(
                WINUSB_INSTANCE, WinUsb_InitializeStr.c_str());
        pWinUsb_ControlTransfer = (WinUsb_ControlTransfer_t) libutils::get_proc(
                WINUSB_INSTANCE, WinUsb_ControlTransferStr.c_str());

        // make sure they actually loaded.
        if(pWinUsb_Initialize != nullptr && pWinUsb_ControlTransfer!= nullptr)
            functions_loaded = true;
    } else {
        log_warning("piuio", "Could not load \'{}\', skipping...", WINUSB_NAME);
    }

    return functions_loaded;
}

PSP_DEVICE_INTERFACE_DETAIL_DATA rawinput::PIUIO::GetDevicePath() {

    // modified from https://forum.vellemanprojects.eu/t/k8090-how-to-identify-which-com-port-it-is-connected-to/3810
    // https://www.velleman.eu/images/tmp/usbfind.c

    HDEVINFO                         hDevInfo;
    SP_DEVICE_INTERFACE_DATA         DevIntfData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DevIntfDetailData;
    SP_DEVINFO_DATA                  DevData;

    DWORD dwSize, dwMemberIdx;

    hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, NULL, 0, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if (hDevInfo != INVALID_HANDLE_VALUE) {
        DevIntfData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        dwMemberIdx = 0;

        SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE,
            dwMemberIdx, &DevIntfData);

        while (GetLastError() != ERROR_NO_MORE_ITEMS) {
            DevData.cbSize = sizeof(DevData);

            SetupDiGetDeviceInterfaceDetail(
                hDevInfo, &DevIntfData, NULL, 0, &dwSize, NULL);

            DevIntfDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
            DevIntfDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &DevIntfData,
                DevIntfDetailData, dwSize, &dwSize, &DevData)) {
                if (strstr(DevIntfDetailData->DevicePath, VENDOR_PROD_ID) != 0) {
                    return DevIntfDetailData;
                }
            }

            HeapFree(GetProcessHeap(), 0, DevIntfDetailData);

            // continue looping
            SetupDiEnumDeviceInterfaces(
                hDevInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE, ++dwMemberIdx, &DevIntfData);
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    return 0;
}

bool rawinput::PIUIO::IsPressed(int iKey) {
    if (iKey < PIUIO_MAX_NUM_OF_INPUTS) {

        // logic high is pressed, logic low is released.
        return button_state & (1 << iKey);
    }

    return false;
}

bool rawinput::PIUIO::WasPressed(int iKey) {
    if (iKey < PIUIO_MAX_NUM_OF_INPUTS)
        return prev_button_state & (1 << iKey);
    return false;
}

void rawinput::PIUIO::SetUSBPortState(bool bState) {
    SetLight(USB_ENABLE_BIT, bState);
}

void rawinput::PIUIO::SetLight(int iLight, bool bState) {
    if (iLight < PIUIO_MAX_NUM_OF_LIGHTS) {

        // turn on a light by setting the bit, turn it off by clearing it.
        if (bState)
            light_data |= 1 << iLight;
        else
            light_data &= ~(1 << iLight);
    }
}

bool rawinput::PIUIO::Open() {

    // ensure the libs are loaded
    // if we can't load them, then we shouldn't exist.
    if (!LoadWinUSBFunctions())
        return false;

    bool bResult = false;
    PSP_DEVICE_INTERFACE_DETAIL_DATA dev_detail;

    dev_detail = GetDevicePath();

    if (dev_detail == 0) {
        HeapFree(GetProcessHeap(), 0, dev_detail);
        return false;
    }

    device_handle = CreateFile(dev_detail->DevicePath,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    // no longer need dev_detail
    HeapFree(GetProcessHeap(), 0, dev_detail);

    if (device_handle == INVALID_HANDLE_VALUE)
        return false;

    if (pWinUsb_Initialize != nullptr)
        bResult = pWinUsb_Initialize(device_handle, &win_usb_handle);

    if (!bResult) {
        CloseHandle(device_handle);
        return false;
    }

    return bResult;
}

uint32_t rawinput::PIUIO::Read() {

    WINUSB_SETUP_PACKET SetupPacket;
    ZeroMemory(&SetupPacket, sizeof(WINUSB_SETUP_PACKET));
    ULONG cbSent = 0;
    bool bResult = false;

    // create the setup packet
    SetupPacket.RequestType = 0x40 | 0x80; // EndPoint In, RequestType Vendor
    SetupPacket.Request = PIUIO_CTL_REQ;
    SetupPacket.Value = 0;
    SetupPacket.Index = 0;
    SetupPacket.Length = PACKET_SIZE_BYTES;

    // ensure we loaded the pointer
    unsigned char buffer[PACKET_SIZE_BYTES] {};
    if (pWinUsb_ControlTransfer != nullptr)
        bResult = pWinUsb_ControlTransfer(win_usb_handle, SetupPacket, buffer, PACKET_SIZE_BYTES, &cbSent, 0);

    if (!bResult)
        log_warning("piuio", "read error");

    return Deserialize(buffer);
}

bool rawinput::PIUIO::Write(uint32_t p_Data) {

    WINUSB_SETUP_PACKET SetupPacket;
    ZeroMemory(&SetupPacket, sizeof(WINUSB_SETUP_PACKET));
    ULONG cbSent = 0;
    bool bResult = false;

    // create the setup packet
    SetupPacket.RequestType = 0x40; //EndPoint Out, RequestType Vendor
    SetupPacket.Request = PIUIO_CTL_REQ;
    SetupPacket.Value = 0;
    SetupPacket.Index = 0;
    SetupPacket.Length = PACKET_SIZE_BYTES;

    // ensure we loaded the pointer
    if (pWinUsb_ControlTransfer != nullptr) {
        bResult = pWinUsb_ControlTransfer(
                win_usb_handle,
                SetupPacket,
                Serialize(light_data),
                PACKET_SIZE_BYTES,
                &cbSent,
                0
        );
    }

    if (!bResult)
        log_warning("piuio", "write error");

    return bResult;
}

unsigned char * rawinput::PIUIO::Serialize(uint32_t p_Data) {
    static unsigned char buffer[PACKET_SIZE_BYTES];

    buffer[0] = p_Data;
    buffer[1] = p_Data >> 8;
    buffer[2] = p_Data >> 16;
    buffer[3] = p_Data >> 24;

    // these bits don't matter for the piuio
    buffer[4] = 0xFF;
    buffer[5] = 0xFF;
    buffer[6] = 0xFF;
    buffer[7] = 0xFF;

    return buffer;
}

uint32_t rawinput::PIUIO::Deserialize(unsigned char * p_Data) {
    uint32_t result;

    result = p_Data[0];
    result |= p_Data[1] << 8;
    result |= p_Data[2] << 16;
    result |= p_Data[3] << 24;

    return result;
}

void rawinput::PIUIO::IOThread() {
    while (is_connected)
        UpdateSingleButtonState();
}

void rawinput::PIUIO::UpdateSingleButtonState() {

    // write a set of sensors to request
    light_data &= 0xFFFCFFFC;
    light_data |= (current_sensor | (current_sensor << 16));

    // request this set of sensors
    // this also updates the light status
    this->Write(light_data);

    // read from this set of sensors
    input_data[current_sensor] = this->Read();

    // piuio opens high; invert the input
    input_data[current_sensor] = ~input_data[current_sensor];

    // update high values ASAP, that's when the button is pressed.
    // see: https://github.com/djpohly/piuio#usb-protocol-and-multiplexer
    auto button_state_new = button_state | input_data[current_sensor];
    if (button_state != button_state_new) {
        this->device->mutex->lock();
        button_state |= button_state_new;
        this->device->updated = true;
        this->device->mutex->unlock();
    }

    if (current_sensor >= SENSOR_NUM - 1) {
        uint32_t temp_btn_state = 0;

        // we have done a full sensor arrangement, let's see if the panel has been pulled away from.
        // combine all the input.
        for (int i = 0; i < 4; ++i)
            temp_btn_state |= input_data[i];
        
        // only store every 255th previous value since we are polling very quickly.
        // only used by spicecfg to see if a user pressed a button, so not very important.
        // unsigned ints rollover
        prev_timeout++;
        if (prev_timeout == 0)
            prev_button_state = button_state;

        if (button_state != temp_btn_state) {
            this->device->mutex->lock();
            button_state = temp_btn_state;
            this->device->updated = true;
            this->device->mutex->unlock();
        }
        current_sensor = 0;
    } else {
        current_sensor++;
    }
}

bool rawinput::PIUIO::Init() {
    is_connected = Open();

    if (is_connected) {

        // force the USB lines to be always on.
        SetUSBPortState(true);

        IOThreadObject = new std::thread(&rawinput::PIUIO::IOThread, this);
    }

    return is_connected;
}

bool rawinput::PIUIO::Stop() {
    is_connected = false;

    if (IOThreadObject != NULL) {
        IOThreadObject->join();
    }

    return true;
}

rawinput::PIUIO::~PIUIO() {
    Stop();
}
