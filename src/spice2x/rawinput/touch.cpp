#include "touch.h"

#include <algorithm>

#include <windows.h>
#include <versionhelpers.h>

#include "util/logging.h"
#include "util/time.h"
#include "touch/touch.h"

// std::min
#ifdef min
#undef min
#endif

// std::max
#ifdef max
#undef max
#endif

namespace rawinput::touch {

    // settings
    bool DISABLED = false;
    bool INVERTED = false;

    // state
    static bool DISPLAY_INITIALIZED = false;
    static DWORD DISPLAY_ORIENTATION = DMDO_DEFAULT;
    static long DISPLAY_SIZE_X = 1920L;
    static long DISPLAY_SIZE_Y = 1080L;

    bool is_touchscreen(Device *device) {

        // check if disabled
        if (DISABLED) {
            return false;
        }

        // check device type
        if (device->type != HID) {
            return false;
        }

        auto *hid = device->hidInfo;
        auto &attributes = hid->attributes;

        // filter by VID/PID
        if (attributes.VendorID > 0) {

            // P2418HT
            // touch points apparently aren't released and will stay
            if (attributes.VendorID == 0x1FD2 && attributes.ProductID == 0x6103) {
                return false;
            }
        }

        // ignore the "Touch Pad" (0x05) usage under the "Digitizers" (0x0d) usage page
        if (hid->caps.UsagePage == 0x0d && hid->caps.Usage == 0x05) {
            return false;
        }

        // check description
        if (device->desc == "HID-compliant touch screen") {
            // TODO: this only works on english OS (?)
            return true;
        }

        // we can also check if there's touch point values inside
        for (const auto &value_name : hid->value_caps_names) {
            if (value_name == "Contact identifier") {
                return true;
            }
        }

        // nope this one probably not
        return false;
    }

    void enable(Device *device) {
        if (device->type == HID) {
            device->hidInfo->touch.valid = true;
            log_info("rawinput", "enabled touchscreen device: {} ({})", device->desc, device->name);
        } else {
            log_fatal("rawinput", "tried to enable touch functionality on non HID device");
        }
    }

    void disable(Device *device) {
        if (device->type == HID) {
            device->hidInfo->touch.valid = false;
            log_info("rawinput", "disabled touchscreen device: {} ({})", device->desc, device->name);
        } else
            log_fatal("rawinput", "tried to disable touch functionality on non HID device");
    }

