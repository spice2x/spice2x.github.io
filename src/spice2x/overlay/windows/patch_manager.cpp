#include "patch_manager.h"

#include <algorithm>
#include <shellapi.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "avs/game.h"
#include "cfg/configurator.h"
#include "games/io.h"
#include "launcher/launcher.h"
#include "misc/clipboard.h"
#include "overlay/imgui/extensions.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/utils.h"

namespace overlay::windows {

    namespace {

        enum class PatchGroupStatus {
            Error,
            Disabled,
            Enabled,
            Mixed,
        };

        struct PatchGroupState {
            PatchGroupStatus status = PatchGroupStatus::Disabled;
            size_t first_error_index = static_cast<size_t>(-1);
            size_t configured_count = 0;
            size_t unverified_count = 0;
        };

        using PatchGroupMembers = std::unordered_map<const patcher::PatchGroup*, std::vector<size_t>>;

        struct PatchGroupSearchMatch {
            bool group_matches = false;
            bool child_matches = false;

            bool visible() const {
                return group_matches || child_matches;
            }
        };

        PatchGroupState get_patch_group_state(const std::vector<size_t>& members) {
            PatchGroupState state;
            bool any_enabled = false;
            bool any_disabled = false;

            for (const auto patch_index : members) {
                const auto& patch = patcher::patches[patch_index];
                if (patch.enabled) {
                    state.configured_count++;
                }
                if (patch.unverified) {
                    state.unverified_count++;
                }

                switch (patch.last_status) {
                    case patcher::PatchStatus::Error:
                        if (state.first_error_index == static_cast<size_t>(-1)) {
                            state.first_error_index = patch_index;
                        }
                        break;
                    case patcher::PatchStatus::Enabled:
                        any_enabled = true;
                        break;
                    case patcher::PatchStatus::Disabled:
                        any_disabled = true;
                        break;
                    default:
                        break;
                }
            }

            if (state.first_error_index != static_cast<size_t>(-1)) {
                state.status = PatchGroupStatus::Error;
            } else if (any_enabled && any_disabled) {
                state.status = PatchGroupStatus::Mixed;
            } else if (any_enabled) {
                state.status = PatchGroupStatus::Enabled;
            }

            return state;
        }

        PatchGroupMembers collect_patch_group_members() {
            PatchGroupMembers group_members;
            for (const auto patch_index : patcher::patches_sorted) {
                const auto *group = patcher::find_patch_group(patcher::patches[patch_index]);
                if (group) {
                    group_members[group].push_back(patch_index);
                }
            }
            return group_members;
        }

        PatchGroupSearchMatch get_patch_group_search_match(
            const patcher::PatchGroup& group,
            const std::vector<size_t>& members,
            const std::string& search_str_in_lower) {
            PatchGroupSearchMatch match;
            match.group_matches = search_str_in_lower.empty()
                || group.name_in_lower_case.find(search_str_in_lower) != std::string::npos;
            if (match.group_matches) {
                return match;
            }

            for (const auto member_index : members) {
                if (patcher::patches[member_index].name_in_lower_case.find(search_str_in_lower)
                    != std::string::npos) {
                    match.child_matches = true;
                    break;
                }
            }
            return match;
        }

        ImU32 get_patch_row_background_color(size_t logical_row_index) {
            const auto color = logical_row_index % 2 == 0
                ? ImGuiCol_TableRowBg
                : ImGuiCol_TableRowBgAlt;
            return ImGui::GetColorU32(color);
        }

        void begin_patch_row(ImU32 row_background_color) {
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_background_color);
        }

        void draw_patch_group_child_guide(
            const ImVec2& cursor,
            float branch_x,
            float branch_end_x,
            bool last_group_child) {
            const auto& style = ImGui::GetStyle();
            const float row_midpoint = cursor.y + ImGui::GetFrameHeight() * 0.5f;
            const float row_top = cursor.y - style.CellPadding.y;
            const float row_bottom = last_group_child
                ? row_midpoint
                : cursor.y + ImGui::GetFrameHeight() + style.CellPadding.y;
            const auto guide_color = ImGui::GetColorU32(ImGuiCol_Separator, 0.8f);
            const float guide_thickness = overlay::apply_scaling(1.0f);
            auto *draw_list = ImGui::GetWindowDrawList();
            draw_list->AddLine(
                ImVec2(branch_x, row_top),
                ImVec2(branch_x, row_bottom),
                guide_color,
                guide_thickness);
            draw_list->AddLine(
                ImVec2(branch_x, row_midpoint),
                ImVec2(branch_end_x, row_midpoint),
                guide_color,
                guide_thickness);
        }

