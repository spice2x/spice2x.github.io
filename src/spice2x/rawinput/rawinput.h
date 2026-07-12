#pragma once

#include <functional>
#include <thread>
#include <condition_variable>
#include <list>
#include <mutex>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

#include "device.h"
#include "hotplug.h"
#include "rawinput/xinput.h"
#include "util/scope_guard.h"

namespace rawinput {

    // settings
    extern bool NOLEGACY;

    extern uint8_t HID_LIGHT_BRIGHTNESS;

    extern bool ENABLE_SMX_STAGE;
    extern bool ENABLE_SMX_DEDICAB;

    extern int TOUCHSCREEN_RANGE_X;
    extern int TOUCHSCREEN_RANGE_Y;

    extern bool DUMP_HID_DEVICES_TO_LOG;

    extern uint32_t MIDI_NOTE_SUSTAIN;

    extern bool NAIVE_REQUIRE_FOCUS;
    extern bool RAWINPUT_REQUIRE_FOCUS;

    // while active, prevents overlay from accepting any input
    // can be used while OS modal dialog is shown on top of overlay/spicecfg
    // always prefer RAII set_os_window_focus_guard instead of the global bool
    extern bool OS_WINDOW_ACTIVE;
    inline scope_guard set_os_window_focus_guard() {
        rawinput::OS_WINDOW_ACTIVE = true;
        return scope_guard {[]() { rawinput::OS_WINDOW_ACTIVE = false; }};
    }

    struct DeviceCallback {
        void *data;
        std::function<void(void*, Device*)> f;
    };
    struct MidiCallback {
        void *data;
        std::function<void(void*, Device*, uint8_t cmd, uint8_t ch, uint8_t b1, uint8_t b2)> f;
    };

    enum class MidiNoteAlgorithm {
        LEGACY,
        V2,
        V2_DRUM
    };

    rawinput::MidiNoteAlgorithm get_midi_algorithm();
    void set_midi_algorithm(rawinput::MidiNoteAlgorithm new_algo);
    
    class RawInputManager {
    private:

        HotplugManager *hotplug = nullptr;

        // std::list (not std::vector) so adding/removing a device never invalidates
        // pointers to other elements. devices_get() hands out pointers that escape the
        // lock and are dereferenced later by pollers, so a concurrent scan must not
        // relocate existing devices
        std::list<Device> devices;
        std::recursive_mutex devices_mutex;

        WNDCLASSEX input_hwnd_class {};
        std::thread *input_thread = nullptr;
        std::thread *midi_thread = nullptr;

        // guards the MIDI scan scheduler flags below. only held around the flag
        // checks, never across the (slow) enumeration, so it makes "is a scan
        // running?" and the worker's "exit or rescan?" a single atomic decision -
        // a request can never slip into the gap between the two
        std::mutex midi_scan_m;

        // true while a scan worker thread is running. only one runs at a time
        bool midi_scan_active = false;

        // set when a scan is requested while one is already running, so the worker
        // rescans once instead of dropping the request (events that arrive during
        // the slow initial scan would otherwise be lost)
        bool midi_scan_pending = false;

        // MIDI handles pending close. devices_destruct() runs under devices_mutex but
        // midiInReset/midiInClose must not (they block on the MIDI callback, which
        // also takes devices_mutex), so handles are queued here and closed by
        // midi_close_deferred_flush() once the lock is released
        std::vector<HMIDIIN> midi_close_deferred;
        std::thread *flush_thread = nullptr;
        bool flush_thread_running = false;
        std::mutex flush_thread_m;
        std::condition_variable flush_thread_cv;
        std::thread *output_thread = nullptr;
        std::mutex output_thread_m;
        bool output_thread_ready = false;
        bool output_thread_running = false;
        std::condition_variable output_thread_cv;
        std::vector<DeviceCallback> callback_add;
        std::vector<DeviceCallback> callback_change;
        std::vector<MidiCallback> callback_midi;

        void input_hwnd_create();
        void input_hwnd_destroy();
        void devices_reload();
        void midi_scan_join();
        void midi_close_deferred_flush();
        void devices_scan_rawinput(RAWINPUTDEVICELIST *device, bool log = true);
        void devices_scan_piuio();
        void devices_scan_smxstage();
        void devices_scan_smxdedicab();
        void devices_destruct();
        void devices_destruct(Device *device, bool log = true);
        void flush_start();
        void flush_stop();
        void output_start();
        void output_stop();
        void devices_write_output_snapshot(bool only_updated);

        static std::string rawinput_get_device_name(HANDLE hDevice);
        static std::string rawinput_get_device_description(const DeviceInfo& info, const std::string &device_name);

        static LRESULT CALLBACK input_wnd_proc(HWND, UINT, WPARAM, LPARAM);
        static void CALLBACK input_midi_proc(HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
        static DeviceInfo get_device_info(const std::string &device_name);

    public:

        HWND input_hwnd = nullptr;
        std::unique_ptr<xinput::XInputManager> XINPUT_MGR = nullptr;

        RawInputManager();
        ~RawInputManager();

        void stop();

        void sextet_register(
                const std::string &port_name,
                const std::string &alias = "Sextet",
                bool warn = true);

        void devices_scan_rawinput(const std::string &device_name = "");
        void devices_scan_midi();
        void midi_scan_start();
        void devices_scan_xinput();
        void devices_remove(const std::string &name);

        void devices_register();
        void devices_unregister();

        static void device_write_output(Device *device, bool only_updated = true);
        void devices_flush_output(bool optimized = true);

        void __stdcall devices_print();
        Device *devices_get(const std::string &name, bool updated = false);

        inline std::list<Device> &devices_get() {
            return devices;
        }

        inline std::vector<Device *> devices_get_updated() {
            std::lock_guard<std::recursive_mutex> lock(devices_mutex);
            std::vector<Device *> updated;
            for (auto &device : devices_get()) {
                device.mutex->lock();
                if (device.updated) {
                    device.updated = false;
                    device.mutex->unlock();
                    updated.emplace_back(&device);
                } else {
                    device.mutex->unlock();
                }
            }
            return updated;
        }

        inline void devices_midi_freeze(bool freeze) {
            std::lock_guard<std::recursive_mutex> lock(devices_mutex);
            for (auto &device : devices_get()) {
                if (device.type == MIDI) {
                    device.midiInfo->freeze = freeze;
                    if (!freeze) {
                        for (unsigned short index = 0; index < device.midiInfo->states.size(); index++) {
                            device.midiInfo->states[index] = false;
                        }
                    }
                }
            }
        }

        void add_callback_add(void *data, std::function<void(void *, Device *)> callback);
        void remove_callback_add(void *data, const std::function<void(void *, Device *)> &callback);
        void add_callback_change(void *data, std::function<void(void *, Device *)> callback);
        void remove_callback_change(void *data, const std::function<void(void *, Device *)>& callback);
        void add_callback_midi(void *data, std::function<void(void *, Device *,
                uint8_t, uint8_t, uint8_t, uint8_t)> callback);
        void remove_callback_midi(void *data, const std::function<void(void *, Device *,
                uint8_t, uint8_t, uint8_t, uint8_t)>& callback);
    };
}
