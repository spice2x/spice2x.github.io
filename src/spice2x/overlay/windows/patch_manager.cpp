#include "patch_manager.h"

#include <thread>
#include <fstream>
#include <shellapi.h>
#include <winhttp.h>
#include <psapi.h>
#include <format>
#include "external/rapidjson/document.h"
#include "external/rapidjson/prettywriter.h"
#include "external/rapidjson/stringbuffer.h"
#include "external/rapidjson/error/en.h"
#include "external/hash-library/sha256.h"
#include "external/robin_hood.h"
#include "cfg/configurator.h"
#include "util/memutils.h"
#include "games/io.h"
#include "build/resource.h"
#include "util/sigscan.h"
#include "util/resutils.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/netutils.h"
#include "util/nt_loader.h"
#include "overlay/imgui/extensions.h"
#include "avs/game.h"
#include "misc/clipboard.h"

// std::min
#ifdef min
#undef min
#endif

// std::max
#ifdef max
#undef max
#endif

using namespace rapidjson;


namespace overlay::windows {


    robin_hood::unordered_map<std::string, std::unique_ptr<std::vector<uint8_t>>> DLL_MAP;
    robin_hood::unordered_map<std::string, std::unique_ptr<std::vector<uint8_t>>> DLL_MAP_ORG;

    // configuration
    std::filesystem::path PatchManager::config_path;
    std::optional<std::string> PATCH_MANAGER_CFG_PATH_OVERRIDE;

    bool PatchManager::config_dirty = false;
    bool PatchManager::setting_auto_apply = false;

    std::vector<std::string> PatchManager::setting_auto_apply_list;
    std::vector<std::string> PatchManager::setting_patches_enabled;

    std::map<std::string, std::string> PatchManager::setting_union_patches_enabled;
    std::map<std::string, int64_t> PatchManager::setting_int_patches_enabled;

    static std::string url_fetch_errors;

    std::string PatchManager::patch_url("");
    std::string PatchManager::patch_name_filter("");

    std::filesystem::path PatchManager::LOCAL_PATCHES_PATH("patches");
    std::string PatchManager::ACTIVE_JSON_FILE("");

    std::map<std::string, std::vector<std::string>> EXTRA_DLLS = {
        {"jubeat.dll", {"music_db.dll", "coin.dll"}},
        {"arkmdxp3.dll", {"gamemdx.dll"}},
        {"arkmdxp4.dll", {"gamemdx.dll"}},
        {"arkmdxbio2.dll", {"gamemdx.dll"}},
        {"arkndd.dll", {"gamendd.dll"}},
        {"arkkep.dll", {"game.dll"}},
        {"arkjc9.dll", {"gamejc9.dll"}},
        {"arkkdm.dll", {"gamekdm.dll"}},
        {"arkmmd.dll", {"gamemmd.dll"}},
        {"arkklp.dll", {"lpac.dll"}},
        {"arknck.dll", {"weac.dll"}},
        {"gdxg.dll", {"game.dll"}}
    };

    static size_t url_recent_idx = -1;
    std::vector<std::string> url_recents = {};

    std::vector<std::string> getExtraDlls(const std::string& firstDll) {
        if (!EXTRA_DLLS.contains(firstDll)) {
            return {};
        }
        return EXTRA_DLLS[firstDll];
    }

    // utility
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
            if (gle == 12175) {
                url_fetch_errors += "\nThis is ERROR_WINHTTP_SECURE_FAILURE - most likely TLS 1.1 / TLS 1.2 error on old OS versions.\n\n";
                url_fetch_errors += "Look up MSDN article on 'Update to enable TLS 1.1 and TLS 1.2 as default secure protocols in WinHTTP in Windows' for a fix.";
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
                url_fetch_errors += "(Patch server did not find any patches for this game version)";
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

    // patches
    std::vector<PatchData> PatchManager::patches;
    bool PatchManager::local_patches_initialized = false;

    // loader notifications
    bool ldr_registered = false;
    void* ldr_notify_cookie = nullptr;
    std::vector<std::string> ldr_target_libraries;

    PatchManager::PatchManager(SpiceOverlay *overlay, bool apply_patches) : Window(overlay) {
        this->title = "Patch Manager";
        this->flags |= ImGuiWindowFlags_AlwaysAutoResize;
        this->toggle_button = games::OverlayButtons::TogglePatchManager;
        this->init_pos = ImVec2(10, 10);

        if (PATCH_MANAGER_CFG_PATH_OVERRIDE.has_value()) {
            this->config_path = PATCH_MANAGER_CFG_PATH_OVERRIDE.value();
            log_info("patchmanager", "using custom config file path: {}", this->config_path.string().c_str());
        } else {
            this->config_path = std::filesystem::path(_wgetenv(L"APPDATA")) / L"spicetools_patch_manager.json";
        }

        if (!ldr_registered) {
            ldr_target_libraries = getExtraDlls(avs::game::DLL_NAME);
            ldr_target_libraries.push_back(avs::game::DLL_NAME);

            auto const register_fn = reinterpret_cast<decltype(&LdrRegisterDllNotification)>
                (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));

            auto callback = [] CALLBACK (ULONG reason, PCLDR_DLL_NOTIFICATION_DATA data, PVOID ctx) {
                if (reason == LDR_DLL_NOTIFICATION_REASON_LOADED) {
                    auto const dll = strtolower(std::filesystem::path({
                        data->Loaded.FullDllName->Buffer,
                        data->Loaded.FullDllName->Length / sizeof(wchar_t)
                    }).filename().string());

                    if (std::ranges::find(ldr_target_libraries, dll) != ldr_target_libraries.end()) {
                        static_cast<PatchManager*>(ctx)->reload_local_patches(true);
                    }
                }
            };

            if (register_fn && NT_SUCCESS(register_fn(0, callback, this, &ldr_notify_cookie))) {
                log_info("patchmanager", "registered for DLL load notifications");
            } else {
                log_warning("patchmanager", "failed to register for DLL load notifications");
            }

            ldr_registered = true;
        }

        if (!local_patches_initialized) {
            patch_url.clear();
            if (cfg::CONFIGURATOR_STANDALONE) {
                apply_patches = true;
            }
            if (fileutils::file_exists(this->config_path)) {
                this->config_load();
            }
            if (apply_patches) {
                this->reload_local_patches(apply_patches);
            }
        }
    }

