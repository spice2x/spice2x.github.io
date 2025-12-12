#include "rawinput.h"

#include <cstdarg>
#include <utility>
#include <vector>

#include <objbase.h>
#include <setupapi.h>

#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

#include "piuio.h"
#include "touch.h"
#include "acio/mdxf/mdxf_poll.h"

extern "C" {
#include "external/usbhidusage/usb-hid-usage.h"
}

namespace rawinput {

    // settings
    bool NOLEGACY = false;
    uint8_t HID_LIGHT_BRIGHTNESS = 100; // 100%
    bool ENABLE_SMX_STAGE = false;
    bool ENABLE_SMX_DEDICAB = false;
    int TOUCHSCREEN_RANGE_X = 0;
    int TOUCHSCREEN_RANGE_Y = 0;
    bool DUMP_HID_DEVICES_TO_LOG = false;
    bool NAIVE_REQUIRE_FOCUS = true;
    bool RAWINPUT_REQUIRE_FOCUS = false;

    // set this to something slightly longer than 16.67ms (60Hz) so that I/O can pick it up
    // this may need to be adjusted for each game in the future if there is a game that polls less
    // often than 60Hz
    uint32_t MIDI_NOTE_SUSTAIN = 20;

    static MidiNoteAlgorithm MIDI_NOTE_ALGORITHM = MidiNoteAlgorithm::V2;
}

rawinput::MidiNoteAlgorithm rawinput::get_midi_algorithm() {
    return rawinput::MIDI_NOTE_ALGORITHM;
}

void rawinput::set_midi_algorithm(rawinput::MidiNoteAlgorithm new_algo) {
    rawinput::MIDI_NOTE_ALGORITHM = new_algo;
    std::string s = "Unknown";
    switch (new_algo) {
        case rawinput::MidiNoteAlgorithm::LEGACY:
            s = "legacy";
            break;
        case rawinput::MidiNoteAlgorithm::V2:
            s = "v2";
            break;
        case rawinput::MidiNoteAlgorithm::V2_DRUM:
            s = "v2_drum";
            break;
        default:
            log_info("rawinput", "assert failed: invalid midi algorithm");
            break;
    }
    log_info("rawinput", "using MIDI algorithm: {}", s);
}

rawinput::RawInputManager::RawInputManager() {

    // create input window and load in devices
    this->input_hwnd_create();
    this->devices_reload();

    // start flushing thread
    this->output_start();
    this->flush_start();

    // now create the hotplug manager on that window
    this->hotplug = new HotplugManager(this, this->input_hwnd);
}

rawinput::RawInputManager::~RawInputManager() {
    this->stop();

    log_info("rawinput", "destructor done");
}

void rawinput::RawInputManager::stop() {
    if (this->hotplug) {

        // remove hotplug
        delete this->hotplug;
        this->hotplug = nullptr;
    }

    // unregister device messages
    this->devices_unregister();

    // stop threads
    this->flush_stop();
    this->output_stop();

    // destruct all devices and input window
    this->devices_destruct();
    this->input_hwnd_destroy();
}

