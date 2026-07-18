#include "rawinput_handles.h"

void rawinput::RawInputHandles::add(Device *device) {
    std::lock_guard<std::mutex> lock(this->mutex);
    this->devices[device->handle] = device;
}

void rawinput::RawInputHandles::remove(Device *device) {
    std::lock_guard<std::mutex> lock(this->mutex);

    // teardown is shared by every device type, and handles can be reused; only erase
    // an entry that still belongs to this exact RawInput device
    auto it = this->devices.find(device->handle);
    if (it != this->devices.end() && it->second == device) {
        this->devices.erase(it);
    }
}

rawinput::RawInputHandles::AcquiredDevice rawinput::RawInputHandles::acquire(HANDLE handle) {
    std::lock_guard<std::mutex> index_lock(this->mutex);
    auto it = this->devices.find(handle);
    if (it == this->devices.end()) {
        return {};
    }

    auto *device = it->second;

    // lock the device before releasing the index so teardown cannot invalidate it
    return {device, std::unique_lock<std::mutex>(*device->mutex)};
}