        void render_patch_group_child_gutter(bool last_group_child) {
            const auto& style = ImGui::GetStyle();
            const auto cursor = ImGui::GetCursorScreenPos();
            const float gutter_width = style.IndentSpacing * 1.5f;
            const float branch_x = cursor.x + style.IndentSpacing * 0.5f;
            draw_patch_group_child_guide(
                cursor,
                branch_x,
                cursor.x + gutter_width,
                last_group_child);
            ImGui::Dummy(ImVec2(gutter_width, ImGui::GetFrameHeight()));
        }

        void set_patch_option_width() {
            const float available_width = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(available_width < 200.0f ? available_width : 200.0f);
        }

        void show_patch_group_tooltip(const patcher::PatchGroup& group) {
            if (!group.caution.empty()) {
                ImGui::WarnTooltip(group.description.c_str(), group.caution.c_str());
            } else if (!group.description.empty()) {
                ImGui::HelpTooltip(group.description.c_str());
            }
        }

        std::string get_patch_group_display_name(
            const patcher::PatchGroup& group,
            const PatchGroupState& state,
            size_t member_count) {
            std::string name = group.name;
            if (state.status == PatchGroupStatus::Error) {
                name += " (Error)";
            }
            if (state.configured_count == member_count) {
                name += patcher::setting_auto_apply ? " (Auto apply)" : " (Saved)";
            } else if (state.configured_count > 0) {
                name += patcher::setting_auto_apply
                    ? " (Partial auto apply)"
                    : " (Partially saved)";
            }
            if (state.unverified_count == member_count) {
                name += " (Unverified patches)";
            } else if (state.unverified_count > 0) {
                name += " (Contains unverified patch)";
            }
            return name;
        }

