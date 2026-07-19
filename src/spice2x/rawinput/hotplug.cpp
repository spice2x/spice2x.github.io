#include <initguid.h>

#include "hotplug.h"

#include <dbt.h>
#include <devguid.h>
#include <hidclass.h>
#include <ntddkbd.h>
#include <ntddmou.h>
#include <objbase.h>

#include "misc/eamuse.h"
#include "rawinput.h"
#include "util/fileutils.h"
#include "util/logging.h"

namespace rawinput {

    static bool is_rawinput_interface(const GUID &guid) {
        return IsEqualGUID(guid, GUID_DEVINTERFACE_HID)
            || IsEqualGUID(guid, GUID_DEVINTERFACE_KEYBOARD)
            || IsEqualGUID(guid, GUID_DEVINTERFACE_MOUSE);
    }

    HotplugManager::HotplugManager(RawInputManager *ri_mgr, HWND hWnd) : ri_mgr(ri_mgr) {
        auto register_notification = [hWnd](const GUID &guid, const char *name) -> HANDLE {
            DEV_BROADCAST_DEVICEINTERFACE settings {
                    .dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE),
                    .dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE,
                    .dbcc_reserved = 0,
                    .dbcc_classguid = guid,
                    .dbcc_name = {0},
            };

            auto notification = RegisterDeviceNotification(hWnd, &settings, DEVICE_NOTIFY_WINDOW_HANDLE);
            if (notification == nullptr) {
                log_warning("hotplug", "failed to register {} notifications: {}", name, GetLastError());
            }
            return notification;
        };

        // register notifications
        this->hotplug_hid = register_notification(GUID_DEVINTERFACE_HID, "HID");
        this->hotplug_keyboard = register_notification(GUID_DEVINTERFACE_KEYBOARD, "keyboard");
        this->hotplug_mouse = register_notification(GUID_DEVINTERFACE_MOUSE, "mouse");
    }

    HotplugManager::~HotplugManager() {

        // unregister notifications
        for (auto notification : {this->hotplug_hid, this->hotplug_keyboard, this->hotplug_mouse}) {
            if (notification != nullptr) {
                UnregisterDeviceNotification(notification);
            }
        }
    }

    LRESULT CALLBACK HotplugManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        // check for device change
        if (msg == WM_DEVICECHANGE) {

            // check type
            switch (wParam) {
                case DBT_DEVICEARRIVAL: {

                    // check arrived device type
                    auto hdr = (DEV_BROADCAST_HDR*) lParam;
                    switch (hdr->dbch_devicetype) {
                        case DBT_DEVTYP_DEVICEINTERFACE: {
                            auto dev = (const DEV_BROADCAST_DEVICEINTERFACE *) hdr;

                            // check if this is one of the registered RawInput interfaces
                            if (!is_rawinput_interface(dev->dbcc_classguid)) {
                                break;
                            }

                            // keyboard and mouse class events accompany the HID event, so scan MIDI only once
                            if (IsEqualGUID(dev->dbcc_classguid, GUID_DEVINTERFACE_HID)) {
                                this->ri_mgr->midi_scan_start();
                            }

                            // hotplug
                            std::string name(dev->dbcc_name);
                            this->ri_mgr->devices_scan_rawinput(name);
                            this->ri_mgr->devices_register();

                            break;
                        }
                        case DBT_DEVTYP_VOLUME: {
                            auto dev = (const DEV_BROADCAST_VOLUME *) hdr;
                            auto unitmask = dev->dbcv_unitmask;

                            // check drive
                            unsigned long bit;
                            while (_BitScanForward(&bit, unitmask)) {

                                // remove drive from unitmask so it is not scanned again
                                unitmask &= ~(1 << bit);

                                // convert bit to drive letter char
                                char drive = 'A' + bit;
                                log_info("hotplug", "detected volume arrival: {}", drive);

#ifndef SPICETOOLS_SPICECFG_STANDALONE
                                // auto insert cards
                                for (int player = 0; player <= 1; player++) {
                                    std::string path = to_string(drive) + ":\\card" + to_string(player) + ".txt";
                                    if (fileutils::file_exists(path)) {
                                        uint8_t card_data[8];
                                        if (eamuse_get_card_from_file(path, card_data, player)) {
                                            eamuse_card_insert(player, card_data);
                                        }
                                    }
                                }
#endif
                            }
                            break;
                        }
                    }

                    // success
                    return TRUE;
                }
                case DBT_DEVICEREMOVECOMPLETE: {

                    // check arrived device type
                    auto hdr = (DEV_BROADCAST_HDR*) lParam;
                    switch (hdr->dbch_devicetype) {
                        case DBT_DEVTYP_DEVICEINTERFACE: {
                            auto dev = (DEV_BROADCAST_DEVICEINTERFACE*) hdr;
                            if (!is_rawinput_interface(dev->dbcc_classguid)) {
                                break;
                            }
                            std::string name(dev->dbcc_name);

                            // destruct device
                            log_misc("hotplug", "device interface removal: {}", name);
                            this->ri_mgr->devices_remove(name);
                        }
                    }

                    // success
                    return TRUE;
                }
                case DBT_DEVNODES_CHANGED: {
                    // catch-all device-tree change: MIDI and XInput have no targeted
                    // arrival/removal notification, so this is our only hook to detect them
                    // can be a little noisy as it gets called on every device
                    log_misc("hotplug", "device tree changed, rescanning MIDI + XInput");
                    this->ri_mgr->midi_scan_start();
                    this->ri_mgr->devices_scan_xinput();
                    break;
                }
            }
        }

        // default
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