void rawinput::RawInputManager::input_hwnd_create() {

    // register window class
    this->input_hwnd_class.cbSize = sizeof(WNDCLASSEX);
    this->input_hwnd_class.hInstance = GetModuleHandle(nullptr);
    this->input_hwnd_class.lpfnWndProc = rawinput::RawInputManager::input_wnd_proc;
    this->input_hwnd_class.lpszClassName = "SpiceTools Input";
    if (!RegisterClassEx(&this->input_hwnd_class)) {
        log_warning("rawinput", "could not register input class");
        return;
    }

    // create input thread
    this->input_thread = new std::thread([this]() {

        // increase priority
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        // create window
        this->input_hwnd = CreateWindowExA(
                0,
                this->input_hwnd_class.lpszClassName,
                "SpiceTools Input",
                0, 0, 0, 0, 0,
                nullptr,
                nullptr,
                this->input_hwnd_class.hInstance,
                this
        );

        // window loop
        MSG msg;
        while (GetMessage(&msg, this->input_hwnd, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DestroyWindow(this->input_hwnd);
        this->input_hwnd = nullptr;
    });

    // wait for window creation being done
    while (!this->input_hwnd) {
        Sleep(1);
    }
}

void rawinput::RawInputManager::input_hwnd_destroy() {
    if (this->input_hwnd) {

        // post close and join
        PostMessage(this->input_hwnd, WM_CLOSE, 0, 0);
    }

    if (this->input_thread) {
        this->input_thread->join();

        // delete thread
        delete this->input_thread;
        this->input_thread = nullptr;
    }

    // unregister the window class
    UnregisterClass(this->input_hwnd_class.lpszClassName, this->input_hwnd_class.hInstance);
}

void rawinput::RawInputManager::devices_reload() {
    this->devices_destruct();
    log_info("rawinput", "reloading devices");

    // scan for devices
    this->devices_scan_rawinput();
    this->devices_scan_midi();
    this->devices_scan_piuio();

    if (ENABLE_SMX_STAGE) {
        this->devices_scan_smxstage();
    }

    if (ENABLE_SMX_DEDICAB) {
        this->devices_scan_smxdedicab();
    }

    // check for LIT Board
    sextet_register("COM54", "LIT Board", false);

    // register devices
    this->devices_register();
}

void rawinput::RawInputManager::devices_scan_rawinput(const std::string &device_name) {

    // get number of devices
    UINT device_no = 0;
    if (GetRawInputDeviceList(nullptr, &device_no, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) {
        return;
    }
    if (!device_no) {
        return;
    }

    // get device list
    std::shared_ptr<RAWINPUTDEVICELIST> device_list(new RAWINPUTDEVICELIST[device_no]);
    GetRawInputDeviceList(device_list.get(), &device_no, sizeof(RAWINPUTDEVICELIST));
    if (!device_no) {
        return;
    }

    // iterate devices
    for (UINT device_cur_index = 0; device_cur_index < device_no; device_cur_index++) {
        auto device = &device_list.get()[device_cur_index];
        if (device_name.length() == 0) {
            devices_scan_rawinput(device, false);
        } else if (device_name == rawinput::RawInputManager::rawinput_get_device_name(device->hDevice)) {
            log_info("rawinput", "scanning device: {}", device_name);
            devices_scan_rawinput(device, true);
        }
    }
}

void rawinput::RawInputManager::devices_scan_rawinput(RAWINPUTDEVICELIST *device, bool log) {

    // get device name
    std::string device_name = rawinput_get_device_name(device->hDevice);
    if (device_name.empty()) {
        return;
    }

    // extract information out of name
    auto device_info = rawinput::RawInputManager::get_device_info(device_name);
    auto device_description = rawinput::RawInputManager::rawinput_get_device_description(device_info, device_name);

    // get device information
    RID_DEVICE_INFO rawinput_device_info {};
    rawinput_device_info.cbSize = sizeof(RID_DEVICE_INFO);
    UINT device_info_size = rawinput_device_info.cbSize;
    if (GetRawInputDeviceInfo(device->hDevice, RIDI_DEVICEINFO, &rawinput_device_info, &device_info_size) == (UINT) -1) {
        return;
    }

    // check for duplicate handle
    size_t unique_id = devices.size() + 1;
    for (size_t i = 0; i < this->devices.size(); i++) {
        if (this->devices[i].name == device_name) {
            unique_id = i;
            break;
        }
    }

    // build device
    Device new_device {};
    new_device.id = unique_id;
    new_device.handle = device->hDevice;
    new_device.name = device_name;
    new_device.desc = device_description;
    new_device.info = device_info;
    new_device.mutex = new std::mutex();
    new_device.mutex_out = new std::mutex();
    new_device.input_time = get_performance_seconds();
    switch (device->dwType) {
        case RIM_TYPEMOUSE:
            new_device.type = MOUSE;
            new_device.mouseInfo = new DeviceMouseInfo();
            break;
        case RIM_TYPEKEYBOARD:
            new_device.type = KEYBOARD;
            new_device.keyboardInfo = new DeviceKeyboardInfo();
            break;
        case RIM_TYPEHID: {
            new_device.type = HID;
            HIDDriver hid_driver = HIDDriver::Default;
            HIDD_ATTRIBUTES hid_attributes {};

            // get preparsed information
            UINT preparsed_size = 0;
            if (GetRawInputDeviceInfo(
                    device->hDevice,
                    RIDI_PREPARSEDDATA,
                    nullptr,
                    &preparsed_size) == (UINT) -1)
            {
                return;
            }
            if (!preparsed_size) {
                return;
            }

            // allocate buffer
            auto preparsed_data = util::make_unique_plain<_HIDP_PREPARSED_DATA>(preparsed_size);

            if (GetRawInputDeviceInfo(
                    device->hDevice,
                    RIDI_PREPARSEDDATA,
                    preparsed_data.get(),
                    &preparsed_size) == (UINT) -1)
            {
                return;
            }

            // get caps
            _HIDP_CAPS caps {};
            if (HidP_GetCaps(preparsed_data.get(), &caps) != HIDP_STATUS_SUCCESS) {
                return;
            }

            // skip vendor-specific devices
            //
            // In the case of the Corsair Vengeance K70 RGB, the get device manufacturer and product
            // string functions take 5 seconds each, which really delays the boot when it has three
            // vendor-specific devices. Luckily, those three vendor-specific devices have the usage page
            // appropriately set. This took Felix an hour to narrow down.
            auto hid_vid = rawinput_device_info.hid.dwVendorId;
            auto hid_pid = rawinput_device_info.hid.dwProductId;
            if ((caps.UsagePage >> 8) == 0xFF
            && !(hid_vid == 0xBEEF && hid_pid == 0x5730)) // allow Minimaid
            {
                if (DUMP_HID_DEVICES_TO_LOG) {
                    log_misc("rawinput", "skipping vendor-specific device, vid/pid 0x{:04x}:0x{:04x}", hid_vid, hid_pid);
                }
                return;
            }

            // get usage description
            std::string usage_name;
            {
                auto *usage_name_str = usb_hid_get_usage_text(caps.UsagePage, caps.Usage);
                if (usage_name_str) {
                    usage_name = usage_name_str;
                    free(usage_name_str);
                } else {
                    usage_name = fmt::format("Unknown (0x{:04x}:0x{:04x})", caps.UsagePage, caps.Usage);
                }
            }

            // get better device description
            HANDLE hid_handle = CreateFile(
                    device_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    0, nullptr);
            if (hid_handle != INVALID_HANDLE_VALUE) {

                // check manufacturer string
                std::string man_ws;
                wchar_t man_str_buffer[256] {};
                if (HidD_GetManufacturerString(hid_handle, man_str_buffer, sizeof(man_str_buffer))) {
                    man_ws = wchar_to_u8(man_str_buffer);
                }

                // get product string
                std::string prod_ws;
                wchar_t prod_str_buffer[256] {};
                if (HidD_GetProductString(hid_handle, prod_str_buffer, sizeof(prod_str_buffer))) {
                    prod_ws = wchar_to_u8(prod_str_buffer);
                }

                // For Bluetooth LE HID devices (e.g., newer Xbox controllers) HidD_GetProductString
                // and HidD_GetManufacturerString will return blank strings.
                // https://docs.microsoft.com/en-us/answers/questions/401236/hidd-getproductstring-with-ble-hid-device.html
                if (man_ws.empty() && prod_ws.empty()) {
                    prod_ws = device_description;
                }

                // build desc string
                std::string desc_ws;
                if (string_begins_with(prod_ws, man_ws)) {
                    desc_ws = prod_ws;
                } else {
                    desc_ws = man_ws;
                    if (!man_ws.empty() && !prod_ws.empty()) {
                        desc_ws += " ";
                    }
                    desc_ws += prod_ws;
                }
                new_device.desc = desc_ws;

                // get attributes
                if (!HidD_GetAttributes(hid_handle, &hid_attributes)) {
                    log_warning("rawinput", "failed to get HID device attributes for {}", device_name);
                }
            }

            // get button caps
            USHORT button_cap_length = caps.NumberInputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> button_cap_data(static_cast<size_t>(button_cap_length));
            if (button_cap_length > 0) {
                if (HidP_GetButtonCaps(HidP_Input, button_cap_data.data(), &button_cap_length,
                        preparsed_data.get()) != HIDP_STATUS_SUCCESS)
                {
                    return;
                }
            }
            std::vector<HIDP_BUTTON_CAPS> button_caps_list;
            std::vector<std::string> button_caps_names;
            std::vector<std::vector<bool>> button_states;
            std::vector<std::vector<double>> button_up, button_down;
            for (int button_cap_num = 0; button_cap_num < button_cap_length; button_cap_num++) {
                auto &button_caps = button_cap_data[button_cap_num];

                // fill out range fields so we don't have to care later on
                if (!button_caps.IsRange) {
                    button_caps.Range.UsageMin = button_caps.NotRange.Usage;
                    button_caps.Range.UsageMax = button_caps.NotRange.Usage;
                    button_caps.Range.DataIndexMin = button_caps.NotRange.DataIndex;
                    button_caps.Range.DataIndexMax = button_caps.NotRange.DataIndex;
                    button_caps.Range.DesignatorMin = button_caps.NotRange.DesignatorIndex;
                    button_caps.Range.DesignatorMax = button_caps.NotRange.DesignatorIndex;
                    button_caps.Range.StringMin = button_caps.NotRange.StringIndex;
                    button_caps.Range.StringMax = button_caps.NotRange.StringIndex;
                }

                int button_count = button_caps.Range.UsageMax - button_caps.Range.UsageMin + 1;

                // ignore bad ranges reported by bad devices
                if (button_count >= 0xffff) {
                    log_warning("rawinput", "skipping bad button cap range for device {}, range [{}, {}]",
                        device_name,
                        button_caps.Range.UsageMin,
                        button_caps.Range.UsageMax);
                    continue;
                }

                // fill vectors
                button_caps_list.emplace_back(button_caps);
                button_states.emplace_back(std::vector<bool>(static_cast<unsigned int>(button_count), false));
                button_up.emplace_back(std::vector<double>(static_cast<unsigned int>(button_count), 0.0));
                button_down.emplace_back(std::vector<double>(static_cast<unsigned int>(button_count), 0.0));

                // names
                for (USAGE usg = button_caps.Range.UsageMin; usg <= button_caps.Range.UsageMax; usg++) {
                    const char *name = usb_hid_get_usage_text(button_caps.UsagePage, usg);
                    button_caps_names.emplace_back(name ? name : "Button Control");
                    free((void *) name);
                }
            }

            // get button output caps
            USHORT button_output_cap_length = caps.NumberOutputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> button_output_cap_data(static_cast<size_t>(button_output_cap_length));
            if (button_output_cap_length > 0) {
                if (HidP_GetButtonCaps(HidP_Output, button_output_cap_data.data(), &button_output_cap_length,
                         preparsed_data.get()) != HIDP_STATUS_SUCCESS)
                {
                    return;
                }
            }
            std::vector<HIDP_BUTTON_CAPS> button_output_caps_list;
            std::vector<std::string> button_output_caps_names;
            std::vector<std::vector<bool>> button_output_states;
            for (int button_cap_num = 0; button_cap_num < button_output_cap_length; button_cap_num++) {
                auto &button_caps = button_output_cap_data[button_cap_num];

                // fill out range fields so we don't have to care later on
                if (!button_caps.IsRange) {
                    button_caps.Range.UsageMin = button_caps.NotRange.Usage;
                    button_caps.Range.UsageMax = button_caps.NotRange.Usage;
                    button_caps.Range.DataIndexMin = button_caps.NotRange.DataIndex;
                    button_caps.Range.DataIndexMax = button_caps.NotRange.DataIndex;
                    button_caps.Range.DesignatorMin = button_caps.NotRange.DesignatorIndex;
                    button_caps.Range.DesignatorMax = button_caps.NotRange.DesignatorIndex;
                    button_caps.Range.StringMin = button_caps.NotRange.StringIndex;
                    button_caps.Range.StringMax = button_caps.NotRange.StringIndex;
                }

                int button_count = button_caps.Range.UsageMax - button_caps.Range.UsageMin + 1;

                // ignore bad ranges reported by bad devices
                if (button_count >= 0xffff) {
                    log_warning("rawinput", "skipping bad button output cap range for device {}, range [{}, {}]",
                        device_name,
                        button_caps.Range.UsageMin,
                        button_caps.Range.UsageMax);
                    continue;
                }

                // fill vectors
                button_output_caps_list.emplace_back(button_caps);
                button_output_states.emplace_back(std::vector<bool>(button_count, false));

                // names
                for (USAGE usg = button_caps.Range.UsageMin; usg <= button_caps.Range.UsageMax; usg++) {

                    // check for custom name
                    wchar_t custom_name[256]{};
                    bool custom_name_set = false;
                    if (hid_handle != INVALID_HANDLE_VALUE) {

                        // get string index
                        ULONG string_index = 0;
                        if (button_caps.IsStringRange && button_caps.Range.StringMin != 0) {
                            string_index = button_caps.Range.StringMin + static_cast<ULONG>(button_output_caps_names.size());
                        }
                        else if (!button_caps.IsStringRange && button_caps.NotRange.StringIndex != 0) {
                            string_index = button_caps.NotRange.StringIndex;
                        }

                        // lookup string
                        if (string_index > 0 && HidD_GetIndexedString(
                            hid_handle,
                            string_index,
                            reinterpret_cast<void*>(custom_name),
                            sizeof(custom_name)))
                        {
                            custom_name_set = true;
                        }
                    }

                    // check if custom name is set
                    if (custom_name_set) {

                        // use custom name
                        button_output_caps_names.push_back(ws2s(std::wstring(custom_name)));

                    } else {

                        // lookup generic name
                        const char* name = usb_hid_get_usage_text(button_caps.UsagePage, usg);
                        button_output_caps_names.emplace_back(name ? name : "Button Control");
                        free((void*)name);
                    }
                }
            }

            /*
             * PacDrive LED driver board ("Ultimarc LED Controller")
             * It's HID descriptor is trash so we need to fix that
             */
            if (hid_attributes.VendorID == 0xD209 && (hid_attributes.ProductID & 0xFFF8) == 0x1500) {
                hid_driver = HIDDriver::PacDrive;

                // clear
                button_output_caps_list.clear();
                button_output_caps_names.clear();
                button_output_states.clear();

                // fake the output LEDs
                for (int i = 0; i < 16; i++) {

                    // create generic indicator caps
                    HIDP_BUTTON_CAPS fakeCaps {};
                    fakeCaps.Range.UsageMin = 0x4B;
                    fakeCaps.Range.UsageMax = 0x4B;

                    // add content to lists
                    button_output_caps_list.push_back(fakeCaps);
                    button_output_caps_names.push_back("LED " + to_string(i + 1));
                    button_output_states.emplace_back(std::vector<bool>(1, false));
                }
            }

            // get value caps
            USHORT value_cap_length = caps.NumberInputValueCaps;
            std::vector<HIDP_VALUE_CAPS> value_cap_data(value_cap_length);
            if (value_cap_length > 0) {
                if (HidP_GetValueCaps(HidP_Input, value_cap_data.data(), &value_cap_length,
                        preparsed_data.get()) != HIDP_STATUS_SUCCESS) {
                    return;
                }
            }
            std::vector<HIDP_VALUE_CAPS> value_caps_list;
            std::vector<std::string> value_caps_names;
            std::vector<float> value_states(value_cap_length, 0.5f);
            std::vector<LONG> value_states_raw(value_cap_length, 0);
            std::vector<float> bind_value_states(value_cap_length, 0.5f);
            
            // erratum for incorrect min/max reported by DJ DAO IIDX controller in HID-light mode
            // (2012 version with updateable firmware)
            bool is_dao_iidx =
                (hid_attributes.VendorID == 0x1CCF &&
                hid_attributes.ProductID == 0x8048 &&
                new_device.desc == "MY-POWER CO.,LTD. PS3Controller");

            for (int value_cap_num = 0; value_cap_num < value_cap_length; value_cap_num++) {
                auto &value_caps = value_cap_data[value_cap_num];

                if (is_dao_iidx && value_caps.BitSize == 8) { 
                    log_info("rawinput", "Override analog range for device {}. Replacing [{}, {}] with [{}, {}]",
                        new_device.name,
                        value_caps.LogicalMin, value_caps.LogicalMax,
                        0, 255);
                    value_caps.LogicalMin = 0;
                    value_caps.LogicalMax = 255;
                }

                // fix min and max values
                if (value_caps.BitSize > 0 && value_caps.BitSize <= sizeof(value_caps.LogicalMin) * 8) {
                    auto shift_size = sizeof(value_caps.LogicalMin) * 8 - value_caps.BitSize + 1;
                    auto mask = ((uint64_t) 1 << value_caps.BitSize) - 1;
                    value_caps.LogicalMin &= mask;
                    value_caps.LogicalMin <<= shift_size;
                    value_caps.LogicalMin >>= shift_size;
                    value_caps.LogicalMax &= mask;
                }

                // fix up hat switch to initially report as neutral position
                if (value_caps.UsagePage == 0x1 && value_caps.Range.UsageMin == 0x39) {
                    value_states[value_cap_num] = -1.f;
                }

                // add to list
                value_caps_list.emplace_back(value_caps);

                // names
                const char *name = usb_hid_get_usage_text(value_caps.UsagePage, value_caps.Range.UsageMin);
                value_caps_names.emplace_back(name ? name : "Analog Control");
                free((void *) name);
            }

            // get value output caps
            USHORT value_output_cap_length = caps.NumberOutputValueCaps;
            std::vector<HIDP_VALUE_CAPS> value_output_cap_data(static_cast<size_t>(value_output_cap_length));
            if (value_output_cap_length > 0) {
                if (HidP_GetValueCaps(HidP_Output, value_output_cap_data.data(), &value_output_cap_length,
                        preparsed_data.get()) != HIDP_STATUS_SUCCESS) {
                    return;
                }
            }
            std::vector<HIDP_VALUE_CAPS> value_output_caps_list;
            std::vector<std::string> value_output_caps_names;
            std::vector<float> value_output_states;
            for (size_t value_cap_num = 0; value_cap_num < value_output_cap_length; value_cap_num++) {
                auto &value_caps = value_output_cap_data[value_cap_num];

                // fix min and max values
                if (value_caps.BitSize > 0 && value_caps.BitSize <= sizeof(value_caps.LogicalMin) * 8) {
                    auto shift_size = sizeof(value_caps.LogicalMin) * 8 - value_caps.BitSize + 1;
                    auto mask = ((uint64_t) 1 << value_caps.BitSize) - 1;
                    value_caps.LogicalMin &= mask;
                    value_caps.LogicalMin <<= shift_size;
                    value_caps.LogicalMin >>= shift_size;
                    value_caps.LogicalMax &= mask;
                }

                // check if this is a range cap
                if (value_caps.IsRange) {

                    // add a cap for each value for range caps
                    auto usage_min = value_caps.Range.UsageMin;
                    auto usage_max = value_caps.Range.UsageMax;
                    for (auto usage = usage_min; usage <= usage_max; usage++) {

                        // add to list
                        value_caps.NotRange.Usage = usage;
                        value_output_caps_list.push_back(value_caps);
                        value_output_states.push_back(0.f);

                        // check for custom name
                        wchar_t custom_name[256] {};
                        bool custom_name_set = false;
                        if (hid_handle != INVALID_HANDLE_VALUE) {

                            // get string index
                            ULONG string_index = 0;
                            if (value_caps.IsStringRange && value_caps.Range.StringMin != 0) {
                                string_index = value_caps.Range.StringMin
                                        + static_cast<ULONG>(value_output_caps_list.size()) - 1;
                            } else if (!value_caps.IsStringRange && value_caps.NotRange.StringIndex != 0) {
                                string_index = value_caps.NotRange.StringIndex;
                            }

                            // lookup string
                            if (string_index > 0 && HidD_GetIndexedString(
                                    hid_handle,
                                    string_index,
                                    reinterpret_cast<void *>(custom_name),
                                    sizeof(custom_name))) {
                                custom_name_set = true;
                            }
                        }

                        // check if custom name is set
                        if (custom_name_set) {

                            // use custom name
                            value_output_caps_names.push_back(ws2s(std::wstring(custom_name)));

                        } else {

                            // lookup generic name
                            const char *name = usb_hid_get_usage_text(value_caps.UsagePage, usage);
                            value_output_caps_names.emplace_back(name ? name : "Value Output");
                            free((void *) name);
                        }
                    }
                } else {

                    // add to list
                    value_output_caps_list.emplace_back(value_caps);
                    value_output_states.push_back(0.f);

                    // check for custom name
                    wchar_t custom_name[256] {};
                    bool custom_name_set = false;
                    if (hid_handle != INVALID_HANDLE_VALUE) {

                        // get string index
                        ULONG string_index = 0;
                        if (!value_caps.IsStringRange && value_caps.NotRange.StringIndex != 0) {
                            string_index = value_caps.NotRange.StringIndex;
                        }

                        // lookup string
                        if (string_index > 0 && HidD_GetIndexedString(
                                hid_handle,
                                string_index,
                                reinterpret_cast<void *>(custom_name),
                                sizeof(custom_name))) {
                            custom_name_set = true;
                        }
                    }

                    // check if custom name is set
                    if (custom_name_set) {

                        // use custom name
                        value_output_caps_names.push_back(ws2s(std::wstring(custom_name)));

                    } else {

                        // lookup generic name
                        const char *name = usb_hid_get_usage_text(value_caps.UsagePage, value_caps.NotRange.Usage);
                        value_output_caps_names.emplace_back(name ? name : "Value Output");
                        free((void *) name);
                    }
                }
            }

            // generate HID info
            new_device.hidInfo = new DeviceHIDInfo();
            new_device.hidInfo->handle = hid_handle;
            new_device.hidInfo->caps = caps;
            new_device.hidInfo->attributes = hid_attributes;
            new_device.hidInfo->driver = hid_driver;
            new_device.hidInfo->usage_name = std::move(usage_name);
            new_device.hidInfo->preparsed_data = std::move(preparsed_data);
            new_device.hidInfo->preparsed_size = preparsed_size;
            new_device.hidInfo->button_caps_list = std::move(button_caps_list);
            new_device.hidInfo->button_caps_names = std::move(button_caps_names);
            new_device.hidInfo->button_output_caps_list = std::move(button_output_caps_list);
            new_device.hidInfo->button_output_caps_names = std::move(button_output_caps_names);
            new_device.hidInfo->value_caps_list = std::move(value_caps_list);
            new_device.hidInfo->value_caps_names = std::move(value_caps_names);
            new_device.hidInfo->value_output_caps_list = std::move(value_output_caps_list);
            new_device.hidInfo->value_output_caps_names = std::move(value_output_caps_names);
            new_device.hidInfo->button_states = std::move(button_states);
            new_device.hidInfo->button_up = std::move(button_up);
            new_device.hidInfo->button_down = std::move(button_down);
            new_device.hidInfo->button_output_states = std::move(button_output_states);
            new_device.hidInfo->value_states = std::move(value_states);
            new_device.hidInfo->value_states_raw = std::move(value_states_raw);
            new_device.hidInfo->value_output_states = std::move(value_output_states);
            new_device.hidInfo->bind_value_states = std::move(bind_value_states);

            // check for touch screen
            if (rawinput::touch::is_touchscreen(&new_device)) {
                rawinput::touch::enable(&new_device);
            }

            break;
        }
        default:
            return;
    }

    // overwrite device with the same handle
    for (auto &prev_device : this->devices) {
        if (prev_device.name == new_device.name) {
            log_info("rawinput", "overwriting existing device: {} / {}", new_device.desc, new_device.name);

            // carry over old device ID
            new_device.id = prev_device.id;

            // destruct and replace
            this->devices_destruct(&prev_device);
            prev_device = new_device;

            // notify change
            for (auto &cb : this->callback_change) {
                cb.f(cb.data, &prev_device);
            }

            return;
        }
    }

    // add device to list
    auto &added_device = this->devices.emplace_back(new_device);
    if (log) {
        log_info("rawinput", "added device: {} / {}", added_device.desc, added_device.name);
    }

    // notify add
    for (auto &cb : this->callback_add) {
        cb.f(cb.data, &added_device);
    }
}

void rawinput::RawInputManager::devices_scan_midi() {

    // add midi devices
    auto midi_device_count = midiInGetNumDevs();
    for (size_t midi_device_id = 0; midi_device_id < midi_device_count; midi_device_id++) {

        // get dev caps
        MIDIINCAPS midi_device_caps{};
        if (midiInGetDevCaps(midi_device_id, &midi_device_caps, sizeof(MIDIINCAPS)) != MMSYSERR_NOERROR) {
            continue;
        }

        // open device
        HMIDIIN midi_device_handle;
        if (midiInOpen(&midi_device_handle,
                       (UINT) midi_device_id,
                       (DWORD_PTR) &input_midi_proc,
                       (DWORD_PTR) this,
                       CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        {
            continue;
        }

        // start input
        if (midiInStart(midi_device_handle) != MMSYSERR_NOERROR) {
            continue;
        }

        // device info
        DeviceInfo midi_device_info {};

        // device midi info
        auto midi_device_midi_info = new DeviceMIDIInfo();
        midi_device_midi_info->states = std::vector<bool>(16 * 128);
        midi_device_midi_info->states_events = std::vector<uint8_t>(16 * 128);
        midi_device_midi_info->bind_states = std::vector<bool>(16 * 128);
        midi_device_midi_info->v2_last_on_time = std::vector<double>(16 * 128);
        midi_device_midi_info->v2_last_off_time = std::vector<double>(16 * 128);
        midi_device_midi_info->v2_velocity_threshold = std::vector<uint8_t>(16 * 128);
        midi_device_midi_info->v2_velocity_threshold_set_on_device = std::vector<bool>(16 * 128);
        midi_device_midi_info->velocity = std::vector<uint8_t>(16 * 128);
        midi_device_midi_info->freeze = false;
        midi_device_midi_info->controls_precision = std::vector<uint16_t>(16 * 32);
        midi_device_midi_info->controls_precision_bind = std::vector<uint16_t>(16 * 32);
        midi_device_midi_info->controls_precision_msb = std::vector<bool>(16 * 32);
        midi_device_midi_info->controls_precision_lsb = std::vector<bool>(16 * 32);
        midi_device_midi_info->controls_precision_set = std::vector<bool>(16 * 32);
        midi_device_midi_info->controls_single = std::vector<uint8_t>(16 * 44);
        midi_device_midi_info->controls_single_bind = std::vector<uint8_t>(16 * 44);
        midi_device_midi_info->controls_single_set = std::vector<bool>(16 * 44);
        midi_device_midi_info->controls_onoff = std::vector<bool>(16 * 6);
        midi_device_midi_info->controls_onoff_bind = std::vector<bool>(16 * 6);
        midi_device_midi_info->controls_onoff_set = std::vector<bool>(16 * 6);
        midi_device_midi_info->v2_controls_onoff_last_on_time = std::vector<double>(16 * 6);
        midi_device_midi_info->v2_controls_onoff_last_off_time = std::vector<double>(16 * 6);
        midi_device_midi_info->pitch_bend = std::vector<int16_t>(16 * 6);
        midi_device_midi_info->pitch_bend_set = std::vector<bool>(16 * 6);

        // build identifier for MIDI
        // ;MIDI; format is now set in stone (in other parts of the code base and in the config xml file)
        // so it should never be changed
        std::ostringstream midi_identifier;
        midi_identifier << ";" << "MIDI";
        midi_identifier << ";" << midi_device_id;
        midi_identifier << ";" << midi_device_caps.szPname;
        midi_identifier << ";" << midi_device_caps.wMid;
        midi_identifier << ";" << midi_device_caps.wPid;

        // build device
        Device midi_device {};
        midi_device.id = devices.size() + 1;
        midi_device.type = MIDI;
        midi_device.handle = midi_device_handle;
        midi_device.name = midi_identifier.str();
        midi_device.desc = to_string(midi_device_caps.szPname);
        midi_device.info = midi_device_info;
        midi_device.mutex = new std::mutex();
        midi_device.mutex_out = new std::mutex();
        midi_device.midiInfo = midi_device_midi_info;

        // check for duplicate handle
        for (auto &device : this->devices) {
            if (device.name == midi_identifier.str()) {

                // carry over ID
                midi_device.id = device.id;

                // destruct and replace
                this->devices_destruct(&device);
                device = midi_device;

                // notify change
                for (auto &cb : this->callback_change) {
                    cb.f(cb.data, &device);
                }

                return;
            }
        }

        // add device to list
        auto &device = this->devices.emplace_back(midi_device);

        // notify add
        for (auto &cb : this->callback_add) {
            cb.f(cb.data, &device);
        }
    }
}

void rawinput::RawInputManager::devices_scan_piuio() {

    // add device to vector first so pointer is valid
    auto *new_piuio_device = new Device();
    new_piuio_device->type = PIUIO_DEVICE;
    new_piuio_device->name = "piuio";
    new_piuio_device->desc = "PIUIO";
    new_piuio_device->piuioDev = nullptr;
    new_piuio_device->mutex = new std::mutex();
    new_piuio_device->mutex_out = new std::mutex();

    // try to initialize
    auto &device = this->devices.emplace_back(*new_piuio_device);
    auto piuioDev = new PIUIO(&device);
    if (piuioDev->Init()) {

        // successful initialization
        device.piuioDev = piuioDev;

        // notify add
        for (auto &cb : this->callback_add) {
            cb.f(cb.data, &device);
        }
    } else {

        // remove device since connection failed
        this->devices.pop_back();
    }
}

void rawinput::RawInputManager::devices_scan_smxstage() {
    auto *new_smxstage_device = new Device();
    new_smxstage_device->type = SMX_STAGE;
    new_smxstage_device->name = "smxstage";
    new_smxstage_device->desc = "SMX Stage";
    new_smxstage_device->smxstageInfo = nullptr;
    new_smxstage_device->mutex = new std::mutex();
    new_smxstage_device->mutex_out = new std::mutex();

    auto &device = this->devices.emplace_back(*new_smxstage_device);
    auto smxstageInfo = new SmxStageDevice();
    if (smxstageInfo->Initialize()) {
        device.smxstageInfo = smxstageInfo;

        // notify add
        for (auto &cb : this->callback_add) {
            cb.f(cb.data, &device);
        }
    } else {
        // remove device since connection failed
        this->devices.pop_back();
    }
}

void rawinput::RawInputManager::devices_scan_smxdedicab() {
    auto *new_smxdedicab_device = new Device();
    new_smxdedicab_device->type = SMX_DEDICAB;
    new_smxdedicab_device->name = "smxdedicab";
    new_smxdedicab_device->desc = "SMX Dedicated Cabinet";
    new_smxdedicab_device->smxdedicabInfo = nullptr;
    new_smxdedicab_device->mutex = new std::mutex();
    new_smxdedicab_device->mutex_out = new std::mutex();

    auto &device = this->devices.emplace_back(*new_smxdedicab_device);
    auto smxdedicabInfo = new SmxDedicabDevice();
    if (smxdedicabInfo->Initialize()) {
        device.smxdedicabInfo = smxdedicabInfo;

        // notify add
        for (auto &cb : this->callback_add) {
            cb.f(cb.data, &device);
        }
    } else {
        // remove device since connection failed
        this->devices.pop_back();
    }
}

void rawinput::RawInputManager::flush_start() {

    // start flush thread
    if (this->flush_thread == nullptr) {
        this->flush_thread_running = true;
        this->flush_thread = new std::thread([this] {
            while (this->flush_thread_running) {

                /*
                 * Write output report all ~500ms so DAO IIDX boards (and probably more) don't go back
                 * to button based lighting.
                 */
                this->devices_flush_output(false);
                Sleep(495);
            }
        });
    }
}

void rawinput::RawInputManager::flush_stop() {

    // set stop flag
    this->flush_thread_running = false;

    // check if thread is set
    if (this->flush_thread) {

        // join and kill
        this->flush_thread->join();
        delete this->flush_thread;

        // unset thread
        this->flush_thread = nullptr;
    }
}

void rawinput::RawInputManager::output_start() {

    // start thread if required
    if (!output_thread) {
        output_thread_running = true;
        output_thread = new std::thread([this] {
            std::unique_lock<std::mutex> lock(output_thread_m);
            while (output_thread_running) {

                // wait for CV
                output_thread_cv.wait(lock, [this] {
                    return output_thread_ready;
                });

                // check for exit
                if (!output_thread_running) {
                    break;
                }

                // iterate all devices
                do {
                    output_thread_ready = false;
                    for (auto &device : this->devices) {

                        // write output
                        device_write_output(&device, true);
                    }
                } while (output_thread_ready);
            }
        });
    }
}

void rawinput::RawInputManager::output_stop() {

    // stop output thread
    this->output_thread_running = false;
    if (this->output_thread) {
        this->output_thread_m.lock();
        this->output_thread_ready = true;
        this->output_thread_m.unlock();
        this->output_thread_cv.notify_all();
        this->output_thread->join();
        delete this->output_thread;
        this->output_thread = nullptr;
    }
}

std::string rawinput::RawInputManager::rawinput_get_device_name(HANDLE hDevice) {

    // get device name length
    UINT device_name_len = 0;
    if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICENAME, nullptr, &device_name_len) == (UINT) -1) {
        return "";
    }

    // allocate buffer
    auto device_name = std::make_unique<char[]>(device_name_len);

    // get device name (but it is actually the path)
    if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICENAME, device_name.get(), &device_name_len) == (UINT) -1) {
        return "";
    }
    if (device_name_len < 4) {
        return "";
    }

    // the infamous XP fix
    // see http://stackoverflow.com/questions/10798798
    device_name[1] = '\\'; //

    // build string
    return std::string(device_name.get());
}

std::string rawinput::RawInputManager::rawinput_get_device_description(const rawinput::DeviceInfo &info,
                                                                       const std::string &device_name) {

    // yes this whole motherfucker is just for the device name - gotta <3 microsoft
    std::string device_description;
    HDEVINFO devinfo = SetupDiGetClassDevs(&info.guid, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    SP_DEVINFO_DATA devinfo_data{};
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i1 = 0; SetupDiEnumDeviceInfo(devinfo, i1, &devinfo_data); i1++) {
        SP_DEVICE_INTERFACE_DATA i_data{};
        i_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        for (DWORD i2 = 0; SetupDiEnumDeviceInterfaces(devinfo, &devinfo_data, &info.guid, i2, &i_data); i2++) {

            // get device path
            DWORD detail_data_size = 0;
            if (SetupDiGetDeviceInterfaceDetail(devinfo, &i_data, nullptr, 0, &detail_data_size, nullptr)) {
                continue;
            }
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                continue;
            }

            // allocate buffer
            std::unique_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> detail_data(
                reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA *>(new uint8_t[detail_data_size])
            );
            detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (!SetupDiGetDeviceInterfaceDetail(devinfo, &i_data, detail_data.get(), detail_data_size,
                                                 nullptr, nullptr))
            {
                continue;
            }

            std::string device_path(detail_data->DevicePath);

            // the XP fix again
            if (device_path.length() > 1) {
                device_path[1] = '\\';
            }

            // check if this is our device (must be case insensitive)
            if (_stricmp(device_path.c_str(), device_name.c_str()) == 0) {

                // get property
                DWORD desc_size = 0;
                if (SetupDiGetDeviceRegistryPropertyW(
                        devinfo, &devinfo_data, SPDRP_DEVICEDESC, nullptr, nullptr, 0, &desc_size))
                {
                    continue;
                }
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                    continue;
                }

                // allocate buffer
                auto desc_data = std::make_unique<BYTE[]>(desc_size);

                if (!SetupDiGetDeviceRegistryPropertyW(
                        devinfo, &devinfo_data, SPDRP_DEVICEDESC, nullptr, desc_data.get(), desc_size, nullptr))
                {
                    continue;
                }

                // kthxbye
                device_description = wchar_to_u8(reinterpret_cast<PWCHAR>(desc_data.get()));
            }
        }
    }

    // kill it with fire
    SetupDiDestroyDeviceInfoList(devinfo);

    // some descriptions are empty - especially using WINE
    if (device_description.empty()) {
        device_description = device_name;
    }

    // alias
    if (device_description == R"(\\?\WINE_MOUSE)") {
        device_description = "WINE Mouse";
    } else if (device_description == R"(\\?\WINE_KEYBOARD)") {
        device_description = "WINE Keyboard";
    }

    // return result
    return device_description;
}