    void update_input(Device *device) {

        // check type
        if (device->type != HID) {
            log_fatal("rawinput", "touch update called on non HID device");
            return;
        }

        // get touch info
        auto hid = device->hidInfo;
        auto &touch = hid->touch;
        if (!touch.valid) {

            // not a registered touchscreen
            return;
        }

        // parse elements
        if (!touch.parsed_elements) {
            log_info("rawinput", "parsing touch elements");

            // clear lists first
            touch.elements_contact_count.clear();
            touch.elements_contact_identifier.clear();
            touch.elements_x.clear();
            touch.elements_y.clear();
            touch.elements_width.clear();
            touch.elements_height.clear();
            touch.elements_pressed.clear();
            touch.elements_pressure.clear();

            // get value indices
            for (int i = 0; i < (int) hid->value_caps_names.size(); i++) {
                auto &name = hid->value_caps_names[i];
                if (name == "Contact count") {
                    touch.elements_contact_count.push_back(i);
                } else if (name == "X") {
                    touch.elements_x.push_back(i);
                } else if (name == "Y") {
                    touch.elements_y.push_back(i);
                } else if (name == "Width") {
                    touch.elements_width.push_back(i);
                } else if (name == "Height") {
                    touch.elements_height.push_back(i);
                } else if (name == "Contact identifier") {
                    touch.elements_contact_identifier.push_back(i);
                } else if (name == "Pressure") {
                    touch.elements_pressure.push_back(i);
                }
            }

            // get button indices
            for (int i = 0; i < (int) hid->button_caps_names.size(); i++) {
                auto &name = hid->button_caps_names[i];
                if (name == "Tip Switch") {
                    touch.elements_pressed.push_back(i);
                }
            }

            // check sizes
            auto touch_size = touch.elements_contact_identifier.size();
            if (touch_size != touch.elements_x.size() ||
                touch_size != touch.elements_y.size() ||
                touch_size != touch.elements_pressed.size())
            {
                log_info("rawinput", "touch element size mismatch: {}:contacts, {}:x, {}:y, {}:pressed",
                         touch.elements_contact_identifier.size(),
                         touch.elements_x.size(),
                         touch.elements_y.size(),
                         touch.elements_pressed.size());
                disable(device);
                return;
            }

            // mark as parsed
            touch.parsed_elements = true;
            log_info("rawinput", "touch elements parsed: {} status fields", touch.elements_x.size());
        }

        // check if display is initialized
        if (!DISPLAY_INITIALIZED) {
            display_update();
        }

        // update timeouts here as well so events are in the right order
        update_timeouts(device);

        // determine the number of touches contained in this report, defaulting to the size of `elements_x`
        size_t touch_report_count = touch.elements_x.size();
        if (!touch.elements_contact_count.empty()) {

            // support devices that have multiple "Contact count" fields, for some reason
            size_t contact_count = 0;
            for (auto &index : touch.elements_contact_count) {
                contact_count = std::max(contact_count, (size_t) hid->value_states_raw[index]);
            }

            // hybrid mode devices will report a contact count of 0 for subsequent reports that are
            // part of the same initial frame
            if (contact_count > 0) {
                if (contact_count > touch_report_count) {
                    touch.remaining_contact_count = contact_count - touch_report_count;
                } else {
                    touch_report_count = contact_count;
                    touch.remaining_contact_count = 0;
                }
            } else if (touch.remaining_contact_count > 0) {
                if (touch.remaining_contact_count > touch_report_count) {
                    touch.remaining_contact_count -= touch_report_count;
                } else {
                    touch_report_count = touch.remaining_contact_count;
                    touch.remaining_contact_count = 0;
                }
            }
        }

        // iterate all input data and get touch points
        std::vector<HIDTouchPoint> touch_points;
        touch_points.reserve(touch.elements_x.size());
        for (size_t i = 0; i < touch.elements_x.size(); i++) {

            // build touch point
            HIDTouchPoint hid_tp{};
            auto pos_x = hid->value_states[touch.elements_x[i]];
            auto pos_y = hid->value_states[touch.elements_y[i]];
            switch (DISPLAY_ORIENTATION) {
                case DMDO_DEFAULT:
                    hid_tp.x = pos_x;
                    hid_tp.y = pos_y;
                    break;
                case DMDO_90:
                    hid_tp.x = 1.f - pos_y;
                    hid_tp.y = pos_x;
                    break;
                case DMDO_180:
                    hid_tp.x = 1.f - pos_x;
                    hid_tp.y = 1.f - pos_y;
                    break;
                case DMDO_270:
                    hid_tp.x = pos_y;
                    hid_tp.y = 1.f - pos_x;
                    break;
                default:
                    break;
            }

            // optionally invert
            if (INVERTED) {
                hid_tp.x = 1.f - hid_tp.x;
                hid_tp.y = 1.f - hid_tp.y;
            }

            // check if this touch point should be considered valid
            //
            // If "Contact count" reports there are no touch reports remaining and the X and Y
            // coordinates of this touch point are zero, then this report element should be
            // skipped.
            if (touch_report_count == 0 && hid_tp.x == 0.f && hid_tp.y == 0.f) {
                continue;
            } else if (touch_report_count > 0) {
                touch_report_count--;
            }

            // generate ID (hopefully unique)
            hid_tp.id = (DWORD) hid->value_states_raw[touch.elements_contact_identifier[i]];
            hid_tp.id += (DWORD) (0xFFFFFF + device->id * 512);

            //std::string src = "(none)";

            // check if tip switch is down
            size_t index_pressed = touch.elements_pressed[i];
            for (auto &button_states : hid->button_states) {
                if (index_pressed < button_states.size()) {
                    hid_tp.down = button_states[index_pressed];

                    /*
                    if (hid_tp.down)
                        src = "pressed (index: " + to_string(touch.elements_pressed[i]) + ", state: " + to_string(button_states[index_pressed]) +")";
                        */

                    break;
                } else
                    index_pressed -= button_states.size();
            }

            // check width/height of touch point to see if pressed
            float width = 0.f, height = 0.f;
            if (!hid_tp.down && i < touch.elements_width.size() && i < touch.elements_height.size()) {
                width = hid->value_states[touch.elements_width[i]];
                height = hid->value_states[touch.elements_height[i]];
                hid_tp.down = width > 0.f && height > 0.f;

                /*
                if (hid_tp.down)
                    src = "width_height (width index: " + to_string(touch.elements_width[i]) + ", height index: " + to_string(touch.elements_height[i]) + ")";
                    */
            }

            // so last thing we can check is the pressure
            if (!hid_tp.down && i < touch.elements_pressure.size()) {
                auto pressure = hid->value_states[touch.elements_pressure[i]];
                hid_tp.down = pressure > 0.f;

                /*
                if (hid_tp.down)
                    src = "pressure (index: " + to_string(touch.elements_pressure[i]) + ")";
                    */
            }

            /*
            log_info("rawinput",
                "touch i: " + to_string(i) +
                " (id: " + to_string(hid_tp.id) +
                "), ci: " + to_string(hid->value_states_raw[touch.elements_contact_identifier[i]]) +
                ", x: " + to_string(pos_x) +
                ", y: " + to_string(pos_y) +
                ", width: " + to_string(width) +
                ", height: " + to_string(height) +
                ", down: " + to_string(hid_tp.down) +
                ", src: " + src);
                */

            // add to touch points
            touch_points.emplace_back(hid_tp);
        }

        // process touch points
        std::vector<DWORD> touch_removes;
        std::vector<TouchPoint> touch_writes;
        std::vector<DWORD> touch_modifications;
        for (auto &hid_tp : touch_points) {

            // check if existing
            auto existing = std::find_if(
                    touch.touch_points.begin(),
                    touch.touch_points.end(),
                    [hid_tp] (const HIDTouchPoint &x) {
                        return x.id == hid_tp.id;
                    });

            /*
            ssize_t ttl = -1;
            int64_t last_report = -1;
            if (existing != touch.touch_points.end()) {
                ttl = existing->ttl;
                last_report = existing->last_report;
            }

            log_info("rawinput",
                "id: " + to_string(hid_tp.id) +
                ", existing: " + to_string(existing != touch.touch_points.end()) +
                ", ttl: " + to_string(ttl) +
                ", last_report: " + to_string(last_report) +
                ", down: " + to_string(hid_tp.down));
                */

            // check if pressed
            if (!hid_tp.down) {

                // only remove if it exists
                if (existing != touch.touch_points.end()) {

                    // remove all touch points with this ID
                    touch_removes.push_back(hid_tp.id);
                    touch.touch_points.erase(std::remove_if(
                            touch.touch_points.begin(),
                            touch.touch_points.end(),
                            [hid_tp] (const HIDTouchPoint &x) {
                                return x.id == hid_tp.id;
                            }), touch.touch_points.end());
                }
            } else {

                // write touch point
                TouchPoint tp {
                        .id = hid_tp.id,
                        .x = (long) (hid_tp.x * DISPLAY_SIZE_X),
                        .y = (long) (hid_tp.y * DISPLAY_SIZE_Y),
                        .mouse = false,
                };
                touch_writes.push_back(tp);
                touch_modifications.push_back(hid_tp.id);

                // check if existing
                if (existing == touch.touch_points.end()) {

                    // add new touch point
                    touch.touch_points.push_back(hid_tp);

                } else {

                    // update existing touch point
                    *existing = hid_tp;
                }
            }
        }

        // set TTL and last report time
        if (!touch.elements_x.empty() && !touch_modifications.empty()) {
            auto ttl = touch.touch_points.size() / touch.elements_x.size() + 1;
            auto system_time_ms = get_system_milliseconds();
            for (auto &hid_tp_id : touch_modifications) {
                for (auto &hid_tp : touch.touch_points) {
                    if (hid_tp.id == hid_tp_id) {
                        hid_tp.ttl = ttl;
                        hid_tp.last_report = system_time_ms;
                    }
                }
            }
        }

        // remove dead touch points
        auto ttl_it = touch.touch_points.begin();
        while (ttl_it != touch.touch_points.end()) {
            auto &tp = *ttl_it;
            if (tp.ttl == 0) {
                touch_removes.push_back(tp.id);
                ttl_it = touch.touch_points.erase(ttl_it);
            } else {
                tp.ttl--;
                ttl_it++;
            }
        }

#ifndef SPICETOOLS_SPICECFG_STANDALONE

        // update touch module
        touch_remove_points(&touch_removes);
        touch_write_points(&touch_writes);
#endif
    }

