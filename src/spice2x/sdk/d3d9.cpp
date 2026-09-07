#include "d3d9.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace sdk::d3d9 {
namespace {

struct Renderer {
    spice_sdk_d3d9_callback_func *callback;
    void *userdata;
    bool ready = false;
    bool started = false;
};

// keep registration separate from serialized device access and callback execution.
std::mutex registry_mutex;
std::recursive_mutex execution_mutex;
std::condition_variable_any shutdown_condition;
std::vector<std::shared_ptr<Renderer>> renderers;

SPICE_SDK_D3D9_FRAME frame{sizeof(SPICE_SDK_D3D9_FRAME), nullptr, nullptr, 0, 0};
bool stopped = false;
bool resetting = false;
bool device_lost = false;
bool reset_in_progress = false;
bool shutdown_complete = false;
DWORD device_thread = 0;

// reject reentrant SDK graphics calls made by a renderer callback.
thread_local bool dispatching = false;
struct DispatchScope {
    DispatchScope() {
        dispatching = true;
    }

    ~DispatchScope() {
        dispatching = false;
    }
};

template<typename Interface>
struct ComReference {
    Interface *value = nullptr;

    ~ComReference() {
        if (value) {
            value->Release();
        }
    }
};

// restore game state after each renderer, including early exits during setup.
struct GraphicsState {
    IDirect3DDevice9 *device;
    ComReference<IDirect3DStateBlock9> block;
    std::array<ComReference<IDirect3DSurface9>, 4> targets;
    ComReference<IDirect3DSurface9> depth;
    D3DVIEWPORT9 viewport{};
    std::array<D3DMATRIX, 3> transforms{};
    std::array<bool, 3> have_transform{};
    DWORD target_count = 0;
    bool captured = false;

    explicit GraphicsState(IDirect3DDevice9 *device) : device(device) {
        D3DCAPS9 caps{};
        if (FAILED(device->GetDeviceCaps(&caps)) ||
            FAILED(device->CreateStateBlock(D3DSBT_ALL, &block.value)) ||
            FAILED(block.value->Capture()) || FAILED(device->GetViewport(&viewport))) {
            return;
        }

        // state blocks do not capture render targets or the depth surface.
        target_count = std::min<DWORD>(caps.NumSimultaneousRTs, targets.size());
        for (DWORD index = 0; index < target_count; ++index) {
            const auto status = device->GetRenderTarget(index, &targets[index].value);
            if (FAILED(status) && status != D3DERR_NOTFOUND) {
                return;
            }
        }

        const auto status = device->GetDepthStencilSurface(&depth.value);
        if (FAILED(status) && status != D3DERR_NOTFOUND) {
            return;
        }

        const std::array kinds{D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION};
        for (size_t index = 0; index < kinds.size(); ++index) {
            have_transform[index] = SUCCEEDED(
                device->GetTransform(kinds[index], &transforms[index]));
        }
        captured = true;
    }

    ~GraphicsState() {
        if (!captured) {
            return;
        }

        // unbind auxiliary surfaces before restoring targets of potentially different sizes.
        device->SetDepthStencilSurface(nullptr);
        for (DWORD index = 1; index < target_count; ++index) {
            device->SetRenderTarget(index, nullptr);
        }
        device->SetRenderTarget(0, targets[0].value);
        for (DWORD index = 1; index < target_count; ++index) {
            device->SetRenderTarget(index, targets[index].value);
        }
        device->SetDepthStencilSurface(depth.value);

        block.value->Apply();
        const std::array kinds{D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION};
        for (size_t index = 0; index < kinds.size(); ++index) {
            if (have_transform[index]) {
                device->SetTransform(kinds[index], &transforms[index]);
            }
        }
        device->SetViewport(&viewport);
    }
};

std::vector<std::shared_ptr<Renderer>> snapshot() {
    // callbacks run without the registry lock, while their entries remain alive.
    std::lock_guard lock(registry_mutex);
    return renderers;
}

void destroy_renderers() {
    for (const auto &renderer : snapshot()) {
        if (renderer->started) {
            renderer->callback(SPICE_SDK_D3D9_DESTROY, &frame, renderer->userdata);
        }
        renderer->ready = false;
        renderer->started = false;
    }

    frame = {sizeof(frame), nullptr, nullptr, 0, 0};
    resetting = false;
    device_lost = false;
    reset_in_progress = false;
    device_thread = 0;
}

void finish_shutdown() {
    if (shutdown_complete) {
        return;
    }

    destroy_renderers();
    {
        std::lock_guard lock(registry_mutex);
        renderers.clear();
    }

    shutdown_complete = true;
    shutdown_condition.notify_all();
}

}

SPICE_SDK_STATUS_CODE register_d3d9(const std::vector<SdkModule> &modules,
        spice_sdk_d3d9_callback_func *callback, void *userdata) {
    if (dispatching) {
        return SPICE_SDK_STATUS_NOT_SUPPORTED;
    }
    if (!callback) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    // only registered SDK modules may supply callbacks.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(callback), &owner) ||
        std::none_of(modules.begin(), modules.end(), [&](const SdkModule &module) {
            return module.module == owner;
        })) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    std::lock_guard lock(registry_mutex);
    if (stopped) {
        return SPICE_SDK_STATUS_TOO_LATE;
    }
    if (std::any_of(renderers.begin(), renderers.end(), [&](const auto &renderer) {
            return renderer->callback == callback && renderer->userdata == userdata;
        })) {
        return SPICE_SDK_STATUS_SUCCESS;
    }

    // keep callback code loaded for the lifetime of the process.
    try {
        auto renderer = std::make_shared<Renderer>(Renderer{callback, userdata});
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(callback), &owner)) {
            return SPICE_SDK_STATUS_GENERIC_ERROR;
        }
        renderers.push_back(std::move(renderer));
    } catch (...) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }

    return SPICE_SDK_STATUS_SUCCESS;
}

