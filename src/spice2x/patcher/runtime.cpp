#include "internal.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <psapi.h>
#include <sstream>
#include <unordered_set>
#include "avs/game.h"
#include "cfg/configurator.h"
#include "external/robin_hood.h"
#include "launcher/launcher.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/memutils.h"
#include "util/sigscan.h"
#include "util/utils.h"

// std::min
#ifdef min
#undef min
#endif

// std::max
#ifdef max
#undef max
#endif

namespace patcher {

    robin_hood::unordered_map<std::string, std::unique_ptr<std::vector<uint8_t>>> DLL_MAP;
    robin_hood::unordered_map<std::string, std::unique_ptr<std::vector<uint8_t>>> DLL_MAP_ORG;

    void clear_dll_maps() {
        DLL_MAP.clear();
        DLL_MAP_ORG.clear();
    }

    void PatchManager::hard_apply_patches() {
        std::vector<std::filesystem::path> written_list;

        // dll_name -> in-memory image; populated lazily, written back once at
        // the end. Avoids re-reading and re-writing the full DLL per patch,
        // which used to dominate the cost of "Overwrite game files" on games
        // with large patch lists (and leaked a vector per call via bin_read's
        // raw new'd return).
        robin_hood::unordered_map<std::string, std::unique_ptr<std::vector<uint8_t>>> dll_cache;
        std::unordered_set<std::string> dirty;

        auto load_dll = [&](const std::string &dll_name) -> std::vector<uint8_t> * {
            auto it = dll_cache.find(dll_name);
            if (it != dll_cache.end()) {
                return it->second ? it->second.get() : nullptr;
            }
            const auto dll_path = MODULE_PATH / dll_name;
            create_dll_backup(written_list, dll_path);
            std::unique_ptr<std::vector<uint8_t>> owned(fileutils::bin_read(dll_path));
            if (!owned || owned->empty()) {
                // remember the miss so subsequent patches don't re-read/backup
                dll_cache.emplace(dll_name, nullptr);
                return nullptr;
            }
            auto *ptr = owned.get();
            dll_cache.emplace(dll_name, std::move(owned));
            return ptr;
        };

        for (auto &patch : patches) {
            switch (patch.type) {
            case PatchType::Memory:
                for (auto &memory_patch : patch.patches_memory) {
                    auto *dll_data = load_dll(memory_patch.dll_name);
                    if (!dll_data) {
                        continue;
                    }
                    const auto max_len = std::max(
                        memory_patch.data_disabled_len, memory_patch.data_enabled_len);
                    if (memory_patch.data_offset + max_len <= dll_data->size()) {
                        if (patch.enabled) {
                            memcpy(dll_data->data() + memory_patch.data_offset,
                                memory_patch.data_enabled.get(),
                                memory_patch.data_enabled_len);
                        } else {
                            memcpy(dll_data->data() + memory_patch.data_offset,
                                memory_patch.data_disabled.get(),
                                memory_patch.data_disabled_len);
                        }
                        dirty.insert(memory_patch.dll_name);
                    }
                }
                break;
            case PatchType::Union:
                if (!patch.enabled) {
                    break;
                }
                for (auto &union_patch : patch.patches_union) {
                    if (union_patch.name == patch.selected_union_name) {
                        auto *dll_data = load_dll(union_patch.dll_name);
                        if (!dll_data) {
                            break;
                        }
                        if (union_patch.offset + union_patch.data_len <= dll_data->size()) {
                            memcpy(dll_data->data() + union_patch.offset,
                                union_patch.data.get(), union_patch.data_len);
                            dirty.insert(union_patch.dll_name);
                        }
                        break;
                    }
                }
                break;
            case PatchType::Integer: {
                if (!patch.enabled) {
                    break;
                }
                auto &numpatch = patch.patch_number;
                auto *dll_data = load_dll(numpatch.dll_name);
                if (!dll_data) {
                    break;
                }
                if (numpatch.data_offset + numpatch.size_in_bytes <= dll_data->size()) {
                    int_to_little_endian_bytes(
                        numpatch.value,
                        dll_data->data() + numpatch.data_offset,
                        numpatch.size_in_bytes);
                    dirty.insert(numpatch.dll_name);
                }
                break;
            }
            default:
                break;
            }
        }

        // single write-back per dirty DLL
        for (const auto &dll_name : dirty) {
            const auto &data = dll_cache.at(dll_name);
            if (data) {
                fileutils::bin_write(MODULE_PATH / dll_name, data->data(), data->size());
            }
        }
    }

