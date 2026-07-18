#include "rawinput.h"

#include <cstdarg>
#include <utility>
#include <vector>

#include <objbase.h>
#include <setupapi.h>

#include "util/logging.h"
#include "external/robin_hood.h"
#include "util/precise_timer.h"
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

    // the price we pay for making spice overlay consume from raw input
    // making focus detection a nightmare
    bool OS_WINDOW_ACTIVE = false;
}

// when replacing a device slot in place, keep the old slot's per-device mutexes
// instead of the freshly allocated pair on `replacement`. their addresses stay
// stable, so a thread still holding a snapshot pointer to the slot (e.g. the
// output thread blocked on mutex_out, which is taken without devices_mutex)
// never locks freed memory. the freshly allocated pair is freed here rather
// than leaking the old one. the slot must already be destructed so both
// mutexes are unlocked
void rawinput::RawInputManager::reuse_device_mutexes(Device &replacement, const Device &existing) {
    delete replacement.mutex;
    delete replacement.mutex_out;
    replacement.mutex = existing.mutex;
    replacement.mutex_out = existing.mutex_out;
}

void rawinput::RawInputManager::rawinput_device_add(Device *device) {
    std::lock_guard<std::mutex> lock(this->rawinput_devices_mutex);
    this->rawinput_devices[device->handle] = device;
}

void rawinput::RawInputManager::rawinput_device_remove(Device *device) {
    std::lock_guard<std::mutex> lock(this->rawinput_devices_mutex);

    // teardown is shared by every device type, and handles can be reused; only erase
    // an entry that still belongs to this exact RawInput device
    auto it = this->rawinput_devices.find(device->handle);
    if (it != this->rawinput_devices.end() && it->second == device) {
        this->rawinput_devices.erase(it);
    }
}

