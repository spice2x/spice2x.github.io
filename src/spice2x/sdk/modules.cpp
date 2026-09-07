#include "modules.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>

namespace sdk::modules {

namespace {

// RAII wrapper for a module handle
struct ModuleReference {
    HMODULE handle = nullptr;

    ModuleReference() = default;
    ModuleReference(const ModuleReference &) = delete;
    ModuleReference &operator=(const ModuleReference &) = delete;
    ~ModuleReference() {
        if (handle) {
            FreeLibrary(handle);
        }
    }
};

template<typename Value>
bool read_module_value(uintptr_t base, size_t offset, Value &value) {
    if (offset > std::numeric_limits<uintptr_t>::max() - base ||
        sizeof(Value) > std::numeric_limits<uintptr_t>::max() - (base + offset)) {
        return false;
    }

    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(base + offset),
                             &value, sizeof(value), &copied) && copied == sizeof(value);
}

}

SPICE_SDK_STATUS_CODE get_module_info(const wchar_t *module_name, SPICE_SDK_MODULE_INFO *info) {
    if (!module_name || !module_name[0]) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (!info) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_2;
    }
    if (info->size < sizeof(SPICE_SDK_MODULE_INFO)) {
        return SPICE_SDK_STATUS_TOO_SMALL;
    }

    // Keep the module loaded while inspecting its headers.
    ModuleReference module;
    if (!GetModuleHandleExW(0, module_name, &module.handle)) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }

    // Locate the PE header through the DOS header.
    const auto base = reinterpret_cast<uintptr_t>(module.handle);
    IMAGE_DOS_HEADER dos{};
    if (!read_module_value(base, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos))) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }

    const auto nt_offset = static_cast<size_t>(dos.e_lfanew);
    DWORD signature{};
    IMAGE_FILE_HEADER header{};
    if (!read_module_value(base, nt_offset, signature) || signature != IMAGE_NT_SIGNATURE ||
        !read_module_value(base, nt_offset + sizeof(signature), header)) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }

    // The optional-header magic selects the PE32 or PE32+ layout.
    const auto optional_offset = nt_offset + sizeof(signature) + sizeof(header);
    WORD magic{};
    if (header.SizeOfOptionalHeader < sizeof(magic) || !read_module_value(base, optional_offset, magic)) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }

    SPICE_SDK_MODULE_INFO result{};
    result.size = info->size;
    result.base = base;
    result.timestamp = header.TimeDateStamp;
    result.machine = header.Machine;

    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_OPTIONAL_HEADER64 optional{};
        if (header.SizeOfOptionalHeader < sizeof(optional) || !read_module_value(base, optional_offset, optional)) {
            return SPICE_SDK_STATUS_GENERIC_ERROR;
        }
        result.image_size = optional.SizeOfImage;
        result.entry_point = optional.AddressOfEntryPoint;
    } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        IMAGE_OPTIONAL_HEADER32 optional{};
        if (header.SizeOfOptionalHeader < sizeof(optional) || !read_module_value(base, optional_offset, optional)) {
            return SPICE_SDK_STATUS_GENERIC_ERROR;
        }
        result.image_size = optional.SizeOfImage;
        result.entry_point = optional.AddressOfEntryPoint;
    } else {
        return SPICE_SDK_STATUS_NOT_SUPPORTED;
    }

    // Reject headers or entry points outside the image.
    if (optional_offset > result.image_size || header.SizeOfOptionalHeader > result.image_size - optional_offset ||
        result.entry_point >= result.image_size) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }

    // Leave the caller's output untouched on failure.
    *info = result;
    return SPICE_SDK_STATUS_SUCCESS;
}

SPICE_SDK_STATUS_CODE get_plugin_directory(const std::vector<SdkModule> &registered_modules,
    const void *plugin_address, wchar_t *buffer, uint32_t *size) {
    if (!plugin_address) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }
    if (!size) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_3;
    }

    // Resolve the address and require a registered plugin.
    ModuleReference module;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(plugin_address), &module.handle) ||
        std::none_of(registered_modules.begin(), registered_modules.end(), [&](const SdkModule &entry) {
            return entry.module == module.handle;
        })) {
        return SPICE_SDK_STATUS_INVALID_ARGUMENT_1;
    }

    try {
        // Reject truncated paths before extracting the directory.
        std::array<wchar_t, 32768> filename{};
        const auto length = GetModuleFileNameW(module.handle, filename.data(), static_cast<DWORD>(filename.size()));
        if (!length || length >= filename.size()) {
            return SPICE_SDK_STATUS_GENERIC_ERROR;
        }

        const auto path = std::filesystem::path(filename.data()).parent_path();

        // Report the full UTF-16 capacity, including the terminator.
        const auto &text = path.native();
        const auto required = static_cast<uint32_t>(text.size() + 1);
        const auto capacity = *size;
        *size = required;
        if (!buffer || capacity < required) {
            return SPICE_SDK_STATUS_TOO_SMALL;
        }

        // Copy only when the entire path fits.
        std::copy_n(text.c_str(), required, buffer);
        return SPICE_SDK_STATUS_SUCCESS;
    } catch (const std::exception &) {
        return SPICE_SDK_STATUS_GENERIC_ERROR;
    }
}

}