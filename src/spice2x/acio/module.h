#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

// macro for lazy typing of hooks
#define ACIO_MODULE_HOOK(f) this->hook(reinterpret_cast<void *>(f), #f)

namespace acio {

    /*
     * Hook Modes
     * Since some versions can't handle inline hooking
     */
    enum class HookMode {
        INLINE,
        IAT
    };

    // this makes logging easier
    const char *hook_mode_str(HookMode hook_mode);

    /*
     * The ACIO module itself
     * Inherit this for extending our libacio implementation
     */
    class ACIOModule {
    protected:

        // the magic
        void hook(void* func, const char *func_name);

    public:

        ACIOModule(std::string name, HMODULE module, HookMode hook_mode) :
            name(std::move(name)),
            module(module),
            hook_mode(hook_mode) {};

        virtual ~ACIOModule() = default;

        virtual void attach();

        // settings
        std::string name;
        HMODULE module;
        HookMode hook_mode;
        bool attached = false;

        // buffer state (optional)
        uint8_t *status_buffer = nullptr;
        size_t status_buffer_size = 0;
        bool *status_buffer_freeze = nullptr;
    };
}