rawinput::RawInputManager::RawInputManager() {

    XINPUT_MGR = std::make_unique<xinput::XInputManager>();

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

    // wait for any in-flight async MIDI scan before tearing down devices
    this->midi_scan_join();

    // unregister device messages
    this->devices_unregister();

    // stop threads
    this->flush_stop();
    this->output_stop();

    // destruct all devices and input window
    this->devices_destruct();
    this->input_hwnd_destroy();

    XINPUT_MGR.reset();
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
    timeutils::PreciseSleepTimer timer;
    while (!this->input_hwnd) {
        timer.sleep(1);
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
    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
    this->devices_destruct();
    log_info("rawinput", "reloading devices...");

    // scan for devices
    this->devices_scan_rawinput();

    // MIDI enumeration can block for ~10s while the Windows MIDI subsystem starts
    // up, so run it off the init path instead of gating startup on it. it locks
    // devices_mutex only for the list mutation, so it is safe to run concurrently
    this->midi_scan_start();

    this->devices_scan_piuio();

    if (ENABLE_SMX_STAGE) {
        this->devices_scan_smxstage();
    }

    if (ENABLE_SMX_DEDICAB) {
        this->devices_scan_smxdedicab();
    }

    this->devices_scan_xinput();

    // check for LIT Board
    sextet_register("COM54", "LIT Board", false);

    // register devices
    this->devices_register();
}

void rawinput::RawInputManager::devices_scan_rawinput(const std::string &device_name) {
    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
    log_misc("rawinput", "scan rawinput devices...");

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
    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

    // get device name
    std::string device_name = rawinput_get_device_name(device->hDevice);
    if (device_name.empty()) {
        return;
    }

    log_misc("rawinput", "found rawinput device: {}", device_name);

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
    size_t i = 0;
    for (const auto &existing : this->devices) {
        if (existing.name == device_name) {
            unique_id = i;
            break;
        }
        i++;
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
            std::map<std::pair<USAGE, ULONG>, ULONG> button_usage_pages;
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
                button_usage_pages[std::make_pair(button_caps.UsagePage, button_caps.LinkCollection)] += button_count;

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

                // fix up invalid max values (seen on xbox controllers where max is 0xffffffff despite being 16-bit)
                if (value_caps.LogicalMin == 0 && value_caps.BitSize > 0 && value_caps.BitSize < 32) {
                    const uint32_t field_max = (1u << value_caps.BitSize) - 1u;
                    const uint32_t logical_max = static_cast<uint32_t>(value_caps.LogicalMax);

                    if (logical_max > field_max) {
                        log_info(
                            "rawinput",
                            "value cap {} LogicalMax exceeds bit width, fixing it up: {} -> {}",
                            value_cap_num,
                            value_caps.LogicalMax,
                            field_max
                        );

                        value_caps.LogicalMax = static_cast<LONG>(field_max);
                    }
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
            new_device.hidInfo->button_usage_pages = std::move(button_usage_pages);
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

            // destruct and replace, reusing the slot's existing mutexes
            this->devices_destruct(&prev_device);
            reuse_device_mutexes(new_device, prev_device);
            prev_device = new_device;
            this->rawinput_device_add(&prev_device);

            // notify change
            for (auto &cb : this->callback_change) {
                cb.f(cb.data, &prev_device);
            }

            return;
        }
    }

    // add device to list
    auto &added_device = this->devices.emplace_back(new_device);
    this->rawinput_device_add(&added_device);
    if (log) {
        log_info("rawinput", "added device: {} / {}", added_device.desc, added_device.name);
    }

    // notify add
    for (auto &cb : this->callback_add) {
        cb.f(cb.data, &added_device);
    }
}

void rawinput::RawInputManager::devices_scan_piuio() {
    log_misc("rawinput", "scan PIUIO devices...");

    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

    // add device to vector first so pointer is valid
    auto *new_piuio_device = new Device();
    new_piuio_device->id = this->devices.size() + 1;
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
    log_misc("rawinput", "scan SMX Stage devices...");

    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

    auto *new_smxstage_device = new Device();
    new_smxstage_device->id = this->devices.size() + 1;
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
    log_misc("rawinput", "scan SMX Dedicated Cabinet devices...");

    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

    auto *new_smxdedicab_device = new Device();
    new_smxdedicab_device->id = this->devices.size() + 1;
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

void rawinput::RawInputManager::devices_scan_xinput() {
    log_misc("rawinput", "scan XInput devices...");

    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

    const auto connected_players = XINPUT_MGR->get_available_players();

    // first, destroy missing devices
    std::vector<std::string> devices_to_remove;
    for (auto &device : this->devices) {
        if (device.type != XINPUT_GAMEPAD) {
            continue;
        }
        const uint8_t player = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(device.handle));
        if (std::find(connected_players.begin(), connected_players.end(), player) == connected_players.end()) {
            devices_to_remove.push_back(device.name);
        }
    }
    for (const auto &name : devices_to_remove) {
        this->devices_remove(name);
    }

    auto create_device = [](const uint8_t player) -> Device {
        Device device = {};
        device.type = XINPUT_GAMEPAD;
        device.name = xinput::get_device_desc(player);
        device.desc = fmt::format("XInput Gamepad P{}", player + 1);
        device.handle = reinterpret_cast<HANDLE>(player);
        device.mutex = new std::mutex();
        device.mutex_out = new std::mutex();
        return device;
    };

    // add new devices
    for (const auto player : connected_players) {
        bool duplicate_found = false;

        // check for duplicates first
        for (auto &prev_device : this->devices) {
            if (prev_device.name != xinput::get_device_desc(player)) {
                continue;
            }
            if (prev_device.type == DESTROYED) {
                log_info("rawinput", "overwriting previously destroyed XInput device: {}", prev_device.name);
                const auto old_id = prev_device.id;

                // replace in place, reusing the slot's existing mutexes
                auto replacement = create_device(player);
                reuse_device_mutexes(replacement, prev_device);
                prev_device = replacement;
                prev_device.id = old_id;

                // notify change
                for (auto &cb : this->callback_change) {
                    cb.f(cb.data, &prev_device);
                }
            }
            duplicate_found = true;
            break;
        }

        if (!duplicate_found) {
            // add new device
            log_info("rawinput", "adding new XInput device: player {}", player + 1);
            auto new_xinput_device = create_device(player);
            new_xinput_device.id = this->devices.size() + 1;
            auto &device = this->devices.emplace_back(new_xinput_device);

            // notify add
            for (auto &cb : this->callback_add) {
                cb.f(cb.data, &device);
            }
        }
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

                // wait up to ~500ms, but wake immediately if flush_stop()
                // flips the running flag. Without the CV the in-flight
                // Sleep() forced launcher::shutdown() to block for the full
                // remaining sleep window on every close.
                std::unique_lock<std::mutex> lock(this->flush_thread_m);
                this->flush_thread_cv.wait_for(
                    lock,
                    std::chrono::milliseconds(495),
                    [this] { return !this->flush_thread_running; });
            }
        });
    }
}