void rawinput::RawInputManager::sextet_register(const std::string &port_name, const std::string &alias,
        bool warn) {

    // check for any sextet-stream devices
    Device device {};
    device.type = SEXTET_OUTPUT;
    device.name = "sextet_" + port_name;
    device.desc = alias + " (" + port_name + ")";
    device.sextetInfo = new rawinput::SextetDevice(R"(\\.\)" + port_name);
    device.mutex = new std::mutex();
    device.mutex_out = new std::mutex();

    // try to connect
    if (device.sextetInfo->connect()) {

        // successful connection
        this->devices.emplace_back(device);

        // notify add
        for (auto &cb : this->callback_add) {
            cb.f(cb.data, &this->devices.back());
        }
    } else if (warn) {
        log_warning("rawinput", "unable to connect to {} on {}", alias, port_name);
    }
}

void rawinput::RawInputManager::devices_remove(const std::string &name) {

    // iterate devices
    for (auto &device : this->devices) {

        // check if name matches
        if (device.name == name) {

            // remove device
            this->devices_destruct(&device);
            return;
        }
    }
}

void rawinput::RawInputManager::devices_register() {

    // check input window
    if (!this->input_hwnd) {
        log_warning("rawinput", "trying to register devices without input window");
        return;
    }

    // register keyboard
    RAWINPUTDEVICE keyboard_device{};
    if (rawinput::NOLEGACY) {

        // this prevents win/media/special key events to get sent to the game window
        keyboard_device.dwFlags = RIDEV_NOLEGACY | RIDEV_INPUTSINK;

    } else {
        keyboard_device.dwFlags = RIDEV_INPUTSINK;
    }
    keyboard_device.usUsagePage = 1;
    keyboard_device.usUsage = 0x06;
    keyboard_device.hwndTarget = this->input_hwnd;
    if (!RegisterRawInputDevices(&keyboard_device, 1, sizeof(keyboard_device))) {
        log_warning("rawinput", "failed to register keyboard events: {}", GetLastError());
    }

    // register keypad
    RAWINPUTDEVICE keypad_device{};
    keypad_device.dwFlags = RIDEV_INPUTSINK;
    keypad_device.usUsagePage = 1;
    keypad_device.usUsage = 0x07;
    keypad_device.hwndTarget = this->input_hwnd;
    if (!RegisterRawInputDevices(&keypad_device, 1, sizeof(keypad_device))) {
        log_warning("rawinput", "failed to register keypad events: {}", GetLastError());
    }

    // register mouse
    RAWINPUTDEVICE mouse_device{};
    mouse_device.dwFlags = RIDEV_INPUTSINK;
    mouse_device.usUsagePage = 1;
    mouse_device.usUsage = 0x02;
    mouse_device.hwndTarget = this->input_hwnd;
    if (!RegisterRawInputDevices(&mouse_device, 1, sizeof(mouse_device))) {
        log_warning("rawinput", "failed to register mouse events: {}", GetLastError());
    }

    // register joystick
    RAWINPUTDEVICE joystick_device{};
    joystick_device.dwFlags = RIDEV_INPUTSINK;
    joystick_device.usUsagePage = 1;
    joystick_device.usUsage = 0x04;
    joystick_device.hwndTarget = this->input_hwnd;
    if (!RegisterRawInputDevices(&joystick_device, 1, sizeof(joystick_device))) {
        log_warning("rawinput", "failed to register joystick events: {}", GetLastError());
    }

    // register gamepad
    RAWINPUTDEVICE gamepad_device{};
    gamepad_device.dwFlags = RIDEV_INPUTSINK;
    gamepad_device.usUsagePage = 1;
    gamepad_device.usUsage = 0x05;
    gamepad_device.hwndTarget = this->input_hwnd;
    if (!RegisterRawInputDevices(&gamepad_device, 1, sizeof(gamepad_device))) {
        log_warning("rawinput", "failed to register gamepad events: {}", GetLastError());
    }

    // register digitizer
    RAWINPUTDEVICE digitizer_device{};
    digitizer_device.dwFlags = RIDEV_PAGEONLY | RIDEV_INPUTSINK;
    digitizer_device.usUsagePage = 0x0D;
    digitizer_device.usUsage = 0x00;
    digitizer_device.hwndTarget = this->input_hwnd;
    if (!RegisterRawInputDevices(&digitizer_device, 1, sizeof(digitizer_device))) {
        log_warning("rawinput", "failed to register digitizer events: {}", GetLastError());
    }
}

