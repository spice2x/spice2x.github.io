#pragma once

#include <mutex>
#include <unordered_map>

#include <windows.h>

#include "device.h"

namespace rawinput {

    class RawInputHandles {
    public:
        struct AcquiredDevice {
            Device *device = nullptr;
            std::unique_lock<std::mutex> lock;
        };

        void add(Device *device);
        void remove(Device *device);
        AcquiredDevice acquire(HANDLE handle);

    private:
        // non-owning index; RawInputManager owns the stable device slots
        std::unordered_map<HANDLE, Device *> devices;
        std::mutex mutex;
    };
}
