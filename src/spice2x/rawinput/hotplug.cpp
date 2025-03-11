#include <initguid.h>

#include "hotplug.h"

#include <dbt.h>
#include <devguid.h>

#include "misc/eamuse.h"
#include "rawinput.h"
#include "util/fileutils.h"
#include "util/logging.h"

namespace rawinput {

    DEFINE_GUID(GUID_HID, 0x4D1E55B2L, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30);

    HotplugManager::HotplugManager(RawInputManager *ri_mgr, HWND hWnd) : ri_mgr(ri_mgr) {

        // init settings
        DEV_BROADCAST_DEVICEINTERFACE settings_hid {
                .dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE),
                .dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE,
                .dbcc_reserved = 0,
                .dbcc_classguid = GUID_HID,
                .dbcc_name = {0},
        };

        // register notifications
        this->hotplug_hid = RegisterDeviceNotification(hWnd, &settings_hid, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (this->hotplug_hid == nullptr) {
            log_warning("hotplug", "failed to register HID notifications: {}", GetLastError());
        }
    }

    HotplugManager::~HotplugManager() {

        // unregister notifications
        if (this->hotplug_hid != nullptr) {
            UnregisterDeviceNotification(this->hotplug_hid);
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
                            this->ri_mgr->devices_scan_midi();

                            // check if class is not HID
                            if (memcmp(&dev->dbcc_classguid, &GUID_HID, sizeof(GUID_HID)) != 0)
                                break;

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
                            std::string name(dev->dbcc_name);

                            // destruct device
                            this->ri_mgr->devices_remove(name);
                        }
                    }

                    // success
                    return TRUE;
                }
                case 7: { // windows 10 reports this for MIDI?
                    this->ri_mgr->devices_scan_midi();
                    break;
                }
            }
        }

        // default
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