void rawinput::RawInputManager::devices_unregister() {

    // unregister keyboard
    RAWINPUTDEVICE keyboard_device {};
    if (rawinput::NOLEGACY) {
        keyboard_device.dwFlags = RIDEV_NOLEGACY | RIDEV_INPUTSINK | RIDEV_REMOVE;
    } else {
        keyboard_device.dwFlags = RIDEV_INPUTSINK | RIDEV_REMOVE;
    }
    keyboard_device.usUsagePage = 1;
    keyboard_device.usUsage = 0x06;
    keyboard_device.hwndTarget = this->input_hwnd;
    RegisterRawInputDevices(&keyboard_device, 1, sizeof(keyboard_device));

    // unregister keypad
    RAWINPUTDEVICE keypad_device {};
    keypad_device.dwFlags = RIDEV_INPUTSINK | RIDEV_REMOVE;
    keypad_device.usUsagePage = 1;
    keypad_device.usUsage = 0x07;
    keypad_device.hwndTarget = this->input_hwnd;
    RegisterRawInputDevices(&keypad_device, 1, sizeof(keypad_device));

    // unregister mouse
    RAWINPUTDEVICE mouse_device {};
    mouse_device.dwFlags = RIDEV_INPUTSINK | RIDEV_REMOVE;
    mouse_device.usUsagePage = 1;
    mouse_device.usUsage = 0x02;
    mouse_device.hwndTarget = this->input_hwnd;
    RegisterRawInputDevices(&mouse_device, 1, sizeof(mouse_device));

    // unregister joystick
    RAWINPUTDEVICE joystick_device {};
    joystick_device.dwFlags = RIDEV_INPUTSINK | RIDEV_REMOVE;
    joystick_device.usUsagePage = 1;
    joystick_device.usUsage = 0x04;
    joystick_device.hwndTarget = this->input_hwnd;
    RegisterRawInputDevices(&joystick_device, 1, sizeof(joystick_device));

    // unregister gamepad
    RAWINPUTDEVICE gamepad_device {};
    gamepad_device.dwFlags = RIDEV_INPUTSINK | RIDEV_REMOVE;
    gamepad_device.usUsagePage = 1;
    gamepad_device.usUsage = 0x06;
    gamepad_device.hwndTarget = this->input_hwnd;
    RegisterRawInputDevices(&gamepad_device, 1, sizeof(gamepad_device));

    // unregister digitizer
    RAWINPUTDEVICE digitizer_device {};
    digitizer_device.dwFlags = RIDEV_PAGEONLY | RIDEV_INPUTSINK | RIDEV_REMOVE;
    digitizer_device.usUsagePage = 0x0D;
    digitizer_device.usUsage = 0x00;
    digitizer_device.hwndTarget = this->input_hwnd;
    RegisterRawInputDevices(&digitizer_device, 1, sizeof(digitizer_device));
}