    void update_timeouts(Device *device) {

        // check type
        if (device->type != HID) {
            log_fatal("rawinput", "touch timeout update called on non HID device");
            return;
        }

        // get touch info
        auto hid = device->hidInfo;
        auto &touch = hid->touch;
        if (!touch.valid) {
            return; // not a registered touchscreen
        }

        // calculate deadline - we allow the devices 50ms of not sending shit
        auto deadline = get_system_milliseconds() - 50;

        // check touch points
        std::vector<DWORD> touch_removes;
        auto touch_it = touch.touch_points.begin();
        while (touch_it != touch.touch_points.end()) {
            auto &hid_tp = *touch_it;

            // see if it's behind the deadline
            if (hid_tp.last_report < deadline) {

                // oops it's gone
                touch_removes.push_back(hid_tp.id);
                touch_it = touch.touch_points.erase(touch_it);

            } else {

                // check the next device
                touch_it++;
            }
        }

#ifndef SPICETOOLS_SPICECFG_STANDALONE

        // remove from touch module
        touch_remove_points(&touch_removes);
#endif
    }

    void update_timeouts(RawInputManager *manager) {

        // iterate all devices
        for (auto &device : manager->devices_get()) {

            // check if it's a valid touchscreen
            if (device.type == HID && device.hidInfo->touch.valid) {

                // lock n' load
                device.mutex->lock();
                update_timeouts(&device);
                device.mutex->unlock();
            }
        }
    }