    PatchManager::~PatchManager() = default;

    void PatchManager::build_content() {

        // check if initialized
        if (!local_patches_initialized) {
            if (fileutils::file_exists(config_path)) {
                this->config_load();
            }
            this->reload_local_patches();
        }

        // game code info
        std::string identifiers;
        identifiers += avs::game::get_identifier() + "\n\n";
        identifiers += avs::game::DLL_NAME + " / " + get_game_identifier(MODULE_PATH / avs::game::DLL_NAME) + "\n";

        for (const auto& dll : getExtraDlls(avs::game::DLL_NAME)) {
            const auto dll_path = MODULE_PATH / dll;
            if (fileutils::file_exists(dll_path)) {
                identifiers += dll + " / " + get_game_identifier(dll_path) + "\n";
            }
        }

        ImGui::AlignTextToFramePadding();
        ImGui::HelpMarker(identifiers.c_str());
        ImGui::SameLine();
        ImGui::Text("%s", avs::game::get_identifier().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            clipboard::copy_text(identifiers);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::HelpMarker(
            "Path being used to look for DLLs, used for reading PE header values.\n"
            "Wrong path? Run spicecfg from the correct directory, or fix your modules parameter before launching spicecfg.\n"
            "Make sure you're not using a different one when launching the game.");
        ImGui::SameLine();
        ImGui::Text("Modules Path: %s", MODULE_PATH.string().c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::DummyMarker();
        ImGui::SameLine();
        if (ACTIVE_JSON_FILE.empty()) {
            ImGui::Text("Patches JSON: built-in");
        } else {
            ImGui::Text("Patches JSON: %s", ACTIVE_JSON_FILE.c_str());
        }

        if (PATCH_MANAGER_CFG_PATH_OVERRIDE.has_value()) {
            ImGui::AlignTextToFramePadding();
            ImGui::DummyMarker();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));  // yellow
            ImGui::Text("Using Config Override: %s", PATCH_MANAGER_CFG_PATH_OVERRIDE.value().c_str());
            ImGui::PopStyleColor();
        }

        // auto apply checkbox
        ImGui::AlignTextToFramePadding();
        ImGui::HelpMarker(
                "This option is saved per game, using the date code.\n"
                "When checked, all set patches will be applied on game boot."
        );
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto apply patches on game start", &setting_auto_apply)) {
            config_dirty = true;
        }