void rawinput::RawInputManager::devices_destruct() {

    // check if there's something to destruct
    if (this->devices.empty()) {
        return;
    }

    // dispose devices
    log_info("rawinput", "disposing devices");
    for (auto &device : this->devices) {
        this->devices_destruct(&device, false);
        delete device.mutex;
    }

    // empty array
    this->devices.clear();
}

void rawinput::RawInputManager::devices_destruct(Device *device, bool log) {

    // check if destroyed
    if (device->type == DESTROYED) {
        return;
    }

    // optionally log
    if (log) {
        log_info("rawinput", "destroying device: {} / {}", device->desc, device->name);
    }

    // mark as destroyed
    auto device_type = device->type;
    device->type = DESTROYED;

    // notify change
    for (auto &cb : this->callback_change) {
        cb.f(cb.data, device);
    }

    /*
     * lock device
     * note: this is an exception to only locking devices when we acquire them from the list
     * callbacks could want to lock the mutex as well and it isn't recursive
     * this also means the device must be unlocked before calling this function
     */
    std::lock_guard<std::mutex> lock(*device->mutex);

    // close device handles
    switch (device_type) {
        case HID:
            if (device->hidInfo->handle != INVALID_HANDLE_VALUE) {
                CloseHandle(device->hidInfo->handle);
                device->hidInfo->handle = INVALID_HANDLE_VALUE;
            }
            break;
        case MIDI:
            midiInReset((HMIDIIN) device->handle);
            midiInClose((HMIDIIN) device->handle);
            break;
        case SEXTET_OUTPUT:
            device->sextetInfo->disconnect();
            break;
        default:
            break;
    }

    // clean up generic stuff
    delete device->mouseInfo;
    device->mouseInfo = nullptr;
    delete device->keyboardInfo;
    device->keyboardInfo = nullptr;
    delete device->hidInfo;
    device->hidInfo = nullptr;
    delete device->midiInfo;
    device->midiInfo = nullptr;
    delete device->sextetInfo;
    device->sextetInfo = nullptr;
    delete device->smxstageInfo;
    device->smxstageInfo = nullptr;
    delete device->smxdedicabInfo;
    device->smxdedicabInfo = nullptr;
    // TODO: check if mutex can be deleted
}

LRESULT CALLBACK rawinput::RawInputManager::input_wnd_proc(
        HWND hWnd, UINT msg, WPARAM wparam, LPARAM lParam) {

    // message switch
    switch (msg) {
        case WM_CREATE: {

            // save reference
            auto create_params = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_params->lpCreateParams));

            break;
        }
        case WM_INPUT: {

            // get reference
            auto ref = reinterpret_cast<RawInputManager *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

            // get raw input data
            UINT data_size = 0;
            if (GetRawInputData(
                    (HRAWINPUT) lParam,
                    RID_INPUT,
                    nullptr,
                    &data_size,
                    sizeof(RAWINPUTHEADER)) == (UINT) -1) {
                break;
            }
            if (!data_size) {
                break;
            }
            std::shared_ptr<RAWINPUT> data((RAWINPUT *) new char[data_size]{});
            if (GetRawInputData(
                    (HRAWINPUT) lParam,
                    RID_INPUT,
                    data.get(),
                    &data_size,
                    sizeof(RAWINPUTHEADER)) != data_size) {
                break;
            }

            // find device
            HANDLE device_handle = data->header.hDevice;
            for (auto &device : ref->devices_get()) {

                // skip if this is the wrong device
                if (device.handle != device_handle) {
                    continue;
                }

                // get input time
                double input_time = get_performance_seconds();

                // lock device
                device.mutex->lock();

                // update hz
                double diff_time = input_time - device.input_time;
                if (diff_time > 0.0001) {
                    device.input_hz = 1.f / diff_time;
                    device.input_hz_max = MAX(device.input_hz_max, device.input_hz);
                    device.input_time = input_time;
                }

                // check type
                switch (device.type) {
                    case DESTROYED:
                        log_warning("rawinput", "received input msg for destroyed device");
                        break;
                    case MOUSE: {

                        // get mouse data
                        auto data_mouse = data->data.mouse;

                        // save position
                        if (data_mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                            if (device.mouseInfo->pos_x != data_mouse.lLastX) {
                                device.updated = true;
                            }
                            device.mouseInfo->pos_x = data_mouse.lLastX;
                            if (device.mouseInfo->pos_y != data_mouse.lLastY) {
                                device.updated = true;
                            }
                            device.mouseInfo->pos_y = data_mouse.lLastY;
                        } else {
                            if (data_mouse.lLastX != 0 || data_mouse.lLastY != 0) {
                                device.updated = true;
                            }
                            device.mouseInfo->pos_x += data_mouse.lLastX;
                            device.mouseInfo->pos_y += data_mouse.lLastY;
                        }

                        // check buttons
                        if (data_mouse.usButtonFlags) {
                            auto &key_states = device.mouseInfo->key_states;
                            auto &key_up = device.mouseInfo->key_up;
                            auto &key_down = device.mouseInfo->key_down;
                            if (data_mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_LEFT] = true;
                                key_down[MOUSEBTN_LEFT] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_LEFT] = false;
                                key_up[MOUSEBTN_LEFT] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_RIGHT] = true;
                                key_down[MOUSEBTN_RIGHT] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_RIGHT] = false;
                                key_up[MOUSEBTN_RIGHT] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_MIDDLE] = true;
                                key_down[MOUSEBTN_MIDDLE] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_MIDDLE] = false;
                                key_up[MOUSEBTN_MIDDLE] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_1] = true;
                                key_down[MOUSEBTN_1] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_1] = false;
                                key_up[MOUSEBTN_1] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_2] = true;
                                key_down[MOUSEBTN_2] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_2] = false;
                                key_up[MOUSEBTN_2] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_3] = true;
                                key_down[MOUSEBTN_3] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_3] = false;
                                key_up[MOUSEBTN_3] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_4] = true;
                                key_down[MOUSEBTN_4] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_4] = false;
                                key_up[MOUSEBTN_4] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) {
                                device.updated = true;
                                key_states[MOUSEBTN_5] = true;
                                key_down[MOUSEBTN_5] = input_time;
                            }
                            if (data_mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) {
                                device.updated = true;
                                key_states[MOUSEBTN_5] = false;
                                key_up[MOUSEBTN_5] = input_time;
                            }
                        }

                        // check wheel
                        if (data_mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                            if ((short) data_mouse.usButtonData != 0) {
                                device.updated = true;
                            }
                            device.mouseInfo->pos_wheel += ((short) data_mouse.usButtonData) / WHEEL_DELTA;
                        }

                        break;
                    }
                    case KEYBOARD: {

                        // get keyboard data
                        auto &data_keyboard = data->data.keyboard;

                        // set index based on flags
                        int index = 0;
                        if (data_keyboard.Flags & RI_KEY_E0) {
                            index += 256;
                        }
                        if (data_keyboard.Flags & RI_KEY_E1) {
                            index += 512;
                        }

                        // check the funny exceptions
                        USHORT vkey = data_keyboard.VKey;
                        switch (index + vkey) {
                            case 17:
                                vkey = VK_LCONTROL;
                                break;
                            case 273:
                                vkey = VK_RCONTROL;
                                break;
                        }
                        switch (data_keyboard.MakeCode) {
                            case 42:
                                vkey = VK_LSHIFT;
                                break;
                            case 54:
                                vkey = VK_RSHIFT;
                                break;
                        }

                        // update key state
                        if (vkey < 255) {
                            bool state = (data_keyboard.Flags & RI_KEY_BREAK) == 0;
                            auto &cur_state = device.keyboardInfo->key_states[index + vkey];
                            if (!cur_state && state) {
                                cur_state = state;
                                device.updated = true;
                                device.keyboardInfo->key_down[index + vkey] = input_time;
                            } else if (cur_state && !state) {
                                cur_state = state;
                                device.updated = true;
                                device.keyboardInfo->key_up[index + vkey] = input_time;
                            }
                        }

                        break;
                    }
                    case HID: {

                        // get HID data
                        auto &data_hid = data->data.hid;

                        // buttons
                        for (size_t cap_num = 0; cap_num < device.hidInfo->button_caps_list.size(); cap_num++) {
                            auto &button_caps = device.hidInfo->button_caps_list[cap_num];
                            auto &button_states = device.hidInfo->button_states[cap_num];
                            auto &button_down = device.hidInfo->button_down[cap_num];
                            auto &button_up = device.hidInfo->button_up[cap_num];

                            // get button count
                            int button_count = button_caps.Range.UsageMax - button_caps.Range.UsageMin + 1;
                            if (button_count <= 0) {
                                continue;
                            }

                            // get usages
                            auto usages_length = static_cast<ULONG>(button_count);
                            std::vector<USAGE> usages(static_cast<size_t>(usages_length));
                            if (HidP_GetUsages(
                                    HidP_Input,
                                    button_caps.UsagePage,
                                    button_caps.LinkCollection,
                                    usages.data(),
                                    &usages_length,
                                    reinterpret_cast<PHIDP_PREPARSED_DATA>(device.hidInfo->preparsed_data.get()),
                                    reinterpret_cast<PCHAR>(data_hid.bRawData),
                                    data_hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
                                continue;
                            }

                            // update buttons
                            std::vector<bool> new_states(button_count);
                            for (ULONG usage_num = 0; usage_num < usages_length; usage_num++) {
                                USAGE usage = usages[usage_num] - button_caps.Range.UsageMin;

                                // guard against some buggy device sending an event for a usage below `UsageMin`
                                if (usage < button_count) {
                                    new_states[usage] = true;
                                }
                            }
                            for (int button_num = 0; button_num < button_count; button_num++) {
                                if (!new_states[button_num] && button_states[button_num]) {
                                    device.updated = true;
                                    button_states[button_num] = new_states[button_num];
                                    button_down[button_num] = input_time;
                                } else if (new_states[button_num] && !button_states[button_num]) {
                                    device.updated = true;
                                    button_states[button_num] = new_states[button_num];
                                    button_up[button_num] = input_time;
                                }
                            }
                        }

                        // analogs
                        for (auto cap_num = 0; cap_num < device.hidInfo->caps.NumberInputValueCaps; cap_num++) {
                            auto &value_caps = device.hidInfo->value_caps_list[cap_num];

                            // get value
                            LONG value_raw = 0;
                            if (HidP_GetUsageValue(
                                    HidP_Input,
                                    value_caps.UsagePage,
                                    value_caps.LinkCollection,
                                    value_caps.Range.UsageMin,
                                    reinterpret_cast<ULONG *>(&value_raw),
                                    reinterpret_cast<PHIDP_PREPARSED_DATA>(device.hidInfo->preparsed_data.get()),
                                    reinterpret_cast<CHAR *>(data_hid.bRawData),
                                    data_hid.dwSizeHid) != HIDP_STATUS_SUCCESS)
                            {
                                continue;
                            }

                            // get min and max
                            LONG value_min = value_caps.LogicalMin;
                            LONG value_max = value_caps.LogicalMax;

                            // fix sign bits for signed values
                            if (value_caps.LogicalMin < 0 &&
                                    value_caps.BitSize > 0 &&
                                    value_caps.BitSize <= sizeof(value_caps.LogicalMin) * 8) {
                                auto shift_size = sizeof(value_caps.LogicalMin) * 8 - value_caps.BitSize + 1;
                                value_raw <<= shift_size;
                                value_raw >>= shift_size;
                            }

                            float value;
                            // 0x1 == generic desktop, 0x39 == hat switch
                            if (value_caps.UsagePage == 0x1 && value_caps.Range.UsageMin == 0x39) {
                                if (value_min <= value_raw && value_raw <= value_max) {
                                    // scale to float; minimum valid value is UP, and increases in clockwise order
                                    value = (float) (value_raw - value_min) / (float) (value_max - value_min);
                                } else {
                                    // hat switches report an out-of-bounds value to indicate a neutral position, so it
                                    // needs special handling; here, we will use a negative value to indicate neutral
                                    value = -1.f;
                                }
                            } else {
                                // automatic calibration
                                if (value_raw < value_min) {
                                    value_caps.LogicalMin = value_raw;
                                    value_min = value_raw;
                                }
                                if (value_raw > value_max) {
                                    value_caps.LogicalMax = value_raw;
                                    value_max = value_raw;
                                }

                                // scale to float
                                value = (float) (value_raw - value_min) / (float) (value_max - value_min);
                            }

                            // store value
                            auto &cur_state = device.hidInfo->value_states[cap_num];
                            if (cur_state != value) {
                                device.updated = true;
                                cur_state = value;
                            }

                            // store raw value
                            auto &cur_raw_state = device.hidInfo->value_states_raw[cap_num];
                            if (cur_raw_state != value_raw) {
                                device.updated = true;
                                cur_raw_state = value_raw;
                            }
                        }

                        // touch screen
                        rawinput::touch::update_input(&device);

                        break;
                    }
                    default:
                        break;
                }

                // free device
                device.mutex->unlock();
				
				// update controller state ring buffers (DDR/MDXF)
				mdxf_poll();
            }

            // call the default window handler for cleanup
            DefWindowProc(hWnd, msg, wparam, lParam);

            // return zero to indicate the event was processed
            return 0;
        }
        case WM_DEVICECHANGE: {

            // call hotplug manager
            auto ref = reinterpret_cast<RawInputManager *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if (ref != nullptr && ref->hotplug != nullptr) {
                return ref->hotplug->WndProc(hWnd, msg, wparam, lParam);
            }

            break;
        }
        default:
            break;
    }

    // default
    return DefWindowProc(hWnd, msg, wparam, lParam);
}