void draw(HWND window, IDirect3DDevice9 *device) {
    if (dispatching) {
        return;
    }

    std::lock_guard lock(execution_mutex);
    DispatchScope scope;

    if (frame.device && frame.device != device) {
        return;
    }
    device_thread = GetCurrentThreadId();
    if (stopped) {
        if (!reset_in_progress) {
            finish_shutdown();
        }
        return;
    }

    // latch the first presented device even before any renderers register.
    frame.device = device;
    frame.window = window;

    const auto current = snapshot();
    if (current.empty() || resetting || device_lost) {
        return;
    }

    ComReference<IDirect3DSurface9> backbuffer;
    D3DSURFACE_DESC description{};
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.value)) ||
        FAILED(backbuffer.value->GetDesc(&description))) {
        return;
    }
    frame = {sizeof(frame), device, window, description.Width, description.Height};

    for (const auto &renderer : current) {
        // isolate each renderer and give it the full backbuffer in its own scene.
        GraphicsState state(device);
        if (!state.captured) {
            continue;
        }

        if (FAILED(device->SetDepthStencilSurface(nullptr))) {
            continue;
        }
        bool target_ready = true;
        for (DWORD index = 1; index < state.target_count; ++index) {
            if (FAILED(device->SetRenderTarget(index, nullptr))) {
                target_ready = false;
            }
        }

        const D3DVIEWPORT9 viewport{0, 0, frame.width, frame.height, 0.0f, 1.0f};
        if (!target_ready || FAILED(device->SetRenderTarget(0, backbuffer.value)) ||
            FAILED(device->SetViewport(&viewport)) || FAILED(device->BeginScene())) {
            continue;
        }

        // dispatch READY to also recreate resources released before a successful reset.
        if (!renderer->ready) {
            renderer->started = true;
            renderer->callback(SPICE_SDK_D3D9_READY, &frame, renderer->userdata);
            renderer->ready = true;
        }

        renderer->callback(SPICE_SDK_D3D9_DRAW, &frame, renderer->userdata);
        device->EndScene();
    }
}

void present_complete(IDirect3DDevice9 *device, HRESULT result) {
    if (dispatching) {
        return;
    }

    std::lock_guard lock(execution_mutex);
    DispatchScope scope;

    if (frame.device == device) {
        device_thread = GetCurrentThreadId();
        if (stopped && !reset_in_progress) {
            finish_shutdown();
            return;
        }

        // a busy nonblocking present does not change the known device-loss state.
        if (SUCCEEDED(result)) {
            device_lost = false;
        } else if (result != D3DERR_WASSTILLDRAWING) {
            device_lost = true;
        }
    }
}

void invalidate(IDirect3DDevice9 *device) {
    if (dispatching) {
        return;
    }

    std::lock_guard lock(execution_mutex);
    DispatchScope scope;

    if (frame.device != device) {
        return;
    }

    device_thread = GetCurrentThreadId();
    if (stopped) {
        finish_shutdown();
        return;
    }

    // default-pool resources must be released before the native reset starts.
    reset_in_progress = true;
    resetting = true;
    for (const auto &renderer : snapshot()) {
        if (renderer->ready) {
            renderer->callback(SPICE_SDK_D3D9_INVALIDATE, &frame, renderer->userdata);
        }
        renderer->ready = false;
    }
}

void reset_complete(IDirect3DDevice9 *device, bool success) {
    if (dispatching) {
        return;
    }

    std::lock_guard lock(execution_mutex);
    DispatchScope scope;

    if (frame.device == device) {
        device_thread = GetCurrentThreadId();
        reset_in_progress = false;
        resetting = !success;
        device_lost = !success;

        if (stopped) {
            finish_shutdown();
        }
    }
}

void destroy(IDirect3DDevice9 *device) {
    if (dispatching) {
        return;
    }

    std::lock_guard lock(execution_mutex);
    DispatchScope scope;

    if (frame.device == device) {
        if (stopped) {
            finish_shutdown();
        } else {
            destroy_renderers();
        }
    }
}

void shutdown(bool graphics_stopped) {
    std::unique_lock lock(execution_mutex);
    if (shutdown_complete) {
        return;
    }

    // close registration before deciding where device resources can be released.
    {
        std::lock_guard registry_lock(registry_mutex);
        stopped = true;
    }

    const auto current = snapshot();
    const bool resources_started = std::any_of(
        current.begin(), current.end(), [](const auto &renderer) {
            return renderer->started;
        });

    if (!resources_started || (!reset_in_progress &&
            (graphics_stopped || device_thread == GetCurrentThreadId()))) {
        DispatchScope scope;
        finish_shutdown();
        return;
    }

    // a graphics boundary must drain callbacks when another thread requests shutdown.
    shutdown_condition.wait(lock, [] {
        return shutdown_complete;
    });
}

}