void rawinput::RawInputManager::flush_stop() {

    // set stop flag and wake the flush thread immediately so shutdown
    // isn't blocked by the in-progress wait inside the loop above.
    {
        std::lock_guard<std::mutex> lock(this->flush_thread_m);
        this->flush_thread_running = false;
    }
    this->flush_thread_cv.notify_all();

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
                    this->devices_write_output_snapshot(true);
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

                // base description
                device_description = wchar_to_u8(reinterpret_cast<PWCHAR>(desc_data.get()));

                // append HID product string if available
                HANDLE hid_handle = CreateFile(
                        device_path.c_str(), 0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,
                        OPEN_EXISTING,
                        0, nullptr);
                if (hid_handle != INVALID_HANDLE_VALUE) {
                    wchar_t product_buffer[126] {};
                    if (HidD_GetProductString(hid_handle, product_buffer, sizeof(product_buffer))) {
                        auto const product_str = wchar_to_u8(product_buffer);
                        if (!product_str.empty() && device_description != product_str) {
                            device_description += " - " + product_str;
                        }
                    }
                    CloseHandle(hid_handle);
                }
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

    log_misc("rawinput", "checking for sextet-stream device on {}...", port_name);

    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

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
    {
        std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

        // iterate devices
        for (auto &device : this->devices) {

            // check if name matches
            if (device.name == name) {

                // remove device
                this->devices_destruct(&device);
                break;
            }
        }
    }

    // close any MIDI handle detached above, now that devices_mutex is released.
    // removing a MIDI device queues its handle for deferred close; flush it here
    // since we cannot rely on a later scan happening to do it
    this->midi_close_deferred_flush();
}

void rawinput::RawInputManager::devices_register() {
    
    // check input window
    if (!this->input_hwnd) {
        log_warning("rawinput", "trying to register devices without input window");
        return;
    }
    
    log_misc("rawinput", "registering raw input devices...");

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
    {
        std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

        // dispose devices (if there is anything to dispose)
        if (!this->devices.empty()) {
            log_info("rawinput", "disposing devices");
            for (auto &device : this->devices) {
                this->devices_destruct(&device, false);
                delete device.mutex;
                delete device.mutex_out;
            }

            // empty array
            this->devices.clear();
        }
    }

    // close any MIDI handles detached above, now that devices_mutex is released
    this->midi_close_deferred_flush();
}