        // check for dirty state
        if (config_dirty) {
            if (cfg::CONFIGURATOR_STANDALONE) {
                // auto save for configurator version
                this->config_save();

            } else {
                // manual save for live version
                ImGui::AlignTextToFramePadding();
                ImGui::HelpMarker("Save current patch state to the configuration file.");
                ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    this->config_save();
                }
            }
        }

        bool disable_all_patches = false;
        if (cfg::CONFIGURATOR_STANDALONE) {
            // import from url
            // only allow import in the configurator (and not in-game)
            // e.g., for public game set ups with keyboard access
            ImGui::AlignTextToFramePadding();
            ImGui::DummyMarker();
            ImGui::SameLine();
            ImGui::BeginDisabled(avs::game::DLL_NAME.empty());
            if (ImGui::Button("Import from URL##Button")) {
                ImGui::OpenPopup("Import from URL");
            }
            ImGui::EndDisabled();
            if (avs::game::DLL_NAME.empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::DummyMarker();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));
                ImGui::TextUnformatted("WARNING: Game DLL is not found, fix modules parameter! Importing is disabled.");
                ImGui::PopStyleColor();
            }

            // overwrite DLL
            ImGui::SameLine();
            if (!patches.empty()) {
                if (ImGui::Button("Overwrite game files##Button")) {
                    ImGui::OpenPopup("Overwrite game files?");
                }
            }
            if (ImGui::BeginPopupModal("Overwrite game files?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));  // yellow
                ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.5);
                ImGui::Text(
                    "Are you sure you want to permanently apply patches to your game files?");
                ImGui::PopStyleColor();
                ImGui::Text(
                    "File backups are made, but it's recommended that you keep your own copies.");
                ImGui::PopTextWrapPos();
                ImGui::Separator();
                if (ImGui::Button("Yes, overwrite")) {
                    hard_apply_patches();
                    reload_local_patches();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // disable all
            ImGui::SameLine();
            if (!patches.empty()) {
                disable_all_patches = ImGui::Button("Disable all");
                if (disable_all_patches) {
                    // reset auto apply now, and disable every patch down below
                    config_dirty = true;
                    setting_auto_apply = false;
                }
            }
        }
        
        bool url_entered = false;
        bool is_valid_url = false;
        bool patches_imported = false;

        // import from URL popup dialog
        if (ImGui::BeginPopupModal("Import from URL", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::TreeNodeEx("Warning - use at your own risk!", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.6);
                ImGui::Text(
                    "Only import patches from a trusted source. "
                    "These services are provided by third parties and may contain faulty or malicious code. "
                    "Game datecode and PE header information will be sent in the request.");
                ImGui::PopTextWrapPos();
                ImGui::TreePop();
            }

            ImGui::Spacing();

            if (ImGui::TreeNodeEx("Enter URL", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SetNextItemWidth(360.f);
                ImGui::InputTextWithHint(
                    "##url_textinput",
                    "http://www.example.com",
                    &patch_url,
                    ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll);

                if (ImGui::Button("Paste")) {
                    auto clipboard_url = clipboard::paste_text();
                    if (!clipboard_url.empty()) {
                        strreplace(clipboard_url, "\r\n", "");
                        strreplace(clipboard_url, " ", "");
                        patch_url = clipboard_url;
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(patch_url.empty());
                if (ImGui::Button("Clear")) {
                    patch_url.clear();
                    url_recent_idx = -1;
                }
                ImGui::EndDisabled();

                ImGui::TreePop();
            }

            ImGui::Spacing();

            // history
            if (ImGui::TreeNodeEx("History", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginListBox(
                        "##url_recents",
                        ImVec2(360.f, 3 * ImGui::GetTextLineHeightWithSpacing()))) {

                    for (size_t i = 0; i < url_recents.size(); i++) {
                        const bool is_selected = (url_recent_idx == i);
                        if (ImGui::Selectable(url_recents[i].c_str(), is_selected)) {
                            url_recent_idx = i;
                            patch_url = url_recents[i];
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::BeginDisabled(url_recent_idx == (size_t)(-1));
                if (ImGui::Button("Remove selected")) {
                    url_recents.erase(url_recents.begin() + url_recent_idx);
                    url_recent_idx = -1;
                    this->config_save();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(url_recents.empty());
                if (ImGui::Button("Clear all")) {
                    url_recents.clear();
                    url_recent_idx = -1;
                    this->config_save();
                }
                ImGui::EndDisabled();
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::BeginDisabled(patch_url.empty());
            if (ImGui::Button("Import")) {
                url_entered = true;
                if (patch_url.find("http://") == 0 || patch_url.find("https://") == 0) {
                    is_valid_url = true;
                }
                url_recent_idx = -1;
            }
            ImGui::EndDisabled();

            if (is_valid_url) {
                patches_imported = import_remote_patches_to_disk();
                if (patches_imported) {
                    if (std::find(url_recents.begin(), url_recents.end(), patch_url) == url_recents.end()) {
                        url_recents.emplace_back(patch_url);
                    }
                    this->config_save();
                }
                reload_local_patches();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                patch_url.clear();
                url_recent_idx = -1;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // invalid URL popup dialog
        if (url_entered && !is_valid_url) {
            ImGui::OpenPopup("URL error");
        }
        if (ImGui::BeginPopupModal("URL error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Make sure URL starts with http:// or https://");
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // URL import failure dialog
        if (url_entered && is_valid_url && !patches_imported) {
            ImGui::OpenPopup("Import failed##URLImport");
        }
        if (ImGui::BeginPopupModal("Import failed##URLImport", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to import patches from URL.");
            if (!url_fetch_errors.empty()) {
                ImGui::TextUnformatted("");
                ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.5);
                ImGui::TextUnformatted(url_fetch_errors.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // search function
        if (!patches.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::DummyMarker();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(300.f);
            ImGui::InputTextWithHint(
                "", "Type here to search..", &patch_name_filter,
                ImGuiInputTextFlags_EscapeClearsAll);
            if (!patch_name_filter.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    patch_name_filter.clear();
                }
            }
        }

        // check for empty list
        if (patches.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "No patches available.");
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "New patches are no longer being added to spice2x.");
            if (cfg::CONFIGURATOR_STANDALONE) {
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Use Import button above to load patches from an online patcher.");
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "For more information, click the button below:");
                if (ImGui::Button("More about patches")) {
                    // doing this on a separate thread to avoid polluting ImGui context
                    std::thread t([] {
                        ShellExecuteA(
                            NULL, "open",
                            "https://github.com/spice2x/spice2x.github.io/wiki/Patching-DLLs-(hex-edits)",
                            NULL, NULL, SW_SHOWNORMAL);
                    });
                    t.join();
                }
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Exit the game, launch spicecfg, and try importing patches from URL.");
            }
        } else {
            // draw patches
            if (ImGui::BeginTable("PatchesTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("##NameColumn", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##OptionsColumn", ImGuiTableColumnFlags_WidthFixed, 240);

                const auto search_str_in_lower = strtolower(patch_name_filter);
                size_t patches_shown = 0;
                for (auto &patch : patches) {
                    
                    // get patch status
                    PatchStatus patch_status = is_patch_active(patch);
                    patch.last_status = patch_status;

                    // user requested to disable all
                    if (disable_all_patches && patch.enabled) {
                        patch.enabled = false;
                        config_dirty = true;
                        switch (patch_status) {
                            case PatchStatus::Enabled:
                            case PatchStatus::Disabled:
                                apply_patch(patch, false);
                                break;
                            case PatchStatus::Error:
                                if (cfg::CONFIGURATOR_STANDALONE) {
                                    patch.enabled = false;
                                }
                                break;
                            default:
                                break;
                        }
                    }

                    // search function
                    if (!patch_name_filter.empty()) {
                        if (patch.name_in_lower_case.find(search_str_in_lower) == std::string::npos) {
                            continue;
                        }
                    }

                    // start drawing a row for this patch
                    ImGui::TableNextRow();
                    ImGui::PushID(&patch);
                    patches_shown += 1;

                    // first column, part 1: help / caution marker
                    ImGui::TableNextColumn();
                    const std::string description = patch.description;
                    const std::string caution = patch.caution;
                    if (!description.empty() && !caution.empty()) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::WarnMarker(description.c_str(), caution.c_str());
                    } else if (!description.empty()) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::HelpMarker(description.c_str());
                    } else if (!caution.empty()) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::WarnMarker(nullptr, caution.c_str());
                    } else {
                        ImGui::DummyMarker();
                    }

                    // get current state
                    bool patch_checked = patch_status == PatchStatus::Enabled;

                    // default text for the label (patch name)
                    auto patch_name = patch.name;

                    // push style
                    int style_color_pushed = 0;
                    switch (patch_status) {
                        case PatchStatus::Error:
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                            patch_name += " (Error)";
                            style_color_pushed++;
                            break;
                        case PatchStatus::Enabled:
                            if (setting_auto_apply && patch.enabled) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                                style_color_pushed++;
                            }
                            break;
                        case PatchStatus::Disabled:
                            break;
                        default:
                            break;
                    }

                    if (patch.enabled) {
                        patch_name += setting_auto_apply ? " (Auto apply)" : " (Saved)";
                    }
                    if (patch.unverified) {
                        patch_name += " (Unverified patch)";
                    }

                    // first column, part 3: name
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    // patch_name can include % (formatting markers) - ensure Unformatted widget used here
                    ImGui::TextUnformatted(patch_name.c_str());
                    if (style_color_pushed) {
                        ImGui::PopStyleColor(style_color_pushed);
                    }
                    if (patch.type == PatchType::Integer) {
                        ImGui::SameLine();
                        auto& numpatch = patch.patch_number;
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%d..%d", numpatch.min, numpatch.max);
                    }

                    // second column, part 1: enable checkbox (applies to all)
                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled(patch_status == PatchStatus::Error);
                    if (ImGui::Checkbox("##patch_checked_checkbox", &patch_checked)) {
                        config_dirty = true;
                        switch (patch_status) {
                            case PatchStatus::Enabled:
                            case PatchStatus::Disabled:
                                if (patch_checked) {
                                    setting_auto_apply = true;
                                }
                                patch.enabled = patch_checked;
                                apply_patch(patch, patch_checked);
                                break;
                            case PatchStatus::Error:
                                if (cfg::CONFIGURATOR_STANDALONE) {
                                    if (patch_checked) {
                                        setting_auto_apply = true;
                                    }
                                    patch.enabled = patch_checked;
                                }
                                break;
                            default:
                                break;
                        }
                        // update status
                        patch.last_status = is_patch_active(patch);
                    }
                    ImGui::EndDisabled();

                    // second column, part 2: additional options UI (dropdown, text input)
                    ImGui::SameLine();
                    if (patch_status == PatchStatus::Error){
                        ImGui::AlignTextToFramePadding();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                        if (patch.error_reason.empty()) {
                            ImGui::TextUnformatted("Unknown error");
                        } else {
                            ImGui::TextUnformatted(patch.error_reason.c_str());
                        }
                        ImGui::PopStyleColor();
                    } else if (patch.type == PatchType::Union || patch.type == PatchType::Integer) {
                        if (patch_status == PatchStatus::Enabled) {
                            if (patch.type == PatchType::Union) {
                                ImGui::SetNextItemWidth(200.0f);
                                if (ImGui::BeginCombo("##union_patch_dropdown", patch.selected_union_name.c_str())) {
                                    for (const auto& union_patch : patch.patches_union) {
                                        if (ImGui::Selectable(union_patch.name.c_str())) {
                                            patch.selected_union_name = union_patch.name;
                                            apply_patch(patch, true);
                                            config_dirty = true;
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            } else if (patch.type == PatchType::Integer) {
                                ImGui::SetNextItemWidth(200.0f);
                                auto& numpatch = patch.patch_number;
                                ImGui::InputInt("##int_input", &numpatch.value, 1, 10);
                                if (ImGui::IsItemDeactivatedAfterEdit()) {
                                    numpatch.value = CLAMP(
                                            numpatch.value,
                                            numpatch.min,
                                            numpatch.max);

                                    apply_patch(patch, true);
                                    config_dirty = true;
                                }
                            }
                        } else if (patch_status == PatchStatus::Disabled) {
                            ImGui::SetNextItemWidth(200.0f);
                            ImGui::BeginDisabled();
                            if (patch.type == PatchType::Union) {
                                if (ImGui::BeginCombo(
                                        "##dummy_union_patch_dropdown",
                                        patch.selected_union_name.c_str())) {
                                    ImGui::EndCombo();
                                }
                            } else if (patch.type == PatchType::Integer) {
                                ImGui::InputInt("##dummy_int_input", &patch.patch_number.value);
                            }
                            ImGui::EndDisabled();
                        }
                    } else {
                        ImGui::AlignTextToFramePadding();
                        ImGui::BeginDisabled(!patch_checked);
                        ImGui::TextUnformatted(patch_checked ? "ON" : "off");
                        ImGui::EndDisabled();
                    }
                    ImGui::PopID();
                }
                
                if (patches_shown == 0) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::DummyMarker();
                    ImGui::SameLine();
                    ImGui::BeginDisabled();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("No patches found.");
                    ImGui::EndDisabled();
                    ImGui::TableNextColumn();
                    ImGui::DummyMarker();
                }

                ImGui::EndTable();
            }
        }
    }

    void PatchManager::hard_apply_patches() {
        std::vector<std::string> written_list;
        for (auto& patch : patches) {
            switch (patch.type) {
            case PatchType::Memory:
                for (auto& memory_patch : patch.patches_memory) {
                    auto dll_path = MODULE_PATH / memory_patch.dll_name;
                    create_dll_backup(written_list, dll_path);
                    auto dll_data = fileutils::bin_read(dll_path);
                    if (dll_data) {
                        auto max_len = std::max(memory_patch.data_disabled_len, memory_patch.data_enabled_len);
                        if (memory_patch.data_offset + max_len <= dll_data->size()) {
                            if (patch.enabled) {
                                memcpy(dll_data->data() + memory_patch.data_offset, memory_patch.data_enabled.get(), memory_patch.data_enabled_len);
                            } else {
                                memcpy(dll_data->data() + memory_patch.data_offset, memory_patch.data_disabled.get(), memory_patch.data_disabled_len);
                            }
                            fileutils::bin_write(dll_path, dll_data->data(), dll_data->size());
                        }
                    }
                }
                break;
            case PatchType::Union:
                if (!patch.enabled) {
                    break;
                }
                for (auto& union_patch : patch.patches_union) {
                    if (union_patch.name == patch.selected_union_name) {
                        auto dll_path = MODULE_PATH / union_patch.dll_name;
                        create_dll_backup(written_list, dll_path);
                        auto dll_data = fileutils::bin_read(dll_path);
                        if (dll_data) {
                            if (union_patch.offset + union_patch.data_len <= dll_data->size()) {
                                memcpy(dll_data->data() + union_patch.offset, union_patch.data.get(), union_patch.data_len);
                                fileutils::bin_write(dll_path, dll_data->data(), dll_data->size());
                            }
                        }
                        break;
                    }
                }
                break;
            case PatchType::Integer:
                {
                    if (!patch.enabled) {
                        break;
                    }
                    auto& numpatch = patch.patch_number;
                    auto dll_path = MODULE_PATH / numpatch.dll_name;
                    create_dll_backup(written_list, dll_path);
                    auto dll_data = fileutils::bin_read(dll_path);
                    if (dll_data) {
                        if (numpatch.data_offset + numpatch.size_in_bytes <= dll_data->size()) {
                            int_to_little_endian_bytes(
                                numpatch.value,
                                dll_data->data() + numpatch.data_offset,
                                numpatch.size_in_bytes);
                            fileutils::bin_write(dll_path, dll_data->data(), dll_data->size());
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    void PatchManager::config_load() {
        log_info("patchmanager", "loading config");

        // read config file
        std::string config = fileutils::text_read(config_path);
        if (!config.empty()) {

            // parse document
            Document doc;
            doc.Parse(config.c_str());

            // check parse error
            auto error = doc.GetParseError();
            if (error) {
                log_warning("patchmanager", "config file parse error: {}", error);
            }

            // verify root is a dict
            if (doc.IsObject()) {

                // read auto apply settings
                auto auto_apply = doc.FindMember("auto_apply");
                if (auto_apply != doc.MemberEnd() && auto_apply->value.IsArray()) {

                    // get game id
                    auto game_id = avs::game::get_identifier();

                    // iterate entries
                    setting_auto_apply = false;
                    setting_auto_apply_list.clear();
                    for (auto &entry : auto_apply->value.GetArray()) {
                        if (entry.IsString()) {

                            // check if this is our game identifier
                            std::string entry_id = entry.GetString();
                            if (game_id == entry_id) {
                                setting_auto_apply = true;
                            }

                            // move to list
                            setting_auto_apply_list.emplace_back(entry_id);
                        }
                    }
                }

                // read enabled patches
                auto patches_enabled = doc.FindMember("patches_enabled");
                if (patches_enabled != doc.MemberEnd() && patches_enabled->value.IsArray()) {
                    setting_patches_enabled.clear();
                    for (const auto &patch : patches_enabled->value.GetArray()) {
                        if (patch.IsString()) {
                            setting_patches_enabled.emplace_back(std::string(patch.GetString()));
                        }
                    }
                }
                // read enabled union patches
                auto patches_union_enabled = doc.FindMember("union_patches_enabled");
                if (patches_union_enabled != doc.MemberEnd() && patches_union_enabled->value.IsObject()) {
                    setting_union_patches_enabled.clear();
                    for (auto it = patches_union_enabled->value.MemberBegin(); it != patches_union_enabled->value.MemberEnd(); ++it) {
                        if (it->name.IsString() && it->value.IsString()) {
                            setting_union_patches_enabled[it->name.GetString()] = it->value.GetString();
                        }
                    }
                }
                // read enabled integer patches
                auto patches_int_enabled = doc.FindMember("integer_patches_enabled");
                if (patches_int_enabled != doc.MemberEnd() && patches_int_enabled->value.IsObject()) {
                    setting_int_patches_enabled.clear();
                    for (auto it = patches_int_enabled->value.MemberBegin(); it != patches_int_enabled->value.MemberEnd(); ++it) {
                        if (it->name.IsString() && it->value.IsNumber()) {
                            setting_int_patches_enabled[it->name.GetString()] = it->value.GetInt();
                        }
                    }
                }

                // read remote patch URLs
                auto remote_url_history = doc.FindMember("remote_url_history");
                if (remote_url_history != doc.MemberEnd() && remote_url_history->value.IsArray()) {
                    url_recents.clear();
                    for (const auto &url : remote_url_history->value.GetArray()) {
                        if (url.IsString()) {
                            url_recents.emplace_back(std::string(url.GetString()));
                        }
                    }
                }
            }
        }
    }

    static std::string patch_hash(PatchData &patch) {
        SHA256 hash;
        hash.add(patch.game_code.c_str(), patch.game_code.length());
        if (patch.datecode_min != 0 || patch.datecode_max != 0) {
            hash.add(&patch.datecode_min, sizeof(patch.datecode_min));
            hash.add(&patch.datecode_max, sizeof(patch.datecode_max));
        }
        if (!patch.peIdentifier.empty()) {
            hash.add(patch.peIdentifier.c_str(), patch.peIdentifier.length());
        }
        hash.add(patch.name.c_str(), patch.name.length());
        hash.add(patch.description.c_str(), patch.description.length());
        return hash.getHash();
    }

    void PatchManager::config_save() {

        // create document
        Document doc;
        doc.Parse(
                "{"
                "  \"auto_apply\": [],"
                "  \"patches_enabled\": [],"
                "  \"union_patches_enabled\": {},"
                "  \"integer_patches_enabled\": {},"
                "  \"remote_url_history\": []"
                "}"
        );

        // check parse error
        auto error = doc.GetParseError();
        if (error) {
            log_warning("patchmanager", "template parse error: {}", error);
        }

        // auto apply setting
        auto &auto_apply_list = doc["auto_apply"];
        auto game_id = avs::game::get_identifier();
        bool game_id_added = false;
        for (auto &entry : setting_auto_apply_list) {
            if (entry == game_id) {
                if (!setting_auto_apply) {
                    continue;
                }
                game_id_added = true;
            }
            auto_apply_list.PushBack(StringRef(entry.c_str()), doc.GetAllocator());
        }
        if (setting_auto_apply && !game_id_added) {
            auto_apply_list.PushBack(StringRef(game_id.c_str()), doc.GetAllocator());
        }

        // get enabled patches
        auto &doc_patches_enabled = doc["patches_enabled"];
        auto &doc_union_patches_enable = doc["union_patches_enabled"];
        auto &doc_int_patches_enable = doc["integer_patches_enabled"];
        for (auto &patch : patches) {
            auto hash = patch_hash(patch);

            if (patch.type == PatchType::Union) {
                // enable hash if known as enabled, overridden and missing from list
                if (patch.enabled) {
                    setting_union_patches_enabled[hash] = patch.selected_union_name;
                } else {
                    setting_union_patches_enabled.erase(hash);
                }
            } else if (patch.type == PatchType::Integer) {
                if (patch.enabled) {
                    setting_int_patches_enabled[hash] = patch.patch_number.value;
                } else {
                    setting_int_patches_enabled.erase(hash);
                }
            } else {
                // hash patch and find entry
                auto entry = std::find(setting_patches_enabled.begin(), setting_patches_enabled.end(), hash);

                // enable hash if known as enabled, overridden and missing from list
                if ((patch.last_status == PatchStatus::Enabled && patch.enabled)
                    || (cfg::CONFIGURATOR_STANDALONE && patch.last_status == PatchStatus::Error && patch.enabled)) {
                    if (entry == setting_patches_enabled.end()) {
                        setting_patches_enabled.emplace_back(hash);
                    }
                }

                // disable hash if patch known as disabled
                if (patch.last_status == PatchStatus::Disabled
                    || (cfg::CONFIGURATOR_STANDALONE && patch.last_status == PatchStatus::Error && !patch.enabled)) {
                    if (entry != setting_patches_enabled.end()) {
                        setting_patches_enabled.erase(entry);
                    }
                }
            }
        }

        // add hashes to document
        for (auto &hash : setting_patches_enabled) {
            Value hash_value(hash.c_str(), doc.GetAllocator());
            doc_patches_enabled.PushBack(hash_value, doc.GetAllocator());
        }

        for (auto& it : setting_union_patches_enabled) {
            const std::string& key = it.first;
            const std::string& val = it.second;
            doc_union_patches_enable.AddMember(StringRef(key.c_str()), StringRef(val.c_str()), doc.GetAllocator());
        }

        for (auto& it : setting_int_patches_enabled) {
            const std::string& key = it.first;
            const int32_t& val = it.second;
            doc_int_patches_enable.AddMember(StringRef(key.c_str()), val, doc.GetAllocator());
        }

        // remote URLs
        auto &doc_url_history = doc["remote_url_history"];
        for (auto& url : url_recents) {
            Value url_value(url.c_str(), doc.GetAllocator());
            doc_url_history.PushBack(url_value, doc.GetAllocator());
        }

        // build JSON
        StringBuffer buffer;
        PrettyWriter<StringBuffer> writer(buffer);
        doc.Accept(writer);

        // save to file
        if (fileutils::text_write(config_path, buffer.GetString())) {
            config_dirty = false;
        } else {
            log_warning("patchmanager", "unable to save config file");
        }
    }

    std::string get_game_identifier(const std::filesystem::path& dll_path) {
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

        return identifier;
    }

    void PatchManager::load_embedded_patches(bool apply_patches) {
        // load embedded patches from resources
        auto patches_json = resutil::load_file_string(IDR_PATCHES);

        // parse document
        Document doc;
        doc.Parse(patches_json.c_str());

        // check parse error
        auto error = doc.GetParseError();
        if (error) {
            log_warning("patchmanager", "embedded patches json file parse error: {}", error);
        }

        // iterate patches
        for (auto &patch : doc.GetArray()) {

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
                .game_code = game_code_it->value.GetString(),
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
                    log_warning("patchmanager", "unknown patch type: {}", patch_data.type);
                    break;
            }

            // auto apply
            if (apply_patches && setting_auto_apply && patch_data.enabled) {
                print_auto_apply_status(patch_data);
                apply_patch(patch_data, true);
            }

            // remember patch
            patches.emplace_back(patch_data);
        }
    }

    bool PatchManager::import_remote_patches_for_dll(const std::string& url, const std::string& dll_name) {
        log_info("patchmanager", "loading remote patches for {}...", dll_name);
        std::string identifier = get_game_identifier(MODULE_PATH / dll_name);
        std::string url_cpy = url;
        if (url_cpy.back() != '/')
            url_cpy += '/';
        std::string json_path = fmt::format("{}{}.json", url_cpy, identifier);
        try {
            auto patches_json = getFromUrl(dll_name, json_path);
            if (!patches_json.empty()) {
                if (!fileutils::dir_exists(LOCAL_PATCHES_PATH))
                    fileutils::dir_create(LOCAL_PATCHES_PATH);
                std::filesystem::path save_path = LOCAL_PATCHES_PATH / (identifier + ".json");
                fileutils::text_write(save_path, patches_json);
                return true;
            } else {
                log_warning("patchmanager", "failed to fetch patches JSON for {}", dll_name);
            }
        } catch (const std::exception& e) {
            log_warning("patchmanager", "exception occurred while loading remote patches JSON for {}: {}", dll_name, e.what());
        }
        return false;
    }

    bool PatchManager::load_from_patches_json(bool apply_patches) {
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
                log_misc("patchmanager", "file does not exist, skipping: {}", patches_json_path.string());
                continue;
            }

            log_misc("patchmanager", "reading from patches.json: {}", patches_json_path.string());
            std::string content = fileutils::text_read(patches_json_path);
            append_patches(content, apply_patches, filter);

            const auto new_patches = patches.size() - patches_size_previous;
            log_info("patchmanager", "loaded {} patches from: {}", new_patches, patches_json_path.string());
            if (0 < new_patches) {
                ret = true;
                ACTIVE_JSON_FILE = patches_json_path.string();
                break;
            }
        }
        return ret;
    }

    void PatchManager::reload_local_patches(bool apply_patches) {
        // announce reload
        if (apply_patches) {
            log_info("patchmanager", "reloading (local) and applying patches");
        } else {
            log_info("patchmanager", "reloading (local) patches");
        }

        // clear old patches
        patches.clear();
        if (cfg::CONFIGURATOR_STANDALONE) {
            DLL_MAP.clear();
            DLL_MAP_ORG.clear();
        }

        ACTIVE_JSON_FILE = "";

        std::string firstDll = avs::game::DLL_NAME;
        std::string first_id = get_game_identifier(MODULE_PATH / firstDll);
        std::filesystem::path firstPath = fmt::format("patches/{}.json", first_id);

        auto extraDlls = getExtraDlls(firstDll);
        std::erase_if(extraDlls, [](const std::string& dll) {
            auto identifier = get_game_identifier(MODULE_PATH / dll);
            return identifier.empty() || !fileutils::file_exists(fmt::format("patches/{}.json", identifier));
        });

        if (fileutils::file_exists(firstPath) || !extraDlls.empty()) {
            if (fileutils::file_exists(firstPath)) {
                log_info("patchmanager", "loaded patches for {} from {}", firstDll, firstPath.string());
                std::string content = fileutils::text_read(firstPath);
                append_patches(content, apply_patches, nullptr, first_id);
                ACTIVE_JSON_FILE = firstPath.string();
            }
            for (const std::string& dll : extraDlls) {
                auto extraId = get_game_identifier(MODULE_PATH / dll);
                auto extraPath = std::filesystem::path(fmt::format("patches/{}.json", extraId));
                log_info("patchmanager", "loaded patches for {} from {}", dll, extraPath.string());
                std::string content = fileutils::text_read(extraPath);
                append_patches(content, apply_patches, nullptr, extraId);
                if (ACTIVE_JSON_FILE.empty()) {
                    ACTIVE_JSON_FILE = extraPath.string();
                } else {
                    ACTIVE_JSON_FILE += ", " + extraPath.string();
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

    bool PatchManager::import_remote_patches_to_disk() {
        bool imported = false;
        // clear old patches
        patches.clear();
        url_fetch_errors.clear();

        // load patches for main dll
        imported = import_remote_patches_for_dll(patch_url, avs::game::DLL_NAME);

        // check for additional patches based on module name
        for (const std::string& dll : getExtraDlls(avs::game::DLL_NAME)) {
            imported |= import_remote_patches_for_dll(patch_url, dll);
        }

        return imported;
    }

    void PatchManager::append_patches(
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
                error,
                rapidjson::GetParseError_En(error));
        }

        // iterate patches
        for (auto &patch : doc.GetArray()) {

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
                .game_code = game_code_it->value.GetString(),
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
                log_warning("patchmanager", "unknown patch type: {}", patch_data.type);
                break;
            }

            // auto apply
            if (apply_patches && setting_auto_apply && patch_data.enabled) {
                print_auto_apply_status(patch_data);
                apply_patch(patch_data, true);
            }

            // remember patch
            patches.emplace_back(patch_data);
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

    void create_dll_backup(
        std::vector<std::string>& written_list, const std::filesystem::path& dll_path) {

        // if dll_path is not in written_list, create a file backup.
        if (std::find(written_list.begin(), written_list.end(), dll_path.string()) == written_list.end()) {
            written_list.push_back(dll_path.string());
            auto dll_bak_path = std::filesystem::path(dll_path.string() + ".bak");
            try {
                if (!fileutils::file_exists(dll_bak_path)) {
                    std::filesystem::copy(dll_path, dll_bak_path);
                }
                log_info("patchmanager", "created DLL backup for: {}", dll_path.string());
            } catch (const std::filesystem::filesystem_error& e) {
                log_warning(
                    "patchmanager",
                    "filesystem error while creating DLL backup for {}, error: {}",
                    dll_path.string(), e.what());
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
            log_warning("patchmanager", "{} does not exist", dll_path.string());
            return nullptr;
        }

        // get module
        auto module = libutils::try_module(dll_path);
        if (!module) {
            log_warning("patchmanager", "cannot get module: {}", dll_path.string());
            return nullptr;
        }

        // convert offset to RVA
        auto offset = libutils::offset2rva(dll_path, (intptr_t)data_offset);
        if (offset == -1) {
            log_warning(
                "patchmanager", "cannot convert offset to RVA: {}, {}",
                dll_path.string(), data_offset);
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
                dll_path.string(), GetLastError());

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