void CALLBACK rawinput::RawInputManager::input_midi_proc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance,
                                                         DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    // get instance
    auto ri_mgr = reinterpret_cast<RawInputManager *>(dwInstance);

    // handle message
    switch (wMsg) {
        case MIM_OPEN:
        case MIM_CLOSE:
            break;
        case MIM_MOREDATA:
        case MIM_DATA: {

            // param mapping
            auto dwMidiMessage = dwParam1;
            //auto dwTimestamp = dwParam2;

            // message unpacking
            auto midi_status = LOBYTE(LOWORD(dwMidiMessage));
            auto midi_status_command = (midi_status & 0xF0u) >> 4u;
            auto midi_status_channel = (midi_status & 0x0Fu);
            auto midi_byte1 = HIBYTE(LOWORD(dwMidiMessage));
            auto midi_byte2 = LOBYTE(HIWORD(dwMidiMessage));

            // callbacks
            for (auto &callback : ri_mgr->callback_midi) {

                // find device
                for (auto &device : ri_mgr->devices_get()) {
                    if (device.type == MIDI && device.handle == hMidiIn) {

                        // call function
                        callback.f(callback.data, &device,
                                   midi_status_command, midi_status_channel,
                                   midi_byte1, midi_byte2);
                    }
                }
            }

            // skip unused messages types early for performance
            bool skip = false;
            switch (midi_status_command) {
                case 0xA: // POLYPHONIC PRESSURE
                case 0xC: // PROGRAM CHANGE
                case 0xD: // CHANNEL PRESSURE
                case 0xF: // SYSTEM EXCLUSIVE
                    skip = true;
                    break;
                default:
                    break;
            }
            if (skip) {
                break;
            }

            // find device
            for (auto &device : ri_mgr->devices_get()) {

                // filter non MIDI devices
                if (device.type != MIDI) {
                    continue;
                }

                // filter wrong handles
                if (device.handle != hMidiIn) {
                    continue;
                }

                // get input time
                auto input_time = get_performance_seconds();

                // lock device
                std::lock_guard<std::mutex> lock(*device.mutex);

                // update hz
                auto diff_time = input_time - device.input_time;
                if (diff_time > 0.0001) {
                    device.input_hz = 1.f / diff_time;
                    device.input_hz_max = MAX(device.input_hz_max, device.input_hz);
                    device.input_time = input_time;
                }

                // command logic
                switch (midi_status_command) {
                    case 0x8: { // NOTE OFF

                        // param mapping
                        const auto midi_note = midi_byte1 & 127u;

                        // log_misc("midi", "[{}] OFF", midi_note);

                        // get index
                        const auto midi_index = midi_status_channel * 128 + midi_note;
                        if (midi_index < 16 * 128) {
                            if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::LEGACY) {
                                // update velocity
                                device.midiInfo->velocity[midi_index] = 0;
                                // disable note
                                if (device.midiInfo->states_events[midi_index]) {
                                    device.midiInfo->states[midi_index] = false;
                                }
                                device.updated = true;
                            } else {
                                // v2 logic
                                // exactly the same as NOTE ON with 0 velocity
                                // velocity is kept; api will ignore it if button is not pressed
                                if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::V2) {
                                    device.midiInfo->v2_last_off_time[midi_index] = get_performance_milliseconds();
                                    device.updated = true;
                                }
                                // for v2_drum, NOTE ON is ignored
                            }
                        }

                        break;
                    }
                    case 0x9: { // NOTE ON

                        // param mapping
                        const auto midi_note = midi_byte1 & 127u;

                        // per MIDI spec, if NOTE ON is sent with 0 velocity, it's the same thing as NOTE OFF.
                        const auto midi_velocity = midi_byte2 & 127u;

                        // log_misc("midi", "[{}] ON v={}", midi_note, midi_velocity);

                        // get index
                        const auto midi_index = midi_status_channel * 128 + midi_note;
                        if (midi_index < 16 * 128) {
                            if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::LEGACY) {
                                // update velocity
                                device.midiInfo->velocity[midi_index] = (uint8_t) midi_velocity;

                                if (midi_velocity) {
                                    // update events (for legacy logic)
                                    // how does this work? see the comment in api.cpp around the check for
                                    // get_midi_algorithm() for an explanation

                                    // so currently it's meant to be turned on
                                    device.midiInfo->states[midi_index] = true;

                                    // if its already on just increase it by one to turn it off
                                    if (device.midiInfo->states_events[midi_index] % 2)
                                        device.midiInfo->states_events[midi_index]++;
                                    else
                                        device.midiInfo->states_events[midi_index] += 2;

                                } else if (!device.midiInfo->freeze) {
                                    // velocity 0 means turn it off
                                    device.midiInfo->states[midi_index] = false;
                                }
                                device.updated = true;

                            } else {
                                // v2 logic
                                const auto now = get_performance_milliseconds();
                                auto threshold = device.midiInfo->v2_velocity_threshold[midi_index];
                                // when device is frozen (binding is happening) ignore the velocity threshold
                                // this allows users to bind keys even if the midi note is set to high threshold at
                                // rawinput layer, either from a previous binding that was cleared, or existing binding
                                // for another button
                                if (device.midiInfo->freeze) {
                                    threshold = 0;
                                }
                                if (threshold < midi_velocity) {
                                    device.midiInfo->velocity[midi_index] = (uint8_t)midi_velocity;
                                    device.midiInfo->v2_last_on_time[midi_index] = now;

                                    // disable holds and release all notes immediately
                                    if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::V2_DRUM) {
                                        device.midiInfo->v2_last_off_time[midi_index] = now;
                                    }
                                    device.updated = true;
                                } else {
                                    if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::V2) {
                                        // insufficient velocity ON == exactly the same as NOTE OFF
                                        device.midiInfo->v2_last_off_time[midi_index] = now;
                                        device.updated = true;
                                    }
                                    // for v2_drum, NOTE ON with insufficient velocity is ignored
                                }
                            }
                        }

                        break;
                    }
                    case 0xA: // POLYPHONIC PRESSURE
                        break; // skipped above (!)
                    case 0xB: { // CONTROL CHANGE

                        // param mapping
                        auto midi_control = midi_byte1 & 127;
                        auto midi_value = midi_byte2 & 127u;

                        // get index
                        auto channel_offset = midi_status_channel * 128;
                        auto midi_index = channel_offset + midi_control;
                        if (midi_index < 16 * 128) {

                            // continuous controller MSB
                            if (midi_control >= 0x00 && midi_control <= 0x1F) {

                                // update index
                                midi_index = midi_status_channel * 32 + midi_control;
                                device.midiInfo->controls_precision_set[midi_index] = true;

                                // check if MSB wasn't sent yet
                                if (!device.midiInfo->controls_precision_msb[midi_index]) {
                                    device.midiInfo->controls_precision_msb[midi_index] = true;

                                    // move LSB value to actual position
                                    device.midiInfo->controls_precision[midi_index] >>= 7u;
                                }

                                // update MSB
                                auto tmp = device.midiInfo->controls_precision[midi_index];
                                tmp = (tmp & 127u) | midi_value << 7u;
                                if (!device.midiInfo->controls_precision_lsb[midi_index])
                                    tmp = (tmp & (127u << 7u)) | midi_value;
                                if (device.midiInfo->controls_precision[midi_index] != tmp) {
                                    device.midiInfo->controls_precision[midi_index] = tmp;
                                    device.updated = true;
                                }
                            }

                            // continuous controller LSB
                            else if (midi_control >= 0x20 && midi_control <= 0x3F) {

                                // update index
                                midi_index = midi_status_channel * 32 + midi_control - 0x20;
                                device.midiInfo->controls_precision_set[midi_index] = true;
                                device.midiInfo->controls_precision_lsb[midi_index] = true;

                                // check for MSB flag
                                if (device.midiInfo->controls_precision_msb[midi_index]) {

                                    // update LSB only
                                    auto tmp = device.midiInfo->controls_precision[midi_index];
                                    tmp &= 127u << 7u;
                                    tmp |= midi_value;
                                    if (device.midiInfo->controls_precision[midi_index] != tmp) {
                                        device.midiInfo->controls_precision[midi_index] = tmp;
                                        device.updated = true;
                                    }

                                } else {

                                    // cast to MSB
                                    if (device.midiInfo->controls_precision[midi_index] != midi_value << 7u) {
                                        device.midiInfo->controls_precision[midi_index] = midi_value << 7u | midi_value;
                                        device.updated = true;
                                    }
                                }
                            }

                            // on/off controls
                            else if (midi_control >= 0x40 && midi_control <= 0x45) {

                                // update index
                                midi_index = midi_status_channel * 6 + midi_control - 0x40;
                                device.midiInfo->controls_onoff_set[midi_index] = true;

                                // get on/off state
                                const auto onoff_state = midi_value >= 64;

                                // update device
                                if (MIDI_NOTE_ALGORITHM == MidiNoteAlgorithm::LEGACY) {
                                    if (device.midiInfo->controls_onoff[midi_index] != onoff_state) {
                                        device.midiInfo->controls_onoff[midi_index] = onoff_state;
                                        device.updated = true;
                                    }

                                } else {
                                    // v2 and v2_drum:
                                    //   unlike notes (drum pads), controls can send continuous ON signal
                                    //   therefore, check for rising and falling edges
                                    const auto now = get_performance_milliseconds();
                                    const auto previous_value = device.midiInfo->controls_onoff[midi_index];
                                    if (!previous_value && onoff_state) {
                                        device.midiInfo->v2_controls_onoff_last_on_time[midi_index] = now;
                                        device.updated = true;
                                    } else if (previous_value && !onoff_state) {
                                        device.midiInfo->v2_controls_onoff_last_off_time[midi_index] = now;
                                        device.updated = true;
                                    }

                                    device.midiInfo->controls_onoff[midi_index] = onoff_state;
                                }
                            }

                            // single byte controllers
                            else if (midi_control >= 0x46 && midi_control <= 0x5F) {

                                // update index
                                midi_index = midi_status_channel * 44 + midi_control - 0x46;
                                device.midiInfo->controls_single_set[midi_index] = true;

                                // update device
                                if (device.midiInfo->controls_single[midi_index] != midi_value) {
                                    device.midiInfo->controls_single[midi_index] = midi_value;
                                    device.updated = true;
                                }
                            }

                            // increment/decrement and parameter numbers
                            else if (midi_control >= 0x60 && midi_control <= 0x65) {
                                // skip
                            }

                            // undefined single-byte controllers
                            else if (midi_control >= 0x66 && midi_control <= 0x77) {

                                // update index
                                auto sbc_count = 0x5F - 0x46 + 1;
                                midi_index = midi_status_channel * 44 + midi_control - 0x66 + sbc_count;
                                device.midiInfo->controls_single_set[midi_index] = true;

                                // update device
                                if (device.midiInfo->controls_single[midi_index] != midi_value) {
                                    device.midiInfo->controls_single[midi_index] = midi_value;
                                    device.updated = true;
                                }
                            }

                            // channel mode messages
                            else if (midi_control >= 0x78 && midi_control <= 0x7F) {
                                switch (midi_control) {
                                    case 0x78: // all sound off
                                        break;
                                    case 0x79: { // reset all controllers
                                        for (int i = 0; i < 32; i++)
                                            device.midiInfo->controls_precision[midi_status_channel * 32 + i] = 0;
                                        for (int i = 0; i < 44; i++)
                                            device.midiInfo->controls_single[midi_status_channel * 44 + i] = 0;
                                        for (int i = 0; i < 6; i++) {
                                            const auto index = midi_status_channel * 6 + i;
                                            device.midiInfo->controls_onoff[index] = false;
                                            device.midiInfo->v2_controls_onoff_last_on_time[index] = 0;
                                            device.midiInfo->v2_controls_onoff_last_off_time[index] = 0;
                                        }
                                        device.updated = true;
                                        break;
                                    }
                                    case 0x7A: // local control on/off
                                        break;
                                    case 0x7B: // all notes off
                                    case 0x7C: // omni mode off + all notes off
                                    case 0x7D: // omni mode on + all notes off
                                    case 0x7E: // mono mode on + poly off + all notes off
                                    case 0x7F: // poly mode on + mono off + all notes off
                                        for (int i = 0; i < 128; i++) {
                                            // common
                                            device.midiInfo->velocity[channel_offset + i] = 0;
                                            device.midiInfo->bind_states[channel_offset + i] = false;

                                            // legacy
                                            device.midiInfo->states[channel_offset + i] = false;
                                            device.midiInfo->states_events[channel_offset + i] = 0;

                                            // v2
                                            device.midiInfo->v2_last_off_time[channel_offset + i] = 0.0;
                                            device.midiInfo->v2_last_on_time[channel_offset + i] = 0.0;
                                        }
                                        device.updated = true;
                                        break;
                                    default:
                                        break;
                                }
                                break;
                            }
                        }
                        break;
                    }
                    case 0xC: // PROGRAM CHANGE
                        break; // skipped above (!)
                    case 0xD: // CHANNEL PRESSURE
                        break; // skipped above (!)
                    case 0xE: { // PITCH BENDING

                        // raw values range from [0, 0x3FFF] (16383)
                        // build value, centered around zero [-8192, 8191]
                        int16_t value = ((midi_byte1) | (midi_byte2 << 7u)) - 0x2000;

                        // update device
                        if (device.midiInfo->pitch_bend[midi_status_channel] != value) {
                            device.midiInfo->pitch_bend[midi_status_channel] = value;
                            device.midiInfo->pitch_bend_set[midi_status_channel] = true;
                            device.updated = true;
                        }
                        break;
                    }
                    case 0xF: // SYSTEM EXCLUSIVE
                        break; // skipped above (!)
                    default:
                        break;
                }

                // don't iterate through the other devices
                break;
            }
            break;
        }
        case MIM_LONGDATA:
        case MIM_ERROR:
        case MIM_LONGERROR:
            break;
        default:
            break;
    }
}