    PatchStatus is_patch_active(PatchData &patch) {

        // check patch type
        switch (patch.type) {
        case PatchType::Memory: {

            // iterate patches
            bool enabled = false;
            bool disabled = false;
            for (auto &memory_patch : patch.patches_memory) {
                auto max_size = std::max(memory_patch.data_enabled_len, memory_patch.data_disabled_len);

                // check for error to not try to get the pointer every frame
                if (memory_patch.fatal_error) {
                    if (cfg::CONFIGURATOR_STANDALONE) {
                        patch.unverified = true;
                        return patch.enabled ? PatchStatus::Enabled : PatchStatus::Disabled;
                    }
                    return PatchStatus::Error;
                }

                // find data pointer if not known yet
                if (memory_patch.data_offset_ptr == nullptr) {
                    // check if file exists
                    auto dll_path = MODULE_PATH / memory_patch.dll_name;
                    if (!fileutils::file_exists(dll_path)) {
                        // file does not exist so that's pretty fatal
                        memory_patch.fatal_error = true;
                        patch.error_reason = "DLL not found on disk";
                        return PatchStatus::Error;
                    }

                    // standalone mode
                    if (cfg::CONFIGURATOR_STANDALONE) {
                        auto file = find_in_dll_map(
                            memory_patch.dll_name, memory_patch.data_offset, max_size);
                        if (!file) {
                            patch.error_reason = "Invalid DLL or offset";
                            return PatchStatus::Error;
                        }
                        memory_patch.data_offset_ptr = &(*file)[memory_patch.data_offset];

                    } else {
                        // get module
                        auto module = libutils::try_module(dll_path);
                        if (!module) {
                            // no fatal error, might just not be loaded yet
                            patch.error_reason = "DLL not loaded into memory";
                            return PatchStatus::Error;
                        }

                        // convert offset to RVA
                        auto offset = libutils::offset2rva(dll_path, memory_patch.data_offset);
                        if (offset == -1) {
                            // RVA not found means unrecoverable
                            memory_patch.fatal_error = true;
                            patch.error_reason = "RVA not found";
                            return PatchStatus::Error;
                        }

                        // get module information
                        MODULEINFO module_info {};
                        if (!GetModuleInformation(
                                GetCurrentProcess(),
                                module,
                                &module_info,
                                sizeof(MODULEINFO))) {
                            // hmm, maybe try again sometime, not fatal
                            patch.error_reason = "Failed to get module info";
                            return PatchStatus::Error;
                        }

                        // check bounds
                        auto max_offset = static_cast<uintptr_t>(offset) + max_size;
                        auto image_size = static_cast<uintptr_t>(module_info.SizeOfImage);
                        if (max_offset >= image_size) {
                            // outside of bounds, invalid patch, fatal
                            memory_patch.fatal_error = true;
                            patch.error_reason = "Invalid DLL or offset";
                            return PatchStatus::Error;
                        }

                        // save pointer
                        auto dll_base = reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll);
                        memory_patch.data_offset_ptr = reinterpret_cast<uint8_t *>(dll_base + offset);
                    }
                }

                // virtual protect
                memutils::VProtectGuard guard(memory_patch.data_offset_ptr, max_size);

                // compare
                if (!guard.is_bad_address() && !memcmp(
                    memory_patch.data_enabled.get(),
                    memory_patch.data_offset_ptr,
                    memory_patch.data_enabled_len)) {
                    enabled = true;
                } else if (!guard.is_bad_address() && !memcmp(
                    memory_patch.data_disabled.get(),
                    memory_patch.data_offset_ptr,
                    memory_patch.data_disabled_len)) {
                    disabled = true;
                } else {
                    patch.error_reason = "Bad patch; patch is neither on or off (single patch)";
                    return PatchStatus::Error;
                }
            }
            // check detection flags
            if (enabled && disabled) {
                patch.error_reason = "Bad patch; patch is both on and off (cumulative)";
                return PatchStatus::Error;
            } else if (enabled) {
                return PatchStatus::Enabled;
            } else if (disabled) {
                return PatchStatus::Disabled;
            } else {
                patch.error_reason = "Bad patch; patch is neither on or off (cumulative)";
                return PatchStatus::Error;
            }
        }
        case PatchType::Signature: {
            return PatchStatus::Error;
        }
        case PatchType::Union: {
            // iterate patches
            bool match_found = false;
            patch.selected_union_name = "";
            for (auto &union_patch : patch.patches_union) {
                // check for error to not try to get the pointer every frame
                if (union_patch.fatal_error) {
                    if (cfg::CONFIGURATOR_STANDALONE) {
                        patch.unverified = true;
                        return patch.enabled ? PatchStatus::Enabled : PatchStatus::Disabled;
                    }
                    return PatchStatus::Error;
                }

                // find data pointer if not known yet
                if (union_patch.data_offset_ptr == nullptr) {
                    // check if file exists
                    auto dll_path = MODULE_PATH / union_patch.dll_name;
                    if (!fileutils::file_exists(dll_path)) {
                        // file does not exist so that's pretty fatal
                        union_patch.fatal_error = true;
                        patch.error_reason = "DLL not found on disk";
                        return PatchStatus::Error;
                    }

                    // standalone mode
                    if (cfg::CONFIGURATOR_STANDALONE) {
                        auto file = find_in_dll_map(
                            union_patch.dll_name, union_patch.offset, union_patch.data_len);
                        if (!file) {
                            patch.error_reason = "Invalid DLL or offset";
                            return PatchStatus::Error;
                        }
                        union_patch.data_offset_ptr = &(*file)[union_patch.offset];

                    } else {
                        // get module
                        auto module = libutils::try_module(dll_path);
                        if (!module) {
                            // no fatal error, might just not be loaded yet
                            patch.error_reason = "DLL not loaded into memory";
                            return PatchStatus::Error;
                        }

                        // convert offset to RVA
                        auto offset = libutils::offset2rva(dll_path, union_patch.offset);
                        if (offset == -1) {
                            // RVA not found means unrecoverable
                            union_patch.fatal_error = true;
                            patch.error_reason = "RVA not found";
                            return PatchStatus::Error;
                        }

                        // get module information
                        MODULEINFO module_info {};
                        if (!GetModuleInformation(
                                GetCurrentProcess(),
                                module,
                                &module_info,
                                sizeof(MODULEINFO))) {
                            // hmm, maybe try again sometime, not fatal
                            patch.error_reason = "Failed to get module info";
                            return PatchStatus::Error;
                        }

                        // check bounds
                        auto max_offset = static_cast<uintptr_t>(offset) + union_patch.data_len;
                        auto image_size = static_cast<uintptr_t>(module_info.SizeOfImage);
                        if (max_offset >= image_size) {
                            // outside of bounds, invalid patch, fatal
                            union_patch.fatal_error = true;
                            patch.error_reason = "Invalid DLL or offset";
                            return PatchStatus::Error;
                        }

                        // save pointer
                        auto dll_base = reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll);
                        union_patch.data_offset_ptr = reinterpret_cast<uint8_t *>(dll_base + offset);
                    }
                }

                // virtual protect
                memutils::VProtectGuard guard(union_patch.data_offset_ptr, union_patch.data_len);
                if (guard.is_bad_address()) {
                    patch.error_reason = "Invalid offset, bad address";
                    return PatchStatus::Error;
                }

                // is this union patch enabled in DLL?
                if (!match_found &&
                    memcmp(union_patch.data.get(), union_patch.data_offset_ptr, union_patch.data_len) == 0) {
                    match_found = true;
                    patch.selected_union_name = union_patch.name;
                }

                // if everything is OK, continue to check other patches in this union
            }
            // none of the union patches match what's in the DLL
            if (!match_found) {
                patch.error_reason = "No match found in union";
                return PatchStatus::Error;
            }
            return patch.enabled ? PatchStatus::Enabled : PatchStatus::Disabled;
        }
        case PatchType::Integer: {
            auto& numpatch = patch.patch_number;
            numpatch.value = 0;

            // check for fatal error and give up early
            if (numpatch.fatal_error) {
                if (cfg::CONFIGURATOR_STANDALONE) {
                    patch.unverified = true;
                    return patch.enabled ? PatchStatus::Enabled : PatchStatus::Disabled;
                }
                return PatchStatus::Error;
            }

            // find data pointer if not known yet
            if (numpatch.data_offset_ptr == nullptr) {
                // check if file exists
                auto dll_path = MODULE_PATH / numpatch.dll_name;
                if (!fileutils::file_exists(dll_path)) {
                    // file does not exist so that's pretty fatal
                    numpatch.fatal_error = true;
                    patch.error_reason = "DLL not found on disk";
                    return PatchStatus::Error;
                }

                // standalone mode
                if (cfg::CONFIGURATOR_STANDALONE) {
                    const auto file = find_in_dll_map(
                        numpatch.dll_name, numpatch.data_offset, numpatch.size_in_bytes);
                    if (!file) {
                        patch.error_reason = "Invalid DLL or offset";
                        return PatchStatus::Error;
                    }
                    numpatch.data_offset_ptr = &(*file)[numpatch.data_offset];
                } else {
                    // get module
                    const auto module = libutils::try_module(dll_path);
                    if (!module) {
                        // no fatal error, might just not be loaded yet
                        patch.error_reason = "DLL not loaded into memory";
                        return PatchStatus::Error;
                    }

                    // convert offset to RVA
                    const auto offset = libutils::offset2rva(dll_path, numpatch.data_offset);
                    if (offset == -1) {
                        // RVA not found means unrecoverable
                        numpatch.fatal_error = true;
                        patch.error_reason = "RVA not found";
                        return PatchStatus::Error;
                    }

                    // get module information
                    MODULEINFO module_info {};
                    if (!GetModuleInformation(
                            GetCurrentProcess(),
                            module,
                            &module_info,
                            sizeof(MODULEINFO))) {

                        // hmm, maybe try again sometime, not fatal
                        patch.error_reason = "Failed to get module info";
                        return PatchStatus::Error;
                    }

                    // check bounds
                    const auto max_offset = static_cast<uintptr_t>(offset) + numpatch.size_in_bytes;
                    const auto image_size = static_cast<uintptr_t>(module_info.SizeOfImage);
                    if (max_offset >= image_size) {
                        // outside of bounds, invalid patch, fatal
                        numpatch.fatal_error = true;
                        patch.error_reason = "Invalid DLL or offset";
                        return PatchStatus::Error;
                    }

                    // save pointer so we don't have to do this again next frame
                    const auto dll_base = reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll);
                    numpatch.data_offset_ptr = reinterpret_cast<uint8_t *>(dll_base + offset);
                }
            }

