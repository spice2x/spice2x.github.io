#include "internal.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <sstream>
#include <windows.h>
#include <winhttp.h>
#include "avs/game.h"
#include "build/resource.h"
#include "cfg/configurator.h"
#include "external/fmt/include/fmt/chrono.h"
#include "external/rapidjson/document.h"
#include "external/rapidjson/error/en.h"
#include "external/rapidjson/prettywriter.h"
#include "external/rapidjson/stringbuffer.h"
#include "launcher/launcher.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/netutils.h"
#include "util/resutils.h"
#include "util/sigscan.h"
#include "util/utils.h"

using namespace rapidjson;

namespace patcher {

    std::vector<std::string> getExtraDlls(const std::string& firstDll) {
        if (!EXTRA_DLLS.contains(firstDll)) {
            return {};
        }
        return EXTRA_DLLS[firstDll];
    }

    // utility
    std::string displayPath(const std::filesystem::path &path) {
        return fmt::format(FMT_STRING("{}"), path);
    }

    std::string getFromUrl(const std::string& dll_name, const std::string& url) {
        log_info("patchmanager", "getting patches from URL: {}, for file: {}", url, dll_name);
        std::string result;

        auto components = URL_COMPONENTS {};
        components.dwStructSize = sizeof(components);
        components.dwHostNameLength = -1;
        components.dwUrlPathLength = -1;

        auto wideUrl = std::wstring(url.begin(), url.end());
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
            const auto gle = GetLastError();
            url_fetch_errors += fmt::format("WinHttpCrackUrl failed: {}\n", gle);
            log_warning("patchmanager", "WinHttpCrackUrl failed: {}", gle);
            return result;
        }

        auto session = WinHttpOpen(L"spice2x", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
        auto session_ = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>(session, WinHttpCloseHandle);
        if (!session) {
            const auto gle = GetLastError();
            url_fetch_errors += fmt::format("WinHttpOpen failed: {}\n", gle);
            log_warning("patchmanager", "WinHttpOpen failed: {}", gle);
            return result;
        }

        auto hostname = std::wstring(components.lpszHostName, components.dwHostNameLength);
        auto connect = WinHttpConnect(session, hostname.c_str(), components.nPort, 0);
        auto connect_ = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>(connect, WinHttpCloseHandle);
        if (!connect) {
            const auto gle = GetLastError();
            url_fetch_errors += fmt::format("WinHttpConnect failed: {}\n", gle);
            log_warning("patchmanager", "WinHttpConnect failed: {}", gle);
            return result;
        }

        auto flags = 0;
        if (components.nScheme == INTERNET_SCHEME_HTTPS) {
            flags = WINHTTP_FLAG_SECURE;
        }

        auto urlPath = std::wstring(components.lpszUrlPath, components.dwUrlPathLength);
        auto request = WinHttpOpenRequest(connect, L"GET", urlPath.c_str(), nullptr, nullptr, nullptr, flags);
        auto request_ = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>(request, WinHttpCloseHandle);
        if (!request) {
            const auto gle = GetLastError();
            url_fetch_errors += fmt::format("WinHttpOpenRequest failed: {}\n", gle);
            log_warning("patchmanager", "WinHttpOpenRequest failed: {}", gle);
            return result;
        }

        if (!WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0)) {
            const auto gle = GetLastError();
            log_warning("patchmanager", "WinHttpSendRequest failed: {}", gle);
            url_fetch_errors += fmt::format("WinHttpSendRequest failed: {}\n", gle);
            if (gle == ERROR_WINHTTP_SECURE_FAILURE) {
                url_fetch_errors += "\nERROR_WINHTTP_SECURE_FAILURE\n\n";
                url_fetch_errors += "Most likely TLS 1.1 / TLS 1.2 error on old OS versions.\n\n";
                url_fetch_errors += "Look up MSDN article on 'Update to enable TLS 1.1 and TLS 1.2 as default secure protocols in WinHTTP in Windows' for a fix.\n";
            } else if (gle == ERROR_WINHTTP_NAME_NOT_RESOLVED) {
                url_fetch_errors += "\nERROR_WINHTTP_NAME_NOT_RESOLVED\n\n";
                url_fetch_errors += "The server name could not be resolved.\n\n";
                url_fetch_errors += "Website is offline, or your internet connection is down.";
            } else if (gle == ERROR_WINHTTP_TIMEOUT) {
                url_fetch_errors += "\nERROR_WINHTTP_TIMEOUT\n\n";
                url_fetch_errors += "The request timed out.";
            }
            return result;
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            const auto gle = GetLastError();
            log_warning("patchmanager", "WinHttpReceiveResponse failed: {}", gle);
            url_fetch_errors += fmt::format("WinHttpReceiveResponse failed: {}\n", gle);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        DWORD queryFlags = WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER;

        if (!WinHttpQueryHeaders(request, queryFlags, nullptr, &statusCode, &statusCodeSize, nullptr)) {
            const auto gle = GetLastError();
            log_warning("patchmanager", "WinHttpQueryHeaders failed: {}", gle);
            url_fetch_errors += fmt::format("WinHttpQueryHeaders failed: {}\n", gle);
            return result;
        }

        if (statusCode != 200) {
            log_warning("patchmanager", "Failed to fetch URL: got unexpected status code {}", statusCode);
            url_fetch_errors +=
                fmt::format(
                    "{}: HTTP Status: {} ({})\n",
                    dll_name,
                    statusCode,
                    netutils::http_status_reason_phrase(statusCode));
            if (statusCode == 404) {
                url_fetch_errors += "(Patch server did not find any patches for this game version)\n";
            }
            return result;
        }

        DWORD bytesRead = 0;
        std::vector<char> buffer(4096);
        while (WinHttpReadData(request, buffer.data(), buffer.size(), &bytesRead)) {
            if (bytesRead == 0) {
                break;
            }
            result.append(buffer.data(), bytesRead);
        }

        return result;
    }

