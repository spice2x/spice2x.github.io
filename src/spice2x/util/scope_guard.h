#pragma once

#include <functional>

class scope_guard {
private:
    std::function<void()> f;
public:
    explicit scope_guard(std::function<void()>&& f) : f(std::move(f)) {}
    ~scope_guard() {
        f();
    }

    scope_guard(const scope_guard&) = delete;
    scope_guard& operator=(const scope_guard&) = delete;

    scope_guard(scope_guard&&) = delete;
    scope_guard& operator=(scope_guard&&) = delete;
};