void rawinput::RawInputManager::devices_destruct(Device *device, bool log) {

    this->rawinput_device_remove(device);

    // check if destroyed
    if (device->type == DESTROYED) {
        return;
    }

    // optionally log
    if (log) {
        log_info("rawinput", "destroying device: {} / {}", device->desc, device->name);
    }

    // hold the output mutex for the whole teardown, taken before we flip the type
    // or free anything. device_write_output() locks mutex_out while dereferencing
    // type-specific state (hidInfo/sextetInfo/...) on a snapshot taken outside
    // devices_mutex, so mutex_out is what actually serializes it against us
    std::lock_guard<std::mutex> lock_out(*device->mutex_out);

    // mark the device destroyed while synchronized with in-flight input
    DeviceType device_type;
    {
        std::lock_guard<std::mutex> lock(*device->mutex);
        device_type = device->type;
        device->type = DESTROYED;
    }

    // callbacks may lock the device mutex
    for (auto &cb : this->callback_change) {
        cb.f(cb.data, device);
    }

    // wait for input users before releasing device resources
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
            // never call midiInReset/midiInClose here: this runs under devices_mutex
            // and those WinMM calls block until in-flight input_midi_proc callbacks
            // return, callbacks that also take devices_mutex. detach the handle and
            // let midi_close_deferred_flush() close it once the lock is released
            if (device->handle != (HANDLE) INVALID_HANDLE_VALUE) {
                log_misc("rawinput", "deferring MIDI handle close for: {}", device->desc);
                this->midi_close_deferred.push_back((HMIDIIN) device->handle);
                device->handle = (HANDLE) INVALID_HANDLE_VALUE;
            }
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

    // note: mutex and mutex_out are intentionally left alive here. other threads
    // may still hold a snapshot pointer to this device and block on them, so they
    // are only freed during full teardown; on reuse the slot keeps the same pair
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
            std::unique_lock<std::mutex> rawinput_devices_lock(ref->rawinput_devices_mutex);
            auto device_it = ref->rawinput_devices.find(device_handle);
            if (device_it != ref->rawinput_devices.end()) {
                auto &device = *device_it->second;

                // get input time
                const auto input_time = get_performance_seconds();

                // lock the device before releasing the index so teardown cannot replace its resources
                std::lock_guard<std::mutex> device_lock(*device.mutex);
                rawinput_devices_lock.unlock();

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

                        // a single WM_INPUT may carry more than one HID report from the same
                        // device: bRawData holds dwCount reports of dwSizeHid bytes each (the
                        // buffer size is dwSizeHid * dwCount). parse every report instead of
                        // only the first one.
                        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawhid
                        const DWORD hid_report_count = data_hid.dwCount > 0 ? data_hid.dwCount : 1;
                        for (DWORD hid_report_index = 0; hid_report_index < hid_report_count; hid_report_index++) {
                            auto *report_data = reinterpret_cast<BYTE *>(data_hid.bRawData)
                                    + (size_t) hid_report_index * data_hid.dwSizeHid;

                            // parse reports
                            for (const auto &pair : device.hidInfo->button_usage_pages) {
                                const auto usage_page = pair.first.first;
                                const auto link_collection = pair.first.second;
                                const auto button_count = pair.second;

                                ULONG usages_length = button_count;
                                std::vector<USAGE> usages(static_cast<size_t>(usages_length));
                                if (HidP_GetUsages(
                                        HidP_Input,
                                        usage_page,
                                        link_collection,
                                        usages.data(),
                                        &usages_length,
                                        reinterpret_cast<PHIDP_PREPARSED_DATA>(device.hidInfo->preparsed_data.get()),
                                        reinterpret_cast<PCHAR>(report_data),
                                        data_hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {

                                    // log_warning(
                                    //     "rawinput",
                                    //     "failed to get usages for device {}, usage page {:x} and link collection {:x}",
                                    //     device.desc,
                                    //     usage_page, link_collection);
                                    continue;
                                }

                                // log_info(
                                //     "rawinput",
                                //     "processing HID input for device {}, usage page {:x} and link collection {:x} with {} buttons, got {} reports",
                                //     device.desc,
                                //     usage_page, link_collection, button_count, usages_length);

                                // buttons
                                for (size_t cap_num = 0; cap_num < device.hidInfo->button_caps_list.size(); cap_num++) {
                                    auto &button_caps = device.hidInfo->button_caps_list[cap_num];
                                    auto &button_states = device.hidInfo->button_states[cap_num];
                                    auto &button_down = device.hidInfo->button_down[cap_num];
                                    auto &button_up = device.hidInfo->button_up[cap_num];

                                    // is this the right usage page and link collection?
                                    if (button_caps.UsagePage != usage_page || button_caps.LinkCollection != link_collection) {
                                        continue;
                                    }

                                    // get button count
                                    int button_count = button_caps.Range.UsageMax - button_caps.Range.UsageMin + 1;
                                    if (button_count <= 0) {
                                        continue;
                                    }

                                    // update buttons
                                    std::vector<bool> new_states(button_count);
                                    for (ULONG usage_num = 0; usage_num < usages_length; usage_num++) {
                                        if (usages[usage_num] < button_caps.Range.UsageMin ||
                                            usages[usage_num] > button_caps.Range.UsageMax) {
                                            continue;
                                        }

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
                                        reinterpret_cast<CHAR *>(report_data),
                                        data_hid.dwSizeHid) != HIDP_STATUS_SUCCESS)
                                {
                                    continue;
                                }

                                // get min and max
                                LONG value_min = value_caps.LogicalMin;
                                LONG value_max = value_caps.LogicalMax;

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

                                    // fix sign bits for signed values
                                    if (value_caps.LogicalMin < 0 &&
                                        0 < value_caps.BitSize && value_caps.BitSize < 32) {

                                        ULONG raw = static_cast<ULONG>(value_raw) & ((1u << value_caps.BitSize) - 1u);
                                        const ULONG sign_bit = 1u << (value_caps.BitSize - 1);
                                        value_raw = static_cast<LONG>((raw ^ sign_bit) - sign_bit);
                                    }

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
                        }

                        break;
                    }
                    default:
                        break;
                }

            }
            if (rawinput_devices_lock.owns_lock()) {
                rawinput_devices_lock.unlock();
            }
            
            // update controller state ring buffers (DDR/MDXF)
            mdxf_poll(true);

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
        case XINPUT_GAMEPAD: {
            // nothing - updates to these are instant
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
    this->devices_write_output_snapshot(false);
}

void rawinput::RawInputManager::devices_write_output_snapshot(bool only_updated) {

    // snapshot device pointers under devices_mutex, then write without holding it.
    // the std::list keeps addresses stable, so the (potentially blocking) writes
    // below don't hold devices_mutex against input
    std::vector<Device *> snapshot;
    {
        std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);
        snapshot.reserve(this->devices.size());
        for (auto &device : this->devices) {
            snapshot.push_back(&device);
        }
    }
    for (auto *device : snapshot) {
        device_write_output(device, only_updated);
    }
}

void rawinput::RawInputManager::devices_print() {

    if (!DUMP_HID_DEVICES_TO_LOG) {
        log_info("rawinput", "verbose dump of HID devices is disabled by default; see -sysdump option");
        return;
    }

    bool touchscreen_found = false;

    // lock the device list while iterating
    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

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
            case XINPUT_GAMEPAD: {
                log_misc("rawinput", "device type: XINPUT");
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

    // lock the device list so a concurrent scan can't mutate it while we search.
    // the returned pointer stays valid after unlock because devices is a std::list
    std::lock_guard<std::recursive_mutex> lock(this->devices_mutex);

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