        void render_patch_group_mixed_checkbox_mark() {
            const auto check_min = ImGui::GetItemRectMin();
            const float check_size = ImGui::GetFrameHeight();
            const float calculated_padding = check_size / 3.6f;
            const float padding = calculated_padding < 1.0f ? 1.0f : calculated_padding;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(check_min.x + padding, check_min.y + padding),
                ImVec2(
                    check_min.x + check_size - padding,
                    check_min.y + check_size - padding),
                ImGui::GetColorU32(ImGuiCol_CheckMark),
                ImGui::GetStyle().FrameRounding);
        }

        bool render_patch_group_checkbox(
            const PatchGroupState& state,
            const std::vector<size_t>& members,
            bool& checked) {
            const bool mixed = state.status == PatchGroupStatus::Mixed;
            ImGui::BeginDisabled(state.status == PatchGroupStatus::Error);
            if (mixed) {
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.f, 0.f, 0.f, 0.f));
            }
            const bool changed = ImGui::Checkbox("##group_checked_checkbox", &checked);
            if (mixed) {
                ImGui::PopStyleColor();
            }
            ImGui::EndDisabled();
            if (mixed && !changed) {
                render_patch_group_mixed_checkbox_mark();
            }
            if (!changed) {
                return false;
            }

            if (checked) {
                patcher::setting_auto_apply = true;
            }
            for (const auto member_index : members) {
                auto& member = patcher::patches[member_index];
                member.enabled = checked;
                patcher::apply_patch(member, checked);
                member.last_status = patcher::is_patch_active(member);
            }
            patcher::config_dirty = true;
            return true;
        }

        void render_patch_group_status(const PatchGroupState& state, bool checked) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (state.status == PatchGroupStatus::Error) {
                const auto& error_patch = patcher::patches[state.first_error_index];
                const auto error_reason = error_patch.error_reason.empty()
                    ? "Unknown error"
                    : error_patch.error_reason;
                const auto error_text = error_patch.name + ": " + error_reason;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                ImGui::TextUnformatted(error_text.c_str());
                ImGui::PopStyleColor();
            } else if (state.status == PatchGroupStatus::Mixed) {
                ImGui::TextUnformatted("mixed");
            } else {
                ImGui::BeginDisabled(!checked);
                ImGui::TextUnformatted(checked ? "ON" : "off");
                ImGui::EndDisabled();
            }
        }

        bool render_patch_group_parent(
            const patcher::PatchGroup& group,
            const std::vector<size_t>& members,
            bool child_matches_search,
            ImU32 row_background_color) {
            auto group_state = get_patch_group_state(members);

            begin_patch_row(row_background_color);
            // render status first so its checkbox can set the tree's next open state
            ImGui::TableSetColumnIndex(1);
            bool group_checked = group_state.status == PatchGroupStatus::Enabled
                || group_state.status == PatchGroupStatus::Mixed;
            const bool group_checkbox_changed = render_patch_group_checkbox(
                group_state,
                members,
                group_checked);
            if (group_checkbox_changed) {
                group_state = get_patch_group_state(members);
            }
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                show_patch_group_tooltip(group);
            }
            render_patch_group_status(group_state, group_checked);

            ImGui::TableSetColumnIndex(0);
            const auto group_name = get_patch_group_display_name(
                group,
                group_state,
                members.size());
            bool group_text_color_set = false;
            ImVec4 group_text_color;
            switch (group_state.status) {
                case PatchGroupStatus::Error:
                    group_text_color = ImVec4(1.f, 0.f, 0.f, 1.f);
                    group_text_color_set = true;
                    break;
                case PatchGroupStatus::Enabled:
                    if (patcher::setting_auto_apply
                        && group_state.configured_count == members.size()) {
                        group_text_color = ImVec4(0.f, 1.f, 0.f, 1.f);
                        group_text_color_set = true;
                    }
                    break;
                case PatchGroupStatus::Mixed:
                    group_text_color = ImVec4(0.f, 1.f, 0.f, 1.f);
                    group_text_color_set = true;
                    break;
                case PatchGroupStatus::Disabled:
                default:
                    break;
            }
            if (!group.caution.empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::WarnMarker(group.description.c_str(), group.caution.c_str());
            } else {
                ImGui::DummyMarker();
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::AlignTextToFramePadding();
            if (child_matches_search) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            } else if (group_checkbox_changed) {
                ImGui::SetNextItemOpen(group_checked, ImGuiCond_Always);
            }
            if (group_text_color_set) {
                ImGui::PushStyleColor(ImGuiCol_Text, group_text_color);
            }
            bool group_open = ImGui::TreeNodeEx(
                "##group_tree",
                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                "%s",
                group_name.c_str());
            if (group_text_color_set) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                show_patch_group_tooltip(group);
            }

            ImGui::HighlightTableRowOnHover();
            return group_open;
        }

    }

    // user-assigned IDs for the patches table columns, used by the sort logic
    enum PatchColumnId {
        PATCH_COLUMN_NAME = 0,
        PATCH_COLUMN_STATUS = 1,
    };

    std::string PatchManager::patch_name_filter("");

    PatchManager::PatchManager(SpiceOverlay *overlay) : Window(overlay) {
        // configurator skips the launcher's init(), so call it here as well
        // this is idempotent, so a no-op if it happens to be called twice
        patcher::init();
        this->title = "Patch Manager";
        this->flags |= ImGuiWindowFlags_AlwaysAutoResize;
        this->toggle_button = games::OverlayButtons::TogglePatchManager;
        this->init_pos = ImVec2(10, 10);
    }

    PatchManager::~PatchManager() = default;

    void PatchManager::build_content() {

        // check if initialized
        if (!patcher::local_patches_initialized) {
            if (fileutils::file_exists(patcher::config_path)) {
                patcher::config_load();
            }
            patcher::reload_local_patches();
        }

        if (avs::game::DLL_NAME == "kamunity.dll") {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Patches are not supported for Unity-based games.");
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Instead, look for downloads of pre-patched DLLs.");
            return;
        }

        // game code info
        std::string identifiers;
        identifiers += avs::game::get_identifier() + "\n\n";
        identifiers += avs::game::DLL_NAME + " / " + patcher::get_game_identifier(MODULE_PATH / avs::game::DLL_NAME) + "\n";

        for (const auto& dll : patcher::getExtraDlls(avs::game::DLL_NAME)) {
            const auto dll_path = MODULE_PATH / dll;
            if (fileutils::file_exists(dll_path)) {
                identifiers += dll + " / " + patcher::get_game_identifier(dll_path) + "\n";
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
        ImGui::Text("Modules Path: %s", patcher::displayPath(MODULE_PATH).c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::DummyMarker();
        ImGui::SameLine();
        if (patcher::ACTIVE_JSON_FILE.empty()) {
            ImGui::Text("Patches JSON: built-in");
        } else {
            ImGui::Text("Patches JSON: %s", patcher::ACTIVE_JSON_FILE.c_str());
        }

        if (patcher::PATCH_MANAGER_CFG_PATH_OVERRIDE.has_value()) {
            ImGui::AlignTextToFramePadding();
            ImGui::DummyMarker();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.f, 1.f));  // yellow
            ImGui::Text(
                "Using Config Override: %s",
                patcher::PATCH_MANAGER_CFG_PATH_OVERRIDE.value().c_str());
            ImGui::PopStyleColor();
        }

        // auto apply checkbox
        ImGui::AlignTextToFramePadding();
        ImGui::HelpMarker(
                "This option is saved per game, using the date code.\n"
                "When checked, all set patches will be applied on game boot."
        );
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto apply patches on game start", &patcher::setting_auto_apply)) {
            patcher::config_dirty = true;
        }

        // handle dirty state
        if (cfg::CONFIGURATOR_STANDALONE) {
            // auto save for configurator version
            if (patcher::config_dirty) {
                patcher::config_save();
            }
        } else {
            // manual save for live version: always render the row so the rest
            // of the panel doesn't shift when config_dirty flips. Disable the
            // button when there is nothing to save.
            ImGui::AlignTextToFramePadding();
            ImGui::HelpMarker("Save current patch state to the configuration file.");
            ImGui::SameLine();
            ImGui::BeginDisabled(!patcher::config_dirty);
            if (ImGui::Button("Save")) {
                patcher::config_save();
            }
            ImGui::EndDisabled();
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
            if (!patcher::patches.empty()) {
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
                    patcher::hard_apply_patches();
                    patcher::reload_local_patches();
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
            if (!patcher::patches.empty()) {
                disable_all_patches = ImGui::Button("Disable all");
                if (disable_all_patches) {
                    // reset auto apply now, and disable every patch down below
                    patcher::config_dirty = true;
                    patcher::setting_auto_apply = false;
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
                    &patcher::patch_url,
                    ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll);

                if (ImGui::Button("Paste from clipboard")) {
                    auto clipboard_url = clipboard::paste_text();
                    if (!clipboard_url.empty()) {
                        strreplace(clipboard_url, "\r\n", "");
                        strreplace(clipboard_url, " ", "");
                        patcher::patch_url = clipboard_url;
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(patcher::patch_url.empty());
                if (ImGui::Button("Clear")) {
                    patcher::patch_url.clear();
                    patcher::url_recent_idx = -1;
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

                    for (size_t i = 0; i < patcher::url_recents.size(); i++) {
                        const bool is_selected = (patcher::url_recent_idx == i);
                        if (ImGui::Selectable(patcher::url_recents[i].c_str(), is_selected)) {
                            patcher::url_recent_idx = i;
                            patcher::patch_url = patcher::url_recents[i];
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::BeginDisabled(patcher::url_recent_idx == (size_t)(-1));
                if (ImGui::Button("Remove selected")) {
                    patcher::url_recents.erase(patcher::url_recents.begin() + patcher::url_recent_idx);
                    patcher::url_recent_idx = -1;
                    patcher::config_save();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(patcher::url_recents.empty());
                if (ImGui::Button("Clear all")) {
                    patcher::url_recents.clear();
                    patcher::url_recent_idx = -1;
                    patcher::config_save();
                }
                ImGui::EndDisabled();
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::BeginDisabled(patcher::patch_url.empty());
            if (ImGui::Button("Import")) {
                url_entered = true;
                if (patcher::patch_url.find("http://") == 0 || patcher::patch_url.find("https://") == 0) {
                    is_valid_url = true;
                }
                patcher::url_recent_idx = -1;
            }
            ImGui::EndDisabled();

            if (is_valid_url) {
                patches_imported = patcher::import_remote_patches_to_disk();
                if (patches_imported) {
                    if (std::find(patcher::url_recents.begin(), patcher::url_recents.end(), patcher::patch_url) == patcher::url_recents.end()) {
                        patcher::url_recents.emplace_back(patcher::patch_url);
                    }
                    patcher::config_save();
                }
                // import_remote_patches_to_disk clears out old patches, so regardless of result,
                // reload patches from disk
                patcher::reload_local_patches(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                patcher::patch_url.clear();
                patcher::url_recent_idx = -1;
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
            if (!patcher::url_fetch_errors.empty()) {
                ImGui::TextUnformatted("");
                ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.7);
                ImGui::TextUnformatted(patcher::url_fetch_errors.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            if (!patcher::url_fetch_errors.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Copy Error")) {
                    clipboard::copy_text(patcher::url_fetch_errors);
                }
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // search function
        if (!patcher::patches.empty()) {
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
        if (patcher::patches.empty()) {
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
            ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding,
                    ImGui::GetStyle().CellPadding.y + overlay::apply_scaling(2));
            if (ImGui::BeginTable("PatchesTable", 2,
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg
                    | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
                ImGui::TableSetupColumn(
                    "Patch",
                    ImGuiTableColumnFlags_WidthStretch,
                    0.f, PATCH_COLUMN_NAME);
                ImGui::TableSetupColumn(
                    "Status",
                    ImGuiTableColumnFlags_WidthFixed,
                    240, PATCH_COLUMN_STATUS);
                ImGui::TableHeadersRow();

                // refresh every child before sorting and aggregating group state
                for (auto& patch : patcher::patches) {
                    patch.last_status = patcher::is_patch_active(patch);

                    if (disable_all_patches && patch.enabled) {
                        patch.enabled = false;
                        patcher::config_dirty = true;
                        switch (patch.last_status) {
                            case patcher::PatchStatus::Enabled:
                            case patcher::PatchStatus::Disabled:
                                patcher::apply_patch(patch, false);
                                patch.last_status = patcher::is_patch_active(patch);
                                break;
                            case patcher::PatchStatus::Error:
                            default:
                                break;
                        }
                    }
                }

                // maintain a sorted view of patches so the underlying vector
                // order (used by config save and hashing) is left untouched
                update_sorted_patches();

                const auto search_str_in_lower = strtolower(patch_name_filter);
                size_t items_shown = 0;
                auto render_patch = [&](size_t patch_index, ImU32 row_background_color,
                                        bool group_child = false,
                                        bool last_group_child = false) {
                    auto &patch = patcher::patches[patch_index];
                    const patcher::PatchStatus patch_status = patch.last_status;

                    // start drawing a row for this patch
                    begin_patch_row(row_background_color);
                    ImGui::PushID(&patch);

                    // first column, part 1: help / caution marker
                    ImGui::TableNextColumn();
                    if (!patch.caution.empty()) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::WarnMarker(patch.description.c_str(), patch.caution.c_str());
                    } else {
                        ImGui::DummyMarker();
                    }
                    if (group_child) {
                        ImGui::SameLine();
                        render_patch_group_child_gutter(last_group_child);
                    }

                    // get current state
                    bool patch_checked = patch_status == patcher::PatchStatus::Enabled;

                    // default text for the label (patch name)
                    auto patch_name = patch.name;

                    // push style
                    int style_color_pushed = 0;
                    switch (patch_status) {
                        case patcher::PatchStatus::Error:
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                            patch_name += " (Error)";
                            style_color_pushed++;
                            break;
                        case patcher::PatchStatus::Enabled:
                            if (patcher::setting_auto_apply && patch.enabled) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 0.f, 1.f));
                                style_color_pushed++;
                            }
                            break;
                        case patcher::PatchStatus::Disabled:
                            break;
                        default:
                            break;
                    }

                    if (patch.enabled) {
                        patch_name += patcher::setting_auto_apply ? " (Auto apply)" : " (Saved)";
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
                    if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                        show_patch_tooltip(patch);
                    }

                    // show range after label for integer patches
                    if (patch.type == patcher::PatchType::Integer) {
                        ImGui::SameLine();
                        auto& numpatch = patch.patch_number;
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%d..%d", numpatch.min, numpatch.max);
                    }

                    // second column, part 1: enable checkbox (applies to all)
                    ImGui::TableNextColumn();
                    if (group_child) {
                        render_patch_group_child_gutter(last_group_child);
                        ImGui::SameLine();
                    }
                    ImGui::BeginDisabled(patch_status == patcher::PatchStatus::Error);
                    if (ImGui::Checkbox("##patch_checked_checkbox", &patch_checked)) {
                        patcher::config_dirty = true;
                        switch (patch_status) {
                            case patcher::PatchStatus::Enabled:
                            case patcher::PatchStatus::Disabled:
                                if (patch_checked) {
                                    patcher::setting_auto_apply = true;
                                }
                                patch.enabled = patch_checked;
                                patcher::apply_patch(patch, patch_checked);
                                break;
                            case patcher::PatchStatus::Error:
                                if (cfg::CONFIGURATOR_STANDALONE) {
                                    if (patch_checked) {
                                        patcher::setting_auto_apply = true;
                                    }
                                    patch.enabled = patch_checked;
                                }
                                break;
                            default:
                                break;
                        }
                        // update status
                        patch.last_status = patcher::is_patch_active(patch);
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                        show_patch_tooltip(patch);
                    }

                    // second column, part 2: additional options UI (dropdown, text input)
                    ImGui::SameLine();
                    if (patch_status == patcher::PatchStatus::Error){
                        ImGui::AlignTextToFramePadding();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
                        if (patch.error_reason.empty()) {
                            ImGui::TextUnformatted("Unknown error");
                        } else {
                            ImGui::TextUnformatted(patch.error_reason.c_str());
                        }
                        ImGui::PopStyleColor();
                    } else if (patch.type == patcher::PatchType::Union || patch.type == patcher::PatchType::Integer) {
                        if (patch_status == patcher::PatchStatus::Enabled) {
                            if (patch.type == patcher::PatchType::Union) {
                                set_patch_option_width();
                                if (ImGui::BeginCombo("##union_patch_dropdown", patch.selected_union_name.c_str())) {
                                    for (const auto& union_patch : patch.patches_union) {
                                        if (ImGui::Selectable(union_patch.name.c_str())) {
                                            patch.selected_union_name = union_patch.name;
                                            patcher::apply_patch(patch, true);
                                            patcher::config_dirty = true;
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                                    show_patch_tooltip(patch);
                                }
                            } else if (patch.type == patcher::PatchType::Integer) {
                                set_patch_option_width();
                                auto& numpatch = patch.patch_number;
                                ImGui::InputInt("##int_input", &numpatch.value, 1, 10);
                                if (ImGui::IsItemDeactivatedAfterEdit()) {
                                    numpatch.value = CLAMP(
                                            numpatch.value,
                                            numpatch.min,
                                            numpatch.max);

                                    patcher::apply_patch(patch, true);
                                    patcher::config_dirty = true;
                                }
                                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                                    show_patch_tooltip(patch);
                                }
                            }
                        } else if (patch_status == patcher::PatchStatus::Disabled) {
                            set_patch_option_width();
                            ImGui::BeginDisabled();
                            if (patch.type == patcher::PatchType::Union) {
                                if (ImGui::BeginCombo(
                                        "##dummy_union_patch_dropdown",
                                        patch.selected_union_name.c_str())) {
                                    ImGui::EndCombo();
                                }
                            } else if (patch.type == patcher::PatchType::Integer) {
                                ImGui::InputInt("##dummy_int_input", &patch.patch_number.value);
                            }
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                                show_patch_tooltip(patch);
                            }
                        }
                    } else {
                        ImGui::AlignTextToFramePadding();
                        ImGui::BeginDisabled(!patch_checked);
                        ImGui::TextUnformatted(patch_checked ? "ON" : "off");
                        ImGui::EndDisabled();
                    }

                    ImGui::HighlightTableRowOnHover();

                    ImGui::PopID();
                };

                const auto group_members = collect_patch_group_members();

                std::unordered_set<const patcher::PatchGroup*> rendered_groups;
                for (const auto patch_index : patcher::patches_sorted) {
                    auto& patch = patcher::patches[patch_index];
                    const auto *group = patcher::find_patch_group(patch);
                    if (!group) {
                        if (!patch_name_filter.empty()
                        && patch.name_in_lower_case.find(search_str_in_lower) == std::string::npos) {
                            continue;
                        }

                        const auto row_background_color =
                            get_patch_row_background_color(items_shown++);
                        render_patch(patch_index, row_background_color);
                        continue;
                    }

                    if (!rendered_groups.insert(group).second) {
                        continue;
                    }

                    const auto& members = group_members.at(group);
                    const auto search_match = get_patch_group_search_match(
                        *group,
                        members,
                        search_str_in_lower);
                    if (!search_match.visible()) {
                        continue;
                    }

                    ImGui::PushID(group);
                    const auto row_background_color =
                        get_patch_row_background_color(items_shown++);
                    const bool group_open = render_patch_group_parent(
                        *group,
                        members,
                        search_match.child_matches,
                        row_background_color);
                    if (group_open) {
                        for (size_t member_position = 0;
                             member_position < members.size();
                             member_position++) {
                            render_patch(
                                members[member_position],
                                row_background_color,
                                true,
                                member_position + 1 == members.size());
                        }
                    }
                    ImGui::PopID();
                }

                if (items_shown == 0) {
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
            ImGui::PopStyleVar(); // ImGuiStyleVar_CellPadding
        }
    }

    void PatchManager::update_sorted_patches() {
        // Rebuild/re-sort only when the sort order changes (SpecsDirty) or when
        // the patch list count changed - not every frame. reload_local_patches()
        // clears the cache to avoid dangling pointers when `patches` is rebuilt.
        auto *sort_specs = ImGui::TableGetSortSpecs();
        const bool patches_changed = patcher::patches_sorted.size() != patcher::patches.size();
        const bool status_sort_active = sort_specs
            && sort_specs->SpecsCount > 0
            && sort_specs->Specs[0].ColumnUserID == PATCH_COLUMN_STATUS;
        if (!patches_changed && !status_sort_active && !(sort_specs && sort_specs->SpecsDirty)) {
            return;
        }

        struct SortItem {
            std::vector<size_t> members;
            std::string name_in_lower_case;
            bool enabled;
        };

        PatchGroupMembers group_members;
        std::vector<const patcher::PatchGroup*> groups_by_patch(patcher::patches.size());
        for (size_t i = 0; i < patcher::patches.size(); i++) {
            const auto *group = patcher::find_patch_group(patcher::patches[i]);
            groups_by_patch[i] = group;
            if (group) {
                group_members[group].push_back(i);
            }
        }

        // build one sortable item per ungrouped patch or group, placing each
        // group at its first occurrence in the underlying file order
        std::vector<SortItem> sort_items;
        sort_items.reserve(patcher::patches.size());
        std::unordered_set<const patcher::PatchGroup*> emitted_groups;
        for (size_t i = 0; i < patcher::patches.size(); i++) {
            const auto& patch = patcher::patches[i];
            const auto *group = groups_by_patch[i];
            if (!group) {
                sort_items.push_back({
                    .members = {i},
                    .name_in_lower_case = patch.name_in_lower_case,
                    .enabled = patch.last_status == patcher::PatchStatus::Enabled,
                });
            } else if (emitted_groups.insert(group).second) {
                auto members = std::move(group_members.at(group));
                const bool group_enabled = get_patch_group_state(members).status
                    == PatchGroupStatus::Enabled;
                sort_items.push_back({
                    .members = std::move(members),
                    .name_in_lower_case = group->name_in_lower_case,
                    .enabled = group_enabled,
                });
            }
        }

        // no active sort specs means the default item order is retained
        if (sort_specs && sort_specs->SpecsCount > 0) {
            const auto &spec = sort_specs->Specs[0];
            const bool ascending = spec.SortDirection != ImGuiSortDirection_Descending;
            std::stable_sort(sort_items.begin(), sort_items.end(),
                [&](const SortItem& a, const SortItem& b) {
                    int cmp;
                    if (spec.ColumnUserID == PATCH_COLUMN_STATUS) {
                        // sort by displayed status (matches the checkbox), then
                        // by name as a tiebreaker
                        if (a.enabled != b.enabled) {
                            cmp = a.enabled ? -1 : 1;
                        } else {
                            cmp = a.name_in_lower_case.compare(b.name_in_lower_case);
                        }
                    } else {
                        cmp = a.name_in_lower_case.compare(b.name_in_lower_case);
                    }
                    return ascending ? (cmp < 0) : (cmp > 0);
                });
        }

        patcher::patches_sorted.clear();
        patcher::patches_sorted.reserve(patcher::patches.size());
        for (const auto& item : sort_items) {
            patcher::patches_sorted.insert(
                patcher::patches_sorted.end(),
                item.members.begin(),
                item.members.end());
        }

        if (sort_specs) {
            sort_specs->SpecsDirty = false;
        }
    }

    void PatchManager::show_patch_tooltip(const patcher::PatchData& patch) {
        if (!patch.caution.empty()) {
            ImGui::WarnTooltip(patch.description.c_str(), patch.caution.c_str());
        } else if (!patch.description.empty()) {
            ImGui::HelpTooltip(patch.description.c_str());
        }
    }
}