void rawinput::RawInputManager::device_write_output(Device *device, bool only_updated) {

    // check if output is enabled
    if (!device->output_enabled) {
        return;
    }

    // check if output is pending
    if (only_updated && !device->output_pending) {
        return;
    }

    // lock device
    device->mutex_out->lock();

    // mark device as updated
    device->output_pending = false;

    // check device type
    switch (device->type) {
        case HID: {

            // get HID info
            auto hid = device->hidInfo;

            // check handle
            if (hid->handle == INVALID_HANDLE_VALUE) {
                break;
            }

            // check driver
            switch (hid->driver) {
                case HIDDriver::Default: {

                    // allocate report
                    CHAR *report_data = new CHAR[hid->caps.OutputReportByteLength] {};

                    // set buttons
                    for (size_t cap_no = 0; cap_no < hid->button_output_caps_list.size(); cap_no++) {
                        auto &button_cap = hid->button_output_caps_list[cap_no];
                        auto &button_state_list = hid->button_output_states[cap_no];

                        // determine which buttons to turn on
                        std::vector<USAGE> usage_list;
                        std::vector<USAGE> usage_off_list;
                        usage_list.reserve(button_state_list.size());
                        usage_off_list.reserve(button_state_list.size());
                        for (size_t state_no = 0; state_no < button_state_list.size(); state_no++) {
                            if (button_state_list[state_no]) {
                                usage_list.push_back(button_cap.Range.UsageMin + (USAGE) state_no);
                            } else {
                                usage_off_list.push_back(button_cap.Range.UsageMin + (USAGE) state_no);
                            }
                        }

                        // set the buttons
                        auto usage_list_length = (ULONG) usage_list.size();
                        while (HidP_SetButtons(
                                       HidP_Output,
                                       button_cap.UsagePage,
                                       button_cap.LinkCollection,
                                       &usage_list[0],
                                       &usage_list_length,
                                       reinterpret_cast<PHIDP_PREPARSED_DATA>(hid->preparsed_data.get()),
                                       report_data,
                                       hid->caps.OutputReportByteLength) == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {

                            // flush report
                            HidD_SetOutputReport(hid->handle, report_data, hid->caps.OutputReportByteLength);
                            memset(report_data, 0, hid->caps.OutputReportByteLength);
                        }

                        // clear the buttons
                        auto usage_off_list_length = (ULONG) usage_off_list.size();
                        while (HidP_UnsetButtons(
                                       HidP_Output,
                                       button_cap.UsagePage,
                                       button_cap.LinkCollection,
                                       &usage_off_list[0],
                                       &usage_off_list_length,
                                       reinterpret_cast<PHIDP_PREPARSED_DATA>(hid->preparsed_data.get()),
                                       report_data,
                                       hid->caps.OutputReportByteLength) == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {

                            // flush report
                            DWORD written_bytes = 0;
                            WriteFile(
                                    hid->handle,
                                    reinterpret_cast<void *>(report_data),
                                    hid->caps.OutputReportByteLength,
                                    &written_bytes,
                                    nullptr
                            );
                            memset(report_data, 0, hid->caps.OutputReportByteLength);
                        }
                    }

                    // set values
                    for (size_t cap_no = 0; cap_no < hid->value_output_caps_list.size(); cap_no++) {
                        auto &value_cap = hid->value_output_caps_list[cap_no];
                        auto &value_state = hid->value_output_states[cap_no];

                        // adjust output value per "brightness" setting
                        auto adjusted_value_state = value_state * HID_LIGHT_BRIGHTNESS / 100;

                        // build value
                        LONG usage_value = value_cap.LogicalMin +
                                           lroundf((value_cap.LogicalMax - value_cap.LogicalMin) * adjusted_value_state);
                        if (usage_value > value_cap.LogicalMax) {
                            usage_value = value_cap.LogicalMax;
                        } else if (usage_value < value_cap.LogicalMin) {
                            usage_value = value_cap.LogicalMin;
                        }

                        // set the state
                        while (HidP_SetUsageValue(
                                HidP_Output,
                                value_cap.UsagePage,
                                value_cap.LinkCollection,
                                value_cap.NotRange.Usage,
                                static_cast<ULONG>(usage_value),
                                reinterpret_cast<PHIDP_PREPARSED_DATA>(hid->preparsed_data.get()),
                                report_data,
                                hid->caps.OutputReportByteLength) == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {

                            // flush report
                            DWORD written_bytes = 0;
                            WriteFile(
                                    hid->handle,
                                    reinterpret_cast<void *>(report_data),
                                    hid->caps.OutputReportByteLength,
                                    &written_bytes,
                                    nullptr
                            );
                            memset(report_data, 0, hid->caps.OutputReportByteLength);
                        }
                    }

                    // MiniMaid Madness
                    if (hid->attributes.VendorID == 0xBEEF && hid->attributes.ProductID == 0x5730) {
                        if (hid->caps.OutputReportByteLength >= 8) {
                            /*
                             * MiniMaid HID Index Positions:
                             * 0: HID Report ID
                             * 1: EXT Values
                             * 2: Cabinet
                             * 3: Player 1
                             * 4: Player 2
                             * 5: Bass
                             * 6: Onboard LED Brightness
                             * 7: Keyboard Enable
                             * 8: Unused 'hax' variable
                             */

                            // put pads in proper mode
                            // bit 4 high is pad enable.
                            report_data[3] |= 0x10u; // P1
                            report_data[4] |= 0x10u; // P2

                            // enable keyboard flag
                            report_data[7] |= 0x01u;
                        }
                    }

                    // write final report
                    DWORD written_bytes = 0;
                    WriteFile(
                            hid->handle,
                            reinterpret_cast<void *>(report_data),
                            hid->caps.OutputReportByteLength,
                            &written_bytes,
                            nullptr
                    );

                    // delete report
                    delete[] report_data;

                    break;
                }
                case HIDDriver::PacDrive: {

                    // allocate report
                    uint8_t report_data[5] {};

                    // set leds
                    static const size_t mapping[] = {
                            8, 9, 10, 11, 12, 13, 14, 15,
                            0, 1, 2, 3, 4, 5, 6, 7
                    };
                    auto led_data = (uint16_t *) &report_data[3];
                    size_t count = 0;
                    for (auto &button_output_states : hid->button_output_states) {
                        for (auto &&button_output_state : button_output_states) {
                            if (button_output_state) {
                                *led_data |= 1u << mapping[count];
                            }
                            count++;
                        }
                    }

                    // write report
                    DWORD written_bytes = 0;
                    WriteFile(
                            hid->handle,
                            static_cast<void *>(&report_data),
                            sizeof(report_data),
                            &written_bytes,
                            nullptr
                    );

                    break;
                }
                default:
                    break;
            }
            break;
        }
        case SEXTET_OUTPUT: {
            device->sextetInfo->push_light_state();
            break;
        }
        case SMX_STAGE: {
            device->smxstageInfo->Update();
            break;
        };
        case SMX_DEDICAB: {
            device->smxdedicabInfo->Update();
            break;
        }
        default:
            break;
    }

    // unlock device
    device->mutex_out->unlock();
}

void rawinput::RawInputManager::devices_flush_output(bool optimized) {

    // optimized routine
    if (optimized) {

        // notify thread
        output_thread_ready = true;
        output_thread_cv.notify_one();
        return;
    }

    // blocking routine
    for (auto &device : this->devices) {

        // write output
        device_write_output(&device, false);
    }
}

void rawinput::RawInputManager::devices_print() {

    if (!DUMP_HID_DEVICES_TO_LOG) {
        log_info("rawinput", "verbose dump of HID devices is disabled by default; see -sysdump option");
        return;
    }

    bool touchscreen_found = false;

    // iterate devices
    log_info("rawinput", "printing list of detected devices");
    log_info("rawinput", "detected device count: {}", devices.size());
    for (auto &device : devices) {
        bool is_touchscreen = false;

        // lock it
        device.mutex->lock();

        // general information
        log_misc("rawinput", "--------begin device @{}", device.handle);
        log_misc("rawinput", "device name: {}", device.name);
        log_misc("rawinput", "device desc: {}", device.desc);
        log_misc("rawinput", "device handle: {}", device.handle);

        // type specific
        switch (device.type) {
            case MOUSE:
                log_misc("rawinput", "device type: MOUSE");
                break;
            case KEYBOARD:
                log_misc("rawinput", "device type: KEYBOARD");
                break;
            case HID: {
                log_misc("rawinput", "device type: HID");
                log_misc("rawinput", "device preparsed size: {}", device.hidInfo->preparsed_size);
                log_misc("rawinput", "device usage: {}", device.hidInfo->usage_name);

                // check touchscreen
                if (device.hidInfo->touch.valid) {
                    log_info("rawinput", "device is marked as touchscreen");
                    if (!touchscreen_found) {
                        touchscreen_found = true;
                        is_touchscreen = true;
                    }
                }

                // button caps
                log_misc("rawinput", "device button caps count: {}",
                        device.hidInfo->button_caps_list.size());
                int button_name_index = 0;
                for (size_t i = 0; i < device.hidInfo->button_caps_list.size(); i++) {
                    auto &button_caps = device.hidInfo->button_caps_list[i];
                    USAGE usage_min = button_caps.Range.UsageMin;
                    USAGE usage_max = button_caps.Range.UsageMax;
                    int cap_len = usage_max - usage_min;
                    auto &name1 = device.hidInfo->button_caps_names[button_name_index];
                    auto &name2 = device.hidInfo->button_caps_names[button_name_index + cap_len];
                    button_name_index += cap_len + 1;
                    log_misc("rawinput", "device button caps detected: {} to {} ({}-{})",
                            name1, name2, usage_min, usage_max);
                }

                // button output caps
                log_misc("rawinput", "device button output caps count: {}",
                        device.hidInfo->button_output_caps_list.size());
                int button_output_name_index = 0;
                for (size_t i = 0; i < device.hidInfo->button_output_caps_list.size(); i++) {
                    auto &button_caps = device.hidInfo->button_output_caps_list[i];
                    USAGE usage_min = button_caps.Range.UsageMin;
                    USAGE usage_max = button_caps.Range.UsageMax;
                    int cap_len = usage_max - usage_min;
                    auto &name1 = device.hidInfo->button_output_caps_names[button_output_name_index];
                    auto &name2 = device.hidInfo->button_output_caps_names[button_output_name_index + cap_len];
                    button_output_name_index += cap_len + 1;
                    log_misc("rawinput", "device button output caps detected: {} to {} ({}-{})",
                            name1, name2, usage_min, usage_max);
                }

                // value caps
                if (!device.hidInfo->value_caps_list.empty()) {
                    log_misc("rawinput", "device value caps count: {}",
                            device.hidInfo->value_caps_list.size());
                    for (size_t i = 0; i < device.hidInfo->value_caps_list.size(); i++) {
                        auto &value_caps = device.hidInfo->value_caps_list[i];
                        if (device.hidInfo->value_caps_names.size() < i) {
                            log_fatal("rawinput", "value cap has no name!");
                        }
                        auto &name = device.hidInfo->value_caps_names[i];
                        LONG min = value_caps.LogicalMin;
                        LONG max = value_caps.LogicalMax;
                        log_misc("rawinput", "device value caps detected: {} ({} to {}, {}-bit)",
                                name, min, max, value_caps.BitSize);

                        if (name.compare("X") == 0 && is_touchscreen) {
                            TOUCHSCREEN_RANGE_X = max;
                        } else if (name.compare("Y") == 0 && is_touchscreen) {
                            TOUCHSCREEN_RANGE_Y = max;
                        }
                    }
                }

                // value output caps
                if (!device.hidInfo->value_output_caps_list.empty()) {
                    log_misc("rawinput", "device value output caps count: {}",
                            device.hidInfo->value_output_caps_list.size());
                    for (size_t i = 0; i < device.hidInfo->value_output_caps_list.size(); i++) {
                        auto &value_caps = device.hidInfo->value_output_caps_list[i];
                        if (device.hidInfo->value_output_caps_names.size() < i) {
                            log_fatal("rawinput", "value output cap has no name!");
                        }
                        auto &name = device.hidInfo->value_output_caps_names[i];
                        LONG min = value_caps.LogicalMin;
                        LONG max = value_caps.LogicalMax;
                        log_misc("rawinput", "device value output caps detected: {} ({} to {}, {}-bit)",
                                name, min, max, value_caps.BitSize);
                    }
                }

                break;
            }
            case MIDI: {
                log_misc("rawinput", "device type: MIDI");
                break;
            }
            case SEXTET_OUTPUT: {
                log_misc("rawinput", "device type: SEXTET_OUTPUT");
                break;
            }
            case PIUIO_DEVICE: {
                log_misc("rawinput", "device type: PIUIO");
                break;
            }
            case SMX_STAGE: {
                log_misc("rawinput", "device type: SMX_STAGE");
                break;
            }
            case SMX_DEDICAB: {
                log_misc("rawinput", "device type: SMX_DEDICAB");
                break;
            }
            case UNKNOWN:
            default:
                log_warning("rawinput", "device type: UNKNOWN");
                break;
        }


        log_misc("rawinput", "----------end device @{}", device.handle);
        // unlock device
        device.mutex->unlock();
    }

    // mark as done
    log_misc("rawinput", "done printing devices");
}

rawinput::DeviceInfo rawinput::RawInputManager::get_device_info(const std::string &device_name) {
    DeviceInfo info {};

    // check device name
    if (device_name.size() < 16) {
        return info;
    }

    // remove header
    auto name = device_name.substr(4);

    // split
    std::vector<std::string> elements;
    strsplit(name, elements, '#');

    // check split
    if (elements.size() < 4) {
        return info;
    }

    // fill out fields
    info.devclass = elements[0];
    info.subclass = elements[1];
    info.protocol = elements[2];
    info.guid_str = elements[3];

    // generate GUID
    std::wstring guid_wstr = s2ws(info.guid_str);
    if (IIDFromString(guid_wstr.c_str(), &info.guid) != S_OK) {
        return info;
    }

    return info;
}

rawinput::Device *rawinput::RawInputManager::devices_get(const std::string &name, bool updated) {

    // if the device name is empty, we do not even have to look for it
    if (name.empty()) {
        return nullptr;
    }

    // check if caller wants only updated devices
    if (updated) {

        // iterate the devices
        for (auto &device : this->devices) {

            // check if the device names match
            if (device.name == name) {

                // lock the device since we are messing with updated
                device.mutex->lock();

                // was the device updated?
                if (device.updated) {

                    // next call shouldn't trigger
                    device.updated = false;

                    // unlock the device
                    device.mutex->unlock();

                    // return the device
                    return &device;

                } else {

                    // unlock the device again
                    device.mutex->unlock();

                    // return null since the device wasn't updated
                    return nullptr;
                }
            }
        }

    } else {

        // just the usual "lookup by name"
        for (auto &device : this->devices) {
            if (device.name == name) {
                return &device;
            }
        }
    }

    // device not found
    return nullptr;
}

void rawinput::RawInputManager::add_callback_add(void *data, std::function<void (void *, Device *)> callback) {
    this->callback_add.push_back(DeviceCallback {
            .data = data,
            .f = std::move(callback),
    });
}

void rawinput::RawInputManager::remove_callback_add(void *data, const std::function<void (void *, Device *)> &callback) {
    this->callback_add.erase(std::remove_if(
            this->callback_add.begin(), this->callback_add.end(),
            [data, callback](DeviceCallback const &cb) {
                return cb.data == data && cb.f.target<void>() == callback.target<void>();
            }), this->callback_add.end());
}

void rawinput::RawInputManager::add_callback_change(void *data, std::function<void (void *, Device *)> callback) {
    this->callback_change.push_back(DeviceCallback {
            .data = data,
            .f = std::move(callback),
    });
}

void rawinput::RawInputManager::remove_callback_change(void * data, const std::function<void (void *, Device *)> &callback) {
    this->callback_change.erase(std::remove_if(
            this->callback_change.begin(), this->callback_change.end(),
            [data, callback](DeviceCallback const &cb) {
                return cb.data == data && cb.f.target<void>() == callback.target<void>();
            }), this->callback_change.end());
}

void rawinput::RawInputManager::add_callback_midi(void * data, std::function<void (void *, Device *,
        uint8_t, uint8_t, uint8_t, uint8_t)> callback) {
    this->callback_midi.push_back(MidiCallback {
            .data = data,
            .f = std::move(callback),
    });
}

void rawinput::RawInputManager::remove_callback_midi(void * data, const std::function<void (void *, Device *,
        uint8_t, uint8_t, uint8_t, uint8_t)> &callback) {
    this->callback_midi.erase(std::remove_if(
            this->callback_midi.begin(), this->callback_midi.end(),
            [data, callback](MidiCallback const &cb) {
                return cb.data == data && cb.f.target<void>() == callback.target<void>();
            }), this->callback_midi.end());
}
