#pragma once

#include <objbase.h>

#include "util/logging.h"

template<typename T>
class CoTaskMemPtr {
public:
    explicit CoTaskMemPtr() {
    }

    explicit CoTaskMemPtr(T *value) : _ptr(value) {
    }

    CoTaskMemPtr(const CoTaskMemPtr &) = delete;
    CoTaskMemPtr &operator=(const CoTaskMemPtr &) = delete;

    ~CoTaskMemPtr() {
        this->drop();
    }

    void drop() const noexcept {
        if (_ptr) {
            log_misc("co_task_mem_ptr", "dropping {}", fmt::ptr(_ptr));
            CoTaskMemFree(_ptr);
        }
    }

    T *data() const noexcept {
        return _ptr;
    }

    T **ppv() noexcept {
        this->drop();

        return &_ptr;
    }

    T *operator->() const noexcept {
        return _ptr;
    }

private:
    T *_ptr = nullptr;
};