    bool is_enabled(RawInputManager *manager) {

        // check if disabled or manager is null
        if (DISABLED || manager == nullptr)
            return false;

        // check if at least one device is marked as valid touchscreen
        for (auto &device : manager->devices_get()) {
            if (device.type == HID && device.hidInfo->touch.valid) {
                return true;
            }
        }

        // no valid touch screen found
        return false;
    }

    void display_update() {

        // check if disabled
        if (DISABLED)
            return;

        // determine monitor size
        static RECT display_rect;
        GetWindowRect(GetDesktopWindow(), &display_rect);
        DISPLAY_SIZE_X = display_rect.right - display_rect.left;
        DISPLAY_SIZE_Y = display_rect.bottom - display_rect.top;
        log_info("rawinput", "display size: {}x{}", (int) DISPLAY_SIZE_X, (int) DISPLAY_SIZE_Y);

        // determine monitor orientation
        DEVMODE display_mode{};
        display_mode.dmSize = sizeof(DEVMODE);
        if (!EnumDisplaySettingsEx(nullptr, ENUM_CURRENT_SETTINGS, &display_mode, EDS_RAWMODE)) {
            log_info("rawinput", "failed to determine monitor mode");
        } else if (display_mode.dmFields & DM_DISPLAYORIENTATION) {
            DISPLAY_ORIENTATION = display_mode.dmDisplayOrientation;
            switch (DISPLAY_ORIENTATION) {
                case DMDO_DEFAULT:
                    log_info("rawinput", "display rotation: 0");
                    break;
                case DMDO_90:
                    log_info("rawinput", "display rotation: 90");
                    break;
                case DMDO_180:
                    log_info("rawinput", "display rotation: 180");
                    break;
                case DMDO_270:
                    log_info("rawinput", "display rotation: 270");
                    break;
                default:
                    break;
            }

            // another XP fix
            if (!IsWindowsVistaOrGreater()) {
                switch (DISPLAY_ORIENTATION) {
                    case DMDO_90:
                        DISPLAY_ORIENTATION = DMDO_180;
                        log_info("rawinput", "flipping to 180");
                        break;
                    case DMDO_270:
                        DISPLAY_ORIENTATION = DMDO_90;
                        log_info("rawinput", "flipping to 90");
                        break;
                    default:
                        break;
                }
            }
        } else {
            log_info("rawinput", "failed to determine monitor orientation");
        }

        // mark as initialized
        DISPLAY_INITIALIZED = true;
    }
}
