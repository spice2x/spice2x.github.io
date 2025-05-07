#pragma once

#include <functional>
#include <thread>
#include <condition_variable>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

#include "device.h"
#include "hotplug.h"

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

        HotplugManager *hotplug;
        std::vector<Device> devices;
        HWND input_hwnd = nullptr;
        WNDCLASSEX input_hwnd_class {};
        std::thread *input_thread = nullptr;
        std::thread *flush_thread = nullptr;
        bool flush_thread_running = false;
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

        static std::string rawinput_get_device_name(HANDLE hDevice);
        static std::string rawinput_get_device_description(const DeviceInfo& info, const std::string &device_name);

        static LRESULT CALLBACK input_wnd_proc(HWND, UINT, WPARAM, LPARAM);
        static void CALLBACK input_midi_proc(HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
        static DeviceInfo get_device_info(const std::string &device_name);

    public:

        RawInputManager();
        ~RawInputManager();

        void stop();

        void sextet_register(
                const std::string &port_name,
                const std::string &alias = "Sextet",
                bool warn = true);

        void devices_scan_rawinput(const std::string &device_name = "");
        void devices_scan_midi();
        void devices_remove(const std::string &name);

        void devices_register();
        void devices_unregister();

        static void device_write_output(Device *device, bool only_updated = true);
        void devices_flush_output(bool optimized = true);

        void __stdcall devices_print();
        Device *devices_get(const std::string &name, bool updated = false);

        inline std::vector<Device> &devices_get() {
            return devices;
        }

        inline std::vector<Device *> devices_get_updated() {
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