            // virtual protect
            memutils::VProtectGuard guard(numpatch.data_offset_ptr, numpatch.size_in_bytes);
            if (guard.is_bad_address()) {
                patch.error_reason = "Invalid offset, bad address";
                return PatchStatus::Error;
            }

            // what is the current value? check bounds
            const auto value_in_dll =
                parse_little_endian_int(numpatch.data_offset_ptr, numpatch.size_in_bytes);
            if (value_in_dll < numpatch.min || numpatch.max < value_in_dll) {
                patch.error_reason = "Number out of range, check min/max";
                return PatchStatus::Error;
            }

            patch.patch_number.value = value_in_dll;
            return patch.enabled ? PatchStatus::Enabled : PatchStatus::Disabled;
        }
        case PatchType::Unknown:
        default:
            patch.error_reason = "Unknown patch type";
            return PatchStatus::Error;
        }
    }

    bool apply_patch(PatchData &patch, bool active) {

        // check patch type
        switch (patch.type) {
        case PatchType::Memory: {

            // iterate memory patches
            for (auto &memory_patch : patch.patches_memory) {

                /*
                * we won't use the cached data_offset_ptr here
                * that makes it more reliable, also only happens on load/toggle
                */

                // determine source/target buffer/size
                uint8_t *src_buf = active
                    ? memory_patch.data_disabled.get()
                    : memory_patch.data_enabled.get();
                size_t src_len = active
                    ? memory_patch.data_disabled_len
                    : memory_patch.data_enabled_len;
                uint8_t *target_buf = active
                    ? memory_patch.data_enabled.get()
                    : memory_patch.data_disabled.get();
                size_t target_len = active
                    ? memory_patch.data_enabled_len
                    : memory_patch.data_disabled_len;

                // standalone mode
                if (cfg::CONFIGURATOR_STANDALONE) {
                    auto max_len = std::max(src_len, target_len);
                    // find file from DLL_MAP
                    auto dll_file = find_in_dll_map(
                        memory_patch.dll_name, memory_patch.data_offset, max_len);
                    if (!dll_file) {
                        return false;
                    }
                    // find offset into file
                    if (memory_patch.data_offset_ptr == nullptr) {
                        memory_patch.data_offset_ptr =
                            &(*dll_file)[memory_patch.data_offset];
                    }
                    if (memory_patch.data_offset_ptr == nullptr) {
                        return false;
                    }
                    // copy target to memory if src matches
                    if (memcmp(memory_patch.data_offset_ptr, src_buf, src_len) == 0) {
                        memcpy(memory_patch.data_offset_ptr, target_buf, target_len);
                    }

                } else {

                    // get pointer to offset
                    auto max_len = std::max(src_len, target_len);
                    if (memory_patch.data_offset_ptr == nullptr) {
                        memory_patch.data_offset_ptr =
                            get_dll_offset_for_patch_apply(
                                memory_patch.dll_name,
                                memory_patch.data_offset,
                                max_len);
                        if (memory_patch.data_offset_ptr == nullptr) {
                            return false;
                        }
                    }

                    // virtual protect
                    memutils::VProtectGuard guard(
                        memory_patch.data_offset_ptr, max_len);

                    // copy target to memory if src matches
                    if (memcmp(memory_patch.data_offset_ptr, src_buf, src_len) == 0) {
                        memcpy(memory_patch.data_offset_ptr, target_buf, target_len);
                    }
                }
            }

            // success
            return true;
        }
        case PatchType::Signature: {
            return false;
        }
        case PatchType::Union: {
            // Find the selected union patch
            auto it = std::find_if(patch.patches_union.begin(), patch.patches_union.end(),
                [&](const UnionPatch& up) { return up.name == patch.selected_union_name; });
            if (it == patch.patches_union.end()) {
                return false;
            }
            auto& union_patch = *it;

            // find data_offset_ptr
            if (cfg::CONFIGURATOR_STANDALONE) {
                // find file from DLL_MAP
                auto dll_file = find_in_dll_map(
                    union_patch.dll_name, union_patch.offset, union_patch.data_len);
                if (!dll_file) {
                    return false;
                }
                // find offset into file
                if (union_patch.data_offset_ptr == nullptr) {
                    union_patch.data_offset_ptr =
                        reinterpret_cast<uint8_t*>(union_patch.offset + &(*dll_file)[0]);
                }
            } else {
                if (union_patch.data_offset_ptr == nullptr) {
                    union_patch.data_offset_ptr = get_dll_offset_for_patch_apply(
                            union_patch.dll_name,
                            union_patch.offset,
                            union_patch.data_len);
                }
            }

            if (union_patch.data_offset_ptr == nullptr) {
                return false;
            }

            // Apply the selected union patch
            memutils::VProtectGuard guard(union_patch.data_offset_ptr, union_patch.data_len);
            if (active) {
                // apply the selected patch
                memcpy(union_patch.data_offset_ptr, union_patch.data.get(), union_patch.data_len);
                return true;
            } else {
                // restore from original file on disk
                return restore_bytes_from_dll_map_org(
                    union_patch.data_offset_ptr,
                    union_patch.dll_name,
                    union_patch.offset,
                    union_patch.data_len);
            }
        }
        case PatchType::Integer: {
            auto& numpatch = patch.patch_number;
            if (cfg::CONFIGURATOR_STANDALONE) {
                // find file from DLL_MAP
                auto dll_file = find_in_dll_map(
                    numpatch.dll_name, numpatch.data_offset, numpatch.size_in_bytes);
                if (!dll_file) {
                    return false;
                }
                // find offset into file
                if (numpatch.data_offset_ptr == nullptr) {
                    numpatch.data_offset_ptr =
                        reinterpret_cast<uint8_t*>(numpatch.data_offset + &(*dll_file)[0]);
                }
            } else {
                if (numpatch.data_offset_ptr == nullptr) {
                    numpatch.data_offset_ptr = get_dll_offset_for_patch_apply(
                            numpatch.dll_name,
                            numpatch.data_offset,
                            numpatch.size_in_bytes);
                }
            }
            if (numpatch.data_offset_ptr == nullptr) {
                return false;
            }
            memutils::VProtectGuard guard(numpatch.data_offset_ptr, numpatch.size_in_bytes);
            if (active) {
                // apply the selected patch
                int_to_little_endian_bytes(
                    numpatch.value, numpatch.data_offset_ptr, numpatch.size_in_bytes);
                return true;
            } else {
                // restore from original file on disk
                return restore_bytes_from_dll_map_org(
                    numpatch.data_offset_ptr,
                    numpatch.dll_name,
                    numpatch.data_offset,
                    numpatch.size_in_bytes);
            }
            return false;
        }
        default: {

            // unknown patch type - fail
            return false;
        }
        }
    }

    MemoryPatch SignaturePatch::to_memory(PatchData *patch) {

        // check if file exists
        auto dll_path = MODULE_PATH / dll_name;
        if (!fileutils::file_exists(dll_path)) {

            // file does not exist so that's pretty fatal
            return {.fatal_error = true};
        }

        // remove spaces
        signature.erase(std::remove(signature.begin(), signature.end(), ' '), signature.end());
        replacement.erase(std::remove(replacement.begin(), replacement.end(), ' '), replacement.end());

        // build pattern
        std::string pattern_str(signature);
        strreplace(pattern_str, "??", "00");
        strreplace(pattern_str, "XX", "00");
        auto pattern_bin = std::make_unique<uint8_t[]>(signature.length() / 2);
        if (!hex2bin(pattern_str.c_str(), pattern_bin.get())) {
            return {.fatal_error = true};
        }

        // build signature mask
        std::ostringstream signature_mask;
        for (size_t i = 0; i < signature.length(); i += 2) {
            if (signature[i] == '?' || signature[i] == 'X') {
                if (signature[i + 1] == '?' || signature[i + 1] == 'X') {
                    signature_mask << '?';
                } else {
                    return {.fatal_error = true};
                }
            } else {
                signature_mask << 'X';
            }
        }
        std::string signature_mask_str = signature_mask.str();

        // build replace data
        std::string replace_data_str(replacement);
        strreplace(replace_data_str, "??", "00");
        strreplace(replace_data_str, "XX", "00");
        auto replace_data_bin = std::make_unique<uint8_t[]>(replacement.length() / 2);
        if (!hex2bin(replace_data_str.c_str(), replace_data_bin.get())) {
            return {.fatal_error = true};
        }

        // build replace mask
        std::ostringstream replace_mask;
        for (size_t i = 0; i < replacement.length(); i += 2) {
            if (replacement[i] == '?' || replacement[i] == 'X') {
                if (replacement[i + 1] == '?' || replacement[i + 1] == 'X') {
                    replace_mask << '?';
                } else {
                    return {.fatal_error = true};
                }
            } else {
                replace_mask << 'X';
            }
        }
        std::string replace_mask_str = replace_mask.str();

        // find offset
        uint64_t data_offset = 0;
        uint8_t *data_offset_ptr = nullptr;
        uintptr_t data_offset_ptr_base = 0;
        if (cfg::CONFIGURATOR_STANDALONE) {

            // load file into dll map if missing
            auto it = DLL_MAP.find(dll_name);
            if (it == DLL_MAP.end()) {
                DLL_MAP[dll_name] =
                        std::unique_ptr<std::vector<uint8_t>>(
                                fileutils::bin_read(dll_path));
                it = DLL_MAP.find(dll_name);
            }

            // find pattern
            data_offset = find_pattern(*it->second, 0, pattern_bin.get(), signature_mask_str.c_str(), offset, usage);
            data_offset_ptr = reinterpret_cast<uint8_t *>(data_offset);
            data_offset_ptr_base = (uintptr_t) it->second->data();

        } else {

            // get module
            auto module = libutils::try_module(dll_path);
            bool module_free = false;
            if (!module) {
                module = libutils::try_library(dll_path);
                if (module) {
                    module_free = true;
                } else {
                    return {.fatal_error = true};
                }
            }

            // find pattern
            data_offset_ptr = reinterpret_cast<uint8_t *>(
                    find_pattern(module, pattern_bin.get(), signature_mask_str.c_str(), offset, usage));

            // convert back to offset
            data_offset = libutils::rva2offset(dll_path, (intptr_t) (data_offset_ptr - (uint8_t*) module));

            // clean
            if (module_free) {
                FreeLibrary(module);
            }
        }

        // check pointers
        if (data_offset_ptr == nullptr) {
            return {.fatal_error = true};
        }

        // get disabled/enabled data
        size_t data_len = std::max(signature_mask_str.length(), replace_mask_str.length());
        std::shared_ptr<uint8_t[]> data_disabled(new uint8_t[data_len]);
        std::shared_ptr<uint8_t[]> data_enabled(new uint8_t[data_len]);
        memutils::VProtectGuard data_guard(data_offset_ptr + data_offset_ptr_base, data_len);
        for (size_t i = 0; i < data_len; ++i) {
            if (i >= signature_mask_str.length() || signature_mask_str[i] != 'X') {
                data_disabled.get()[i] = (data_offset_ptr + data_offset_ptr_base)[i];
            } else {
                data_disabled.get()[i] = pattern_bin.get()[i];
            }
        }
        for (size_t i = 0; i < data_len; ++i) {
            if (i >= replace_mask_str.length() || replace_mask_str[i] != 'X') {
                data_enabled.get()[i] = (data_offset_ptr + data_offset_ptr_base)[i];
            } else {
                data_enabled.get()[i] = replace_data_bin.get()[i];
            }
        }

        // log edit
        log_misc("patchmanager", "found {}: {:#08X}: {} -> {}",
                 patch->name, data_offset,
                 bin2hex(data_disabled.get(), data_len),
                 bin2hex(data_enabled.get(), data_len));

        // build patch
        return MemoryPatch {
                .dll_name = dll_name,
                .data_disabled = std::move(data_disabled),
                .data_disabled_len = data_len,
                .data_enabled = std::move(data_enabled),
                .data_enabled_len = data_len,
                .data_offset = data_offset,
                .data_offset_ptr = data_offset_ptr,
        };
    }

    std::vector<uint8_t>* find_in_dll_map(
        const std::string& dll_name, size_t offset, size_t size) {

        auto dlls = DLL_MAP.find(dll_name);
        if (dlls == DLL_MAP.end()) {
            // not found; load DLL into map
            DLL_MAP[dll_name] =
                std::unique_ptr<std::vector<uint8_t>>(fileutils::bin_read(MODULE_PATH / dll_name));
        }

        // find file
        auto file = DLL_MAP[dll_name].get();

        // check bounds
        if (file->size() < offset + size) {
            return nullptr;
        }

        return file;
    }

    std::vector<uint8_t>* find_in_dll_map_org(
        const std::string& dll_name, size_t offset, size_t size) {

        auto dlls = DLL_MAP_ORG.find(dll_name);
        if (dlls == DLL_MAP_ORG.end()) {
            // not found; load DLL into map
            DLL_MAP_ORG[dll_name] =
                std::unique_ptr<std::vector<uint8_t>>(fileutils::bin_read(MODULE_PATH / dll_name));
        }

        // find file
        auto file = DLL_MAP_ORG[dll_name].get();
        if (!file) {
            log_warning("patchmanager", "could not load file into memory: {}", dll_name);
            return nullptr;
        }

        // check bounds
        if (file->size() < offset + size) {
            return nullptr;
        }

        return file;
    }

    static bool get_file_times(
        const std::filesystem::path& path,
        FILETIME* creation,
        FILETIME* access,
        FILETIME* write) {

        const auto handle = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        const bool ok = GetFileTime(handle, creation, access, write) != 0;
        CloseHandle(handle);
        return ok;
    }

    static bool set_file_times(
        const std::filesystem::path& path,
        const FILETIME* creation,
        const FILETIME* access,
        const FILETIME* write) {

        const auto handle = CreateFileW(
            path.c_str(),
            FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        const bool ok = SetFileTime(handle, creation, access, write) != 0;
        CloseHandle(handle);
        return ok;
    }

    void create_dll_backup(
        std::vector<std::filesystem::path>& written_list, const std::filesystem::path& dll_path) {

        // if dll_path is not in written_list, create a file backup.
        if (std::find(written_list.begin(), written_list.end(), dll_path) == written_list.end()) {
            written_list.push_back(dll_path);
            auto dll_bak_path = dll_path;
            dll_bak_path += ".bak";
            try {
                if (!fileutils::file_exists(dll_bak_path)) {
                    FILETIME creation {};
                    FILETIME access {};
                    FILETIME write {};
                    const bool have_times =
                        get_file_times(dll_path, &creation, &access, &write);
                    if (!have_times) {
                        log_warning(
                            "patchmanager",
                            "could not read timestamps before DLL backup for: {}",
                            dll_path);
                    }

                    std::filesystem::copy(dll_path, dll_bak_path);

                    if (have_times && !set_file_times(dll_bak_path, &creation, &access, &write)) {
                        log_warning(
                            "patchmanager",
                            "could not restore timestamps on DLL backup for: {}",
                            dll_bak_path);
                    }
                }
                log_info("patchmanager", "created DLL backup for: {}", dll_path);
            } catch (const std::filesystem::filesystem_error& e) {
                log_warning(
                    "patchmanager",
                    "filesystem error while creating DLL backup for {}, error: {}",
                    dll_path, e.what());
            }
        }
    }

    std::string fix_up_dll_name(const std::string& dll_name) {
        // IIDX omnimix dll name fix
        if (dll_name == "bm2dx.dll" && avs::game::is_model("LDJ") && avs::game::REV[0] == 'X') {
            return avs::game::DLL_NAME;
        }

        // BST 1/2 combined release dll name fix
        if (dll_name == "beatstream.dll" &&
            (avs::game::DLL_NAME == "beatstream1.dll" || avs::game::DLL_NAME == "beatstream2.dll")) {
            return avs::game::DLL_NAME;
        }

        return dll_name;
    }

    uint8_t* get_dll_offset_for_patch_apply(
        const std::string& dll_name, const uint64_t data_offset, const size_t size_in_bytes) {

        /// check if file exists
        auto dll_path = MODULE_PATH / dll_name;
        if (!fileutils::file_exists(dll_path)) {
            log_warning("patchmanager", "{} does not exist", dll_path);
            return nullptr;
        }

        // get module
        auto module = libutils::try_module(dll_path);
        if (!module) {
            log_warning("patchmanager", "cannot get module: {}", dll_path);
            return nullptr;
        }

        // convert offset to RVA
        auto offset = libutils::offset2rva(dll_path, (intptr_t)data_offset);
        if (offset == -1) {
            log_warning(
                "patchmanager", "cannot convert offset to RVA: {}, {}",
                dll_path, data_offset);
            return nullptr;
        }

        // get module information
        MODULEINFO module_info{};
        if (!GetModuleInformation(
                GetCurrentProcess(),
                module,
                &module_info,
                sizeof(MODULEINFO))) {

            log_warning(
                "patchmanager", "GetModuleInformation failed for {}, gle: {}",
                dll_path, GetLastError());

            return nullptr;
        }

        // transmute pointer
        auto dll_base = reinterpret_cast<uint8_t *>(module_info.lpBaseOfDll);
        auto dll_image_size = static_cast<uintptr_t>(module_info.SizeOfImage);

        // check bounds
        auto max_offset = static_cast<uintptr_t>(offset + size_in_bytes);
        if (max_offset >= dll_image_size) {
            log_warning(
                "patchmanager", "invalid offset bounds for {} ({})",
                dll_name, max_offset);
            return nullptr;
        }

        return &dll_base[offset];
    }

    int64_t parse_little_endian_int(uint8_t* bytes, size_t size) {
        uint64_t result = 0;
        for (size_t i = 0; i < size; i++) {
            result |= bytes[i] << i * 8;
        }
        return static_cast<int64_t>(result);
    }

    void int_to_little_endian_bytes(int64_t value, uint8_t* bytes, size_t size) {
        uint64_t v = static_cast<uint64_t>(value);
        for (size_t i = 0; i < size; i++) {
            bytes[i] = (v >> (i * 8)) & 0xff;
        }
    }

    bool restore_bytes_from_dll_map_org(
        uint8_t* destination, const std::string& dll_name, size_t offset, size_t size) {

        const auto& orig_file = find_in_dll_map_org(dll_name, offset, size);
        if (!orig_file) {
            return false;
        }

        memcpy(destination, orig_file->data() + offset, size);
        return true;
    }


    void print_auto_apply_status(PatchData &patch) {
        switch (patch.type) {
            case PatchType::Union:
                log_info(
                    "patchmanager", "auto apply: {} = {}",
                    patch.name, patch.selected_union_name);
                break;
            case PatchType::Integer:
                log_info(
                    "patchmanager", "auto apply: {} = {}",
                    patch.name, patch.patch_number.value);
                break;
            case PatchType::Memory:
            case PatchType::Signature:
            default:
                log_info("patchmanager", "auto apply: {} = ON", patch.name);
                break;
        }
    }
}