    std::string get_game_identifier(const std::filesystem::path& dll_path, bool print_info) {
        uint32_t time_date_stamp = 0;
        uint32_t address_of_entry_point = 0;

        bool result = get_pe_identifier(dll_path, &time_date_stamp, &address_of_entry_point);

        if (!result) {
            return "";
        }

        // concatenate TimeDateStamp and AddressOfEntryPoint
        std::string identifier =
            fmt::format(
                "{}-{:x}_{:x}",
                avs::game::MODEL,
                time_date_stamp,
                address_of_entry_point);

        if (print_info) {
            const auto time = std::chrono::system_clock::from_time_t(time_date_stamp);
            log_info(
                "patchmanager",
                "file: {}, patch id: {}, build timestamp of dll: {:%Y-%m-%d %H:%M}",
                dll_path.has_filename() ? dll_path.filename() : dll_path,
                identifier,
                time);
        }

        return identifier;
    }

    void load_embedded_patches(bool apply_patches) {
        // load embedded patches from resources
        auto patches_json = resutil::load_file_string(IDR_PATCHES);

        // parse document
        Document doc;
        doc.Parse(patches_json.c_str());

        // check parse error
        auto error = doc.GetParseError();
        if (error) {
            log_warning("patchmanager", "embedded patches json file parse error: {}", static_cast<uint32_t>(error));
        }

        const auto patch_groups = parse_patch_group_definitions(doc);

        // iterate patches
        for (auto &patch : doc.GetArray()) {
            if (is_patch_group_definition(patch)) {
                continue;
            }

            // verfiy patch data
            auto name_it = patch.FindMember("name");
            if (name_it == patch.MemberEnd() || !name_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse patch name");
                continue;
            }
            auto game_code_it = patch.FindMember("gameCode");
            if (game_code_it == patch.MemberEnd() || !game_code_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse game code for {}",
                        name_it->value.GetString());
                continue;
            }
            const std::string game_code(
                game_code_it->value.GetString(),
                game_code_it->value.GetStringLength());
            auto description_it = patch.FindMember("description");
            if (description_it == patch.MemberEnd() || !description_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse description for {}",
                        name_it->value.GetString());
                continue;
            }
            auto type_it = patch.FindMember("type");
            if (type_it == patch.MemberEnd() || !type_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse type for {}",
                        name_it->value.GetString());
                continue;
            }
            auto preset_it = patch.FindMember("preset");
            bool preset = false;
            if (preset_it != patch.MemberEnd() && preset_it->value.IsBool()) {
                preset = preset_it->value.GetBool();
            }

            // build patch data
            PatchData patch_data {
                .enabled = false,
                .game_code = game_code,
                .datecode_min = 0,
                .datecode_max = 0,
                .name = name_it->value.GetString(),
                .description = description_it->value.GetString(),
                .caution = "",
                .name_in_lower_case = strtolower(name_it->value.GetString()),
                .type = PatchType::Unknown,
                .preset = preset,
                .patches_memory = std::vector<MemoryPatch>(),
                .patches_union = std::vector<UnionPatch>(),
                .patch_number = NumberPatch(),
                .group_id = resolve_patch_group_id(
                    patch,
                    patch_groups,
                    game_code,
                    name_it->value.GetString()),
                .last_status = PatchStatus::Disabled,
                .hash = "",
                .unverified = false,
                .peIdentifier = "",
                .error_reason = "",
                .selected_union_name = "",
            };

            // determine patch type
            auto type_str = type_it->value.GetString();
            if (!_stricmp(type_str, "memory")) {
                patch_data.type = PatchType::Memory;
            } else if (!_stricmp(type_str, "signature")) {
                patch_data.type = PatchType::Signature;
            }

            // determine date code
            auto date_code_it = patch.FindMember("dateCode");
            if (date_code_it != patch.MemberEnd() && date_code_it->value.IsInt()) {
                patch_data.datecode_min = date_code_it->value.GetInt();
                patch_data.datecode_max = patch_data.datecode_min;
            } else {
                auto date_code_min_it = patch.FindMember("dateCodeMin");
                if (date_code_min_it == patch.MemberEnd() || !date_code_min_it->value.IsInt()) {
                    log_warning("patchmanager", "unable to parse datecode for {}",
                            name_it->value.GetString());
                    continue;
                }
                auto date_code_max_it = patch.FindMember("dateCodeMax");
                if (date_code_max_it == patch.MemberEnd() || !date_code_max_it->value.IsInt()) {
                    log_warning("patchmanager", "unable to parse datecode for {}",
                            name_it->value.GetString());
                    continue;
                }
                patch_data.datecode_min = date_code_min_it->value.GetInt();
                patch_data.datecode_max = date_code_max_it->value.GetInt();
            }

            // check for skip
            if (!avs::game::is_model(patch_data.game_code.c_str())) {
                continue;
            }
            if (!avs::game::is_ext(patch_data.datecode_min, patch_data.datecode_max)) {
                continue;
            }

            // generate hash
            patch_data.hash = patch_hash(patch_data);

            // check for existing
            bool existing = false;
            for (auto &added_patch : patches) {
                if (added_patch.hash == patch_data.hash) {
                    existing = true;
                    break;
                }
            }
            if (existing) {
                continue;
            }

            // hash check for enabled
            for (auto &enabled_entry : setting_patches_enabled) {
                if (patch_data.hash == enabled_entry) {
                    patch_data.enabled = true;
                    break;
                }
            }

            // check patch type
            switch (patch_data.type) {
                case PatchType::Memory: {

                    // iterate memory patches
                    auto patches_it = patch.FindMember("patches");
                    if (patches_it == patch.MemberEnd()
                    || !patches_it->value.IsArray()) {
                        log_warning("patchmanager", "unable to get patches for {}",
                                name_it->value.GetString());
                        continue;
                    }
                    for (auto &memory_patch : patches_it->value.GetArray()) {

                        // validate data
                        auto data_disabled_it = memory_patch.FindMember("dataDisabled");
                        if (data_disabled_it == memory_patch.MemberEnd()
                        || !data_disabled_it->value.IsString()) {
                            log_warning("patchmanager", "unable to get data for {}",
                                    name_it->value.GetString());
                            continue;
                        }
                        auto data_enabled_it = memory_patch.FindMember("dataEnabled");
                        if (data_enabled_it == memory_patch.MemberEnd()
                        || !data_enabled_it->value.IsString()) {
                            log_warning("patchmanager", "unable to get data for {}",
                                    name_it->value.GetString());
                            continue;
                        }

                        // get hex strings
                        auto data_disabled_hex = data_disabled_it->value.GetString();
                        auto data_enabled_hex = data_enabled_it->value.GetString();
                        auto data_disabled_hex_len = strlen(data_disabled_hex);
                        auto data_enabled_hex_len = strlen(data_enabled_hex);
                        if ((data_disabled_hex_len % 2) != 0 || (data_enabled_hex_len % 2) != 0) {
                            log_warning("patchmanager", "patch hex data length has odd length for {}",
                                    name_it->value.GetString());
                            continue;
                        }

                        // convert to binary
                        std::shared_ptr<uint8_t[]> data_disabled(new uint8_t[data_disabled_hex_len / 2]);
                        std::shared_ptr<uint8_t[]> data_enabled(new uint8_t[data_enabled_hex_len / 2]);
                        if (!hex2bin(data_disabled_hex, data_disabled.get())
                        || (!hex2bin(data_enabled_hex, data_enabled.get()))) {
                            log_warning("patchmanager", "failed to parse patch data from hex for {}",
                                    name_it->value.GetString());
                            continue;
                        }

                        // get DLL name
                        auto dll_name_it = memory_patch.FindMember("dllName");
                        if (dll_name_it == memory_patch.MemberEnd()
                        || !dll_name_it->value.IsString()) {
                            log_warning("patchmanager", "unable to get dllName for {}",
                                    name_it->value.GetString());
                            continue;
                        }
                        std::string dll_name = dll_name_it->value.GetString();

                        // IIDX omnimix dll name fix
                        if (dll_name == "bm2dx.dll" && avs::game::is_model("LDJ") && avs::game::REV[0] == 'X') {
                            dll_name = avs::game::DLL_NAME;
                        }

                        // BST 1/2 combined release dll name fix
                        if (dll_name == "beatstream.dll" &&
                             (avs::game::DLL_NAME == "beatstream1.dll"
                           || avs::game::DLL_NAME == "beatstream2.dll"))
                        {
                            dll_name = avs::game::DLL_NAME;
                        }

                        // build memory patch data
                        MemoryPatch memory_patch_data {
                                .dll_name = dll_name,
                                .data_disabled = std::move(data_disabled),
                                .data_disabled_len = data_disabled_hex_len / 2,
                                .data_enabled = std::move(data_enabled),
                                .data_enabled_len = data_enabled_hex_len / 2,
                                .data_offset = 0,
                        };

                        // get data offset
                        auto data_offset_it = memory_patch.FindMember("dataOffset");
                        if (data_offset_it == memory_patch.MemberEnd()) {
                            log_warning("patchmanager", "unable to get dataOffset for {}",
                                        name_it->value.GetString());
                            continue;
                        }
                        if (data_offset_it->value.IsUint64()) {
                            memory_patch_data.data_offset = data_offset_it->value.GetUint64();
                        } else if (data_offset_it->value.IsString()) {
                            std::stringstream ss;
                            ss << data_offset_it->value.GetString();
                            ss >> memory_patch_data.data_offset;
                            if (!ss.good() || !ss.eof()) {
                                log_warning("patchmanager", "invalid dataOffset for {}",
                                            name_it->value.GetString());
                                continue;
                            }
                        } else {
                            log_warning("patchmanager", "unable to get dataOffset for {}",
                                        name_it->value.GetString());
                            continue;
                        }

                        // move to list
                        patch_data.patches_memory.emplace_back(memory_patch_data);
                    }
                    break;
                }
                case PatchType::Signature: {

                    // validate data
                    auto data_signature_it = patch.FindMember("signature");
                    if (data_signature_it == patch.MemberEnd()
                        || !data_signature_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get data for {}",
                                    name_it->value.GetString());
                        continue;
                    }
                    auto data_replacement_it = patch.FindMember("replacement");
                    if (data_replacement_it == patch.MemberEnd()
                        || !data_replacement_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get data for {}",
                                    name_it->value.GetString());
                        continue;
                    }

                    // get DLL name
                    auto dll_name_it = patch.FindMember("dllName");
                    if (dll_name_it == patch.MemberEnd()
                        || !dll_name_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get dllName for {}",
                                    name_it->value.GetString());
                        continue;
                    }
                    std::string dll_name = dll_name_it->value.GetString();

                    // IIDX omnimix dll name fix
                    if (dll_name == "bm2dx.dll" && avs::game::is_model("LDJ") && avs::game::REV[0] == 'X') {
                        dll_name = avs::game::DLL_NAME;
                    }

                    // BST 1/2 combined release dll name fix
                    if (dll_name == "beatstream.dll" &&
                        (avs::game::DLL_NAME == "beatstream1.dll"
                       || avs::game::DLL_NAME == "beatstream2.dll"))
                    {
                        dll_name = avs::game::DLL_NAME;
                    }

                    // get optional offset
                    uint64_t offset = 0;
                    auto offset_it = patch.FindMember("offset");
                    if (offset_it != patch.MemberEnd()) {
                        bool invalid = false;
                        if (offset_it->value.IsInt64()) {
                            offset = offset_it->value.GetInt64();
                        } else if (offset_it->value.IsString()) {
                            std::stringstream ss;
                            ss << offset_it->value.GetString();
                            ss >> offset;
                            invalid = !ss.good() || !ss.eof();
                        } else {
                            invalid = true;
                        }
                        if (invalid) {
                            log_warning("patchmanager", "invalid offset for {}",
                                        name_it->value.GetString());
                        }
                    }

                    // get optional usage
                    int usage = 0;
                    auto usage_it = patch.FindMember("usage");
                    if (usage_it != patch.MemberEnd()) {
                        bool invalid = false;
                        if (usage_it->value.IsInt64()) {
                            usage = usage_it->value.GetInt64();
                        } else if (usage_it->value.IsString()) {
                            std::stringstream ss;
                            ss << usage_it->value.GetString();
                            ss >> usage;
                            invalid = !ss.good() || !ss.eof();
                        } else {
                            invalid = true;
                        }
                        if (invalid) {
                            log_warning("patchmanager", "invalid usage for {}",
                                        name_it->value.GetString());
                        }
                    }

                    // build signature patch
                    SignaturePatch signature_data = {
                            .dll_name = dll_name,
                            .signature = data_signature_it->value.GetString(),
                            .replacement = data_replacement_it->value.GetString(),
                            .offset = offset,
                            .usage = usage,
                    };

                    // convert to memory patch
                    patch_data.patches_memory.emplace_back(signature_data.to_memory(&patch_data));
                    patch_data.type = PatchType::Memory;
                    break;
                }
                case PatchType::Unknown:
                default:
                    log_warning("patchmanager", "unknown patch type: {}", static_cast<uint32_t>(patch_data.type));
                    break;
            }

            // auto apply
            register_patch_group(patch_data, patch_groups);
            if (apply_patches && setting_auto_apply && patch_data.enabled) {
                print_auto_apply_status(patch_data);
                apply_patch(patch_data, true);
            }

            // remember patch
            patches.emplace_back(patch_data);
        }
    }

    bool import_remote_patches_for_dll(const std::string& url, const std::string& dll_name) {
        log_info("patchmanager", "loading remote patches for {}...", dll_name);
        std::string identifier = get_game_identifier(MODULE_PATH / dll_name);
        std::string url_cpy = url;
        if (url_cpy.back() != '/')
            url_cpy += '/';
        std::string json_path = fmt::format("{}{}.json", url_cpy, identifier);
        try {
            auto patches_json = getFromUrl(dll_name, json_path);
            if (!patches_json.empty()) {

                // see if this is valid JSON
                Document doc_temp;
                doc_temp.Parse(patches_json.c_str());
                const auto error = doc_temp.GetParseError();
                if (error) {
                    log_warning(
                        "patchmanager",
                        "remotely fetched JSON file parse error: {}",
                        rapidjson::GetParseError_En(error));
                    url_fetch_errors += fmt::format(
                            "Invalid JSON received from remote URL.\n"
                            "Your DLL version might not be supported.\n"
                            "URL: {}\n"
                            "JSON Parse Error: {}\n",
                            json_path,
                            rapidjson::GetParseError_En(error));
                    return false;
                } else {
                    log_info("patchmanager", "remotely fetched JSON was successfully parsed");
                }

                // create patches dir
                if (!fileutils::dir_exists(LOCAL_PATCHES_PATH)) {
                    fileutils::dir_create(LOCAL_PATCHES_PATH);
                }
                // save to file
                std::filesystem::path save_path = LOCAL_PATCHES_PATH / (identifier + ".json");
                fileutils::text_write(save_path, patches_json);
                log_info("patchmanager", "remotely fetched JSON saved to: {}", save_path);
                return true;
            } else {
                log_warning("patchmanager", "failed to fetch patches JSON for {}", dll_name);
            }
        } catch (const std::exception& e) {
            log_warning("patchmanager", "exception occurred while loading remote patches JSON for {}: {}", dll_name, e.what());
            url_fetch_errors += fmt::format(
                "Exception while loading remote patches for {}: {}\n", dll_name, e.what());
        }
        return false;
    }

    bool load_from_patches_json(bool apply_patches) {
        bool ret = false;

        // list valid PE identifiers from our local files
        auto modules = std::vector<std::string>();
        modules.push_back(get_game_identifier(MODULE_PATH / avs::game::DLL_NAME));
        for (const std::string& dll : getExtraDlls(avs::game::DLL_NAME)) {
            modules.push_back(get_game_identifier(MODULE_PATH / dll));
        }

        auto filter = [&modules](const PatchData& patch_data) {
            // match on peIdentifier if provided
            if (!patch_data.peIdentifier.empty()) {
                return std::ranges::find(modules, patch_data.peIdentifier) != modules.end();
            }

            // game code is already checked by append_patches, so no need to check here
            // check the datecode / datecode range, if it exists
            if (patch_data.datecode_min != 0 || patch_data.datecode_max != 0) {
                return avs::game::is_ext(patch_data.datecode_min, patch_data.datecode_max);
            }

            // otherwise, don't load them in
            return false;
        };

        // possible locations of patches.json
        // note: MODULE_PATH changes at launch, so it must be checked fresh here
        //       (as opposed to this being checked once at launch)
        const std::filesystem::path LOCAL_PATCHES_JSON_PATHS[] = {
            "patches/patches.json",                      // new in spice2x
            "patches.json",                              // spicetools
            MODULE_PATH / "patches.json",                // spicetools
            std::filesystem::path("..") / "patches.json" // spicetools
        };

        const size_t patches_size_previous = patches.size();
        for (const std::filesystem::path& patches_json_path: LOCAL_PATCHES_JSON_PATHS) {
            if (!fileutils::file_exists(patches_json_path)) {
                log_misc("patchmanager", "file does not exist, skipping: {}", patches_json_path);
                continue;
            }

            log_misc("patchmanager", "reading from patches.json: {}", patches_json_path);
            std::string content = fileutils::text_read(patches_json_path);
            append_patches(content, apply_patches, filter);

            const auto new_patches = patches.size() - patches_size_previous;
            log_info("patchmanager", "loaded {} patches from: {}", new_patches, patches_json_path);
            if (0 < new_patches) {
                ret = true;
                ACTIVE_JSON_FILE = displayPath(patches_json_path);
                break;
            }
        }
        return ret;
    }

    void reload_local_patches(bool apply_patches) {
        // announce reload
        if (apply_patches) {
            log_info("patchmanager", "reloading (local) and applying patches");
        } else {
            log_info("patchmanager", "reloading (local) patches");
        }

        // clear old patches
        patches.clear();
        patch_groups.clear();
        // drop the cached sorted view so the table rebuilds it (in default order)
        // on the next frame
        patches_sorted.clear();
        if (cfg::CONFIGURATOR_STANDALONE) {
            clear_dll_maps();
        }

        ACTIVE_JSON_FILE = "";

        std::string firstDll = avs::game::DLL_NAME;
        std::string first_id = get_game_identifier(MODULE_PATH / firstDll, true);
        std::filesystem::path firstPath = fmt::format("patches/{}.json", first_id);

        auto extraDlls = getExtraDlls(firstDll);
        std::erase_if(extraDlls, [](const std::string& dll) {
            auto identifier = get_game_identifier(MODULE_PATH / dll, true);
            return identifier.empty() || !fileutils::file_exists(fmt::format("patches/{}.json", identifier));
        });

        if (fileutils::file_exists(firstPath) || !extraDlls.empty()) {
            if (fileutils::file_exists(firstPath)) {
                log_info("patchmanager", "loaded patches for {} from {}", firstDll, firstPath);
                std::string content = fileutils::text_read(firstPath);
                append_patches(content, apply_patches, nullptr, first_id);
                ACTIVE_JSON_FILE = displayPath(firstPath);
            }
            for (const std::string& dll : extraDlls) {
                auto extraId = get_game_identifier(MODULE_PATH / dll);
                auto extraPath = std::filesystem::path(fmt::format("patches/{}.json", extraId));
                log_info("patchmanager", "loaded patches for {} from {}", dll, extraPath);
                std::string content = fileutils::text_read(extraPath);
                append_patches(content, apply_patches, nullptr, extraId);
                if (ACTIVE_JSON_FILE.empty()) {
                    ACTIVE_JSON_FILE = displayPath(extraPath);
                } else {
                    ACTIVE_JSON_FILE += ", " + displayPath(extraPath);
                }
            }
        } else {
            load_from_patches_json(apply_patches);
        }

        if (patches.empty()) {
            // load embedded patches from resources
            load_embedded_patches(apply_patches);
        }

        // show amount of patches
        log_info("patchmanager", "loaded total of {} patches", patches.size());
        local_patches_initialized = true;
    }

    bool import_remote_patches_to_disk() {
        bool imported = false;
        // clear old patches
        patches.clear();
        patch_groups.clear();
        patches_sorted.clear();
        url_fetch_errors.clear();

        // load patches for main dll
        imported = import_remote_patches_for_dll(patch_url, avs::game::DLL_NAME);

        // check for additional patches based on module name
        for (const std::string& dll : getExtraDlls(avs::game::DLL_NAME)) {
            imported |= import_remote_patches_for_dll(patch_url, dll);
        }

        return imported;
    }

    void append_patches(
        std::string &patches_json,
        bool apply_patches,
        std::function<bool(const PatchData&)> filter,
        std::string pe_identifier_for_patch) {

        // parse document
        Document doc;
        doc.Parse(patches_json.c_str());

        // check parse error
        const auto error = doc.GetParseError();
        const auto error_offset = doc.GetErrorOffset();
        if (error) {
            log_warning(
                "patchmanager",
                "patches file parse error at offset {}: {} ({})",
                error_offset,
                static_cast<uint32_t>(error),
                rapidjson::GetParseError_En(error));
        }

        const auto patch_groups = parse_patch_group_definitions(doc);

        // iterate patches
        for (auto &patch : doc.GetArray()) {
            if (is_patch_group_definition(patch)) {
                continue;
            }

            // verfiy patch data
            auto name_it = patch.FindMember("name");
            if (name_it == patch.MemberEnd() || !name_it->value.IsString()) {
                if (patch == doc.GetArray()[0]) {
                    // first one is special - it may be header info, print it out to console
                    rapidjson::StringBuffer buffer;
                    PrettyWriter<StringBuffer> writer(buffer);
                    patch.Accept(writer);
                    log_info("patchmanager", "patches file info: \n{}", buffer.GetString());
                } else {
                    log_warning("patchmanager", "failed to parse patch name");
                }
                continue;
            }
            auto game_code_it = patch.FindMember("gameCode");
            if (game_code_it == patch.MemberEnd() || !game_code_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse game code for {}",
                    name_it->value.GetString());
                continue;
            }
            const std::string game_code(
                game_code_it->value.GetString(),
                game_code_it->value.GetStringLength());
            auto description_it = patch.FindMember("description");
            if (description_it == patch.MemberEnd() || !description_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse description for {}",
                    name_it->value.GetString());
                continue;
            }
            auto caution_it = patch.FindMember("caution");
            const char* caution = "";
            if (caution_it != patch.MemberEnd() && caution_it->value.IsString()) {
                caution = caution_it->value.GetString();
            }
            auto type_it = patch.FindMember("type");
            if (type_it == patch.MemberEnd() || !type_it->value.IsString()) {
                log_warning("patchmanager", "failed to parse type for {}",
                    name_it->value.GetString());
                continue;
            }
            auto pe_identifier_it = patch.FindMember("peIdentifier");
            const char* pe_identifier = "";
            if (pe_identifier_it != patch.MemberEnd() && pe_identifier_it->value.IsString()) {
                pe_identifier = pe_identifier_it->value.GetString();
            }
            auto preset_it = patch.FindMember("preset");
            bool preset = false;
            if (preset_it != patch.MemberEnd() && preset_it->value.IsBool()) {
                preset = preset_it->value.GetBool();
            }

            // build patch data
            PatchData patch_data {
                .enabled = false,
                .game_code = game_code,
                .datecode_min = 0,
                .datecode_max = 0,
                .name = name_it->value.GetString(),
                .description = description_it->value.GetString(),
                .caution = std::string(caution),
                .name_in_lower_case = strtolower(name_it->value.GetString()),
                .type = PatchType::Unknown,
                .preset = preset,
                .patches_memory = std::vector<MemoryPatch>(),
                .patches_union = std::vector<UnionPatch>(),
                .patch_number = NumberPatch(),
                .group_id = resolve_patch_group_id(
                    patch,
                    patch_groups,
                    game_code,
                    name_it->value.GetString()),
                .last_status = PatchStatus::Disabled,
                .hash = "",
                .unverified = false,
                .peIdentifier = std::string(pe_identifier),
                .error_reason = "",
                .selected_union_name = "",
            };

            // determine date code
            const auto date_code_it = patch.FindMember("dateCode");
            if (date_code_it != patch.MemberEnd() && date_code_it->value.IsInt()) {
                patch_data.datecode_min = date_code_it->value.GetInt();
                patch_data.datecode_max = patch_data.datecode_min;
            } else {
                const auto date_code_min_it = patch.FindMember("dateCodeMin");
                const auto date_code_max_it = patch.FindMember("dateCodeMax");
                if (date_code_min_it != patch.MemberEnd() && date_code_min_it->value.IsInt() &&
                    date_code_max_it != patch.MemberEnd() && date_code_max_it->value.IsInt()) {
                    patch_data.datecode_min = date_code_min_it->value.GetInt();
                    patch_data.datecode_max = date_code_max_it->value.GetInt();
                }
            }

            // override pe identifier if it wasn't present in JSON (possible for remote patches)
            if (patch_data.peIdentifier.empty()) {
                patch_data.peIdentifier = pe_identifier_for_patch;
            }

            // if the caller provided a filter, check if this patch should be ignored.
            if (filter && !filter(patch_data)) {
                continue;
            }

            // determine patch type
            const auto type_str = type_it->value.GetString();
            if (!_stricmp(type_str, "memory")) {
                patch_data.type = PatchType::Memory;
            } else if (!_stricmp(type_str, "signature")) {
                patch_data.type = PatchType::Signature;
            } else if (!stricmp(type_str, "union")) {
                patch_data.type = PatchType::Union;
            } else if (!stricmp(type_str, "number")) {
                patch_data.type = PatchType::Integer;
            }

            // check for skip
            if (!avs::game::is_model(patch_data.game_code.c_str())) {
                continue;
            }

            // generate hash
            patch_data.hash = patch_hash(patch_data);

            // check for existing
            bool existing = false;
            for (auto &added_patch : patches) {
                if (added_patch.hash == patch_data.hash) {
                    existing = true;
                    break;
                }
            }
            if (existing) {
                continue;
            }

            // hash check for enabled
            if (patch_data.type == PatchType::Union) {
                if (setting_union_patches_enabled.contains(patch_data.hash)) {
                    patch_data.enabled = true;
                }
            } else if (patch_data.type == PatchType::Integer) {
                if (setting_int_patches_enabled.contains(patch_data.hash)) {
                    patch_data.enabled = true;
                }
            } else {
                for (auto &enabled_entry : setting_patches_enabled) {
                    if (patch_data.hash == enabled_entry) {
                        patch_data.enabled = true;
                        break;
                    }
                }
            }

            // check patch type
            switch (patch_data.type) {
            case PatchType::Memory: {

                // iterate memory patches
                auto patches_it = patch.FindMember("patches");
                if (patches_it == patch.MemberEnd()
                    || !patches_it->value.IsArray()) {
                    log_warning("patchmanager", "unable to get patches for {}",
                        name_it->value.GetString());
                    continue;
                }
                for (auto& memory_patch : patches_it->value.GetArray()) {

                    // validate data
                    auto data_disabled_it = memory_patch.FindMember("dataDisabled");
                    if (data_disabled_it == memory_patch.MemberEnd()
                        || !data_disabled_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get data for {}",
                            name_it->value.GetString());
                        continue;
                    }
                    auto data_enabled_it = memory_patch.FindMember("dataEnabled");
                    if (data_enabled_it == memory_patch.MemberEnd()
                        || !data_enabled_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get data for {}",
                            name_it->value.GetString());
                        continue;
                    }

                    // get hex strings
                    auto data_disabled_hex = data_disabled_it->value.GetString();
                    auto data_enabled_hex = data_enabled_it->value.GetString();
                    auto data_disabled_hex_len = strlen(data_disabled_hex);
                    auto data_enabled_hex_len = strlen(data_enabled_hex);
                    if ((data_disabled_hex_len % 2) != 0 || (data_enabled_hex_len % 2) != 0) {
                        log_warning("patchmanager", "patch hex data length has odd length for {}",
                            name_it->value.GetString());
                        continue;
                    }

                    // convert to binary
                    std::shared_ptr<uint8_t[]> data_disabled(new uint8_t[data_disabled_hex_len / 2]);
                    std::shared_ptr<uint8_t[]> data_enabled(new uint8_t[data_enabled_hex_len / 2]);
                    if (!hex2bin(data_disabled_hex, data_disabled.get())
                        || (!hex2bin(data_enabled_hex, data_enabled.get()))) {
                        log_warning("patchmanager", "failed to parse patch data from hex for {}",
                            name_it->value.GetString());
                        continue;
                    }

                    // get DLL name
                    auto dll_name_it = memory_patch.FindMember("dllName");
                    if (dll_name_it == memory_patch.MemberEnd()
                        || !dll_name_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get dllName for {}",
                            name_it->value.GetString());
                        continue;
                    }
                    const std::string dll_name = fix_up_dll_name(dll_name_it->value.GetString());

                    // build memory patch data
                    MemoryPatch memory_patch_data {
                        .dll_name = dll_name,
                        .data_disabled = std::move(data_disabled),
                        .data_disabled_len = data_disabled_hex_len / 2,
                        .data_enabled = std::move(data_enabled),
                        .data_enabled_len = data_enabled_hex_len / 2,
                        .data_offset = 0,
                    };

                    // get data offset
                    memory_patch_data.data_offset =
                        parse_json_data_offset(patch_data.name, memory_patch);
                    if (memory_patch_data.data_offset == 0) {
                        continue;
                    }

                    // move to list
                    patch_data.patches_memory.emplace_back(memory_patch_data);
                }
                break;
            }
            case PatchType::Signature: {

                // validate data
                auto data_signature_it = patch.FindMember("signature");
                if (data_signature_it == patch.MemberEnd()
                    || !data_signature_it->value.IsString()) {
                    log_warning("patchmanager", "unable to get data for {}",
                        name_it->value.GetString());
                    continue;
                }
                auto data_replacement_it = patch.FindMember("replacement");
                if (data_replacement_it == patch.MemberEnd()
                    || !data_replacement_it->value.IsString()) {
                    log_warning("patchmanager", "unable to get data for {}",
                        name_it->value.GetString());
                    continue;
                }

                // get DLL name
                auto dll_name_it = patch.FindMember("dllName");
                if (dll_name_it == patch.MemberEnd()
                    || !dll_name_it->value.IsString()) {
                    log_warning("patchmanager", "unable to get dllName for {}",
                        name_it->value.GetString());
                    continue;
                }
                const std::string dll_name = fix_up_dll_name(dll_name_it->value.GetString());

                // get optional offset
                uint64_t offset = parse_json_data_offset(patch_data.name, patch);

                // get optional usage
                int usage = 0;
                auto usage_it = patch.FindMember("usage");
                if (usage_it != patch.MemberEnd()) {
                    bool invalid = false;
                    if (usage_it->value.IsInt64()) {
                        usage = usage_it->value.GetInt64();
                    } else if (usage_it->value.IsString()) {
                        std::stringstream ss;
                        ss << usage_it->value.GetString();
                        ss >> usage;
                        invalid = !ss.good() || !ss.eof();
                    } else {
                        invalid = true;
                    }
                    if (invalid) {
                        log_warning("patchmanager", "invalid usage for {}",
                            name_it->value.GetString());
                    }
                }

                // build signature patch
                SignaturePatch signature_data = {
                    .dll_name = dll_name,
                    .signature = data_signature_it->value.GetString(),
                    .replacement = data_replacement_it->value.GetString(),
                    .offset = offset,
                    .usage = usage,
                };

                // convert to memory patch
                patch_data.patches_memory.emplace_back(signature_data.to_memory(&patch_data));
                patch_data.type = PatchType::Memory;
                break;
            }
            case PatchType::Union: {
                // iterate union patches
                auto patches_it = patch.FindMember("patches");
                if (patches_it == patch.MemberEnd()
                    || !patches_it->value.IsArray()) {
                    log_warning("patchmanager", "unable to get patches for {}",
                        name_it->value.GetString());
                    continue;
                }

                std::string union_dll_name_for_patch("");
                uint64_t union_offset_for_patch = 0;
                uint64_t union_hex_len_for_patch = 0;

                for (auto& union_patch : patches_it->value.GetArray()) {

                    // validate data
                    auto union_name_it = union_patch.FindMember("name");
                    if (union_name_it == union_patch.MemberEnd()
                        || !union_name_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get name for {}",
                            name_it->value.GetString());
                        continue;
                    }
                    auto union_patch_it = union_patch.FindMember("patch");
                    if (union_patch_it == union_patch.MemberEnd()
                        || !union_patch_it->value.IsObject()) {
                        log_warning("patchmanager", "unable to get patch for {}",
                            name_it->value.GetString());
                        continue;
                    }

                    // get patch data
                    auto union_dll_name_it = union_patch_it->value.FindMember("dllName");
                    if (union_dll_name_it == union_patch_it->value.MemberEnd()
                        || !union_dll_name_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get dllName for {}",
                            name_it->value.GetString());
                        continue;
                    }
                    auto union_data_it = union_patch_it->value.FindMember("data");
                    if (union_data_it == union_patch_it->value.MemberEnd()
                        || !union_data_it->value.IsString()) {
                        log_warning("patchmanager", "unable to get data for {}",
                            name_it->value.GetString());
                        continue;
                    }

                    // get hex string
                    auto union_data_hex = union_data_it->value.GetString();
                    auto union_data_hex_len = strlen(union_data_hex);
                    if ((union_data_hex_len % 2) != 0) {
                        log_warning("patchmanager", "patch hex data length has odd length for {}",
                            name_it->value.GetString());
                        continue;
                    }
                    // convert to binary
                    std::shared_ptr<uint8_t[]> union_data(new uint8_t[union_data_hex_len / 2]);
                    if (!hex2bin(union_data_hex, union_data.get())) {
                        log_warning("patchmanager", "failed to parse patch data from hex for {}",
                            name_it->value.GetString());
                        continue;
                    }

                    // get DLL name
                    const std::string union_dll_name =
                        fix_up_dll_name(union_dll_name_it->value.GetString());
                    if (union_dll_name_for_patch.empty()) {
                        union_dll_name_for_patch = union_dll_name;
                    } else if (union_dll_name != union_dll_name_for_patch) {
                        log_warning(
                            "patchmanager", "inconsistent DLL name for union patch '{}'::'{}', ignoring",
                            name_it->value.GetString(),
                            union_name_it->value.GetString());
                        continue;
                    }

                    // get offset
                    uint64_t union_offset =
                        parse_json_data_offset(patch_data.name, union_patch_it->value);
                    if (union_offset == 0) {
                        continue;
                    }

                    // validate that offset and size are the same for all patches for this union
                    if (union_offset_for_patch == 0) {
                        union_offset_for_patch = union_offset;
                    } else if (union_offset_for_patch != union_offset) {
                        log_warning(
                            "patchmanager", "inconsistent offset detected for union patch '{}'::'{}', ignoring",
                            name_it->value.GetString(),
                            union_name_it->value.GetString());
                        continue;
                    }
                    if (union_hex_len_for_patch == 0) {
                        union_hex_len_for_patch = union_data_hex_len;
                    } else if (union_hex_len_for_patch != union_data_hex_len) {
                        log_warning(
                            "patchmanager", "inconsistent length detected for union patch '{}'::'{}', ignoring",
                            name_it->value.GetString(),
                            union_name_it->value.GetString());
                        continue;
                    }

                    // build union patch
                    UnionPatch union_patch_data{
                        .name = union_name_it->value.GetString(),
                        .dll_name = union_dll_name,
                        .data = std::move(union_data),
                        .data_len = union_data_hex_len / 2,
                        .offset = union_offset,
                    };

                    // move to list
                    patch_data.patches_union.emplace_back(union_patch_data);
                }

                if (setting_union_patches_enabled.contains(patch_data.hash)) {
                    patch_data.selected_union_name = setting_union_patches_enabled[patch_data.hash];
                }
                break;
            }
            case PatchType::Integer: {
                auto& numpatch = patch_data.patch_number;

                auto num_patch_it = patch.FindMember("patch");
                if (num_patch_it == patch.MemberEnd() || !num_patch_it->value.IsObject()) {
                    log_warning("patchmanager", "unable to get patch for {}",
                        name_it->value.GetString());
                    continue;
                }

                // validate data
                auto min_it = num_patch_it->value.FindMember("min");
                if (min_it == num_patch_it->value.MemberEnd() || !min_it->value.IsNumber()) {
                    log_warning(
                        "patchmanager", "unable to get data for min - {}",
                        min_it->value.GetString());
                    continue;
                }
                auto max_it = num_patch_it->value.FindMember("max");
                if (max_it == num_patch_it->value.MemberEnd() || !max_it->value.IsNumber()) {
                    log_warning(
                        "patchmanager", "unable to get data for max - {}",
                        max_it->value.GetString());
                    continue;
                }
                auto size_it = num_patch_it->value.FindMember("size");
                if (size_it == num_patch_it->value.MemberEnd() || !size_it->value.IsNumber()) {
                    log_warning(
                        "patchmanager", "unable to get data for size - {}",
                        size_it->value.GetString());
                    continue;
                }

                // get values
                auto min = min_it->value.GetInt();
                auto max = max_it->value.GetInt();
                auto size = size_it->value.GetUint();
                if (min >= max) {
                    log_warning(
                        "patchmanager", "invalid min/max range provided for patch: {}",
                        name_it->value.GetString());
                    continue;
                }
                if (size != 1 && size != 2 && size != 4 && size != 8) {
                    log_warning(
                        "patchmanager", "invalid size provided for patch: {}",
                        name_it->value.GetString());
                    continue;
                }

                // get DLL name
                auto dll_name_it = num_patch_it->value.FindMember("dllName");
                if (dll_name_it == num_patch_it->value.MemberEnd() || !dll_name_it->value.IsString()) {
                    log_warning(
                        "patchmanager", "unable to get dllName for {}",
                        name_it->value.GetString());
                    continue;
                }

                // build number patch data
                numpatch.dll_name = fix_up_dll_name(dll_name_it->value.GetString());
                numpatch.min = min;
                numpatch.max = max;
                numpatch.size_in_bytes = size;

                // get data offset
                numpatch.data_offset =
                    parse_json_data_offset(patch_data.name, num_patch_it->value);
                if (numpatch.data_offset == 0) {
                    continue;
                }

                // load value from previously saved patch setting
                if (setting_int_patches_enabled.contains(patch_data.hash)) {
                    numpatch.value = CLAMP(
                            setting_int_patches_enabled[patch_data.hash],
                            numpatch.min,
                            numpatch.max);
                }

                break;
            }
            case PatchType::Unknown:
            default:
                log_warning("patchmanager", "unknown patch type: {}", static_cast<uint32_t>(patch_data.type));
                break;
            }

            // auto apply
            register_patch_group(patch_data, patch_groups);
            if (apply_patches && setting_auto_apply && patch_data.enabled) {
                print_auto_apply_status(patch_data);
                apply_patch(patch_data, true);
            }

            // remember patch
            patches.emplace_back(patch_data);
        }
    }


    uint64_t parse_json_data_offset(
        const std::string &patch_name, const rapidjson::Value &value) {

        // parse "offset" / "dataOffset"
        auto data_offset_it = value.FindMember("offset");
        if (data_offset_it == value.MemberEnd()) {
            data_offset_it = value.FindMember("dataOffset");
            if (data_offset_it == value.MemberEnd()) {
                log_warning("patchmanager", "unable to get offset / dataOffset for {}", patch_name);
                return 0;
            }
        }

        if (data_offset_it->value.IsUint64()) {
            // parse as unsigned integer
            return data_offset_it->value.GetUint64();

        } else if (data_offset_it->value.IsString()) {
            // parse as string and convert to integer
            uint64_t offset;
            std::stringstream ss;
            ss << data_offset_it->value.GetString();
            ss >> offset;
            if (!ss.good() || !ss.eof()) {
                log_warning("patchmanager", "invalid offset for {}", patch_name);
                return 0;
            }
            return offset;
        } else {
            log_warning("patchmanager", "unable to get offset for {}", patch_name);
            return 0;
        }
    }

}
