#include "patch_manager.h"

#include <algorithm>
#include <shellapi.h>
#include <thread>
#include "avs/game.h"
#include "cfg/configurator.h"
#include "games/io.h"
#include "launcher/launcher.h"
#include "misc/clipboard.h"
#include "overlay/imgui/extensions.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/nt_loader.h"
#include "util/utils.h"

namespace overlay::windows {
    using patcher::apply_patch;
    using patcher::displayPath;
    using patcher::get_game_identifier;
    using patcher::is_patch_active;
    using patcher::PatchData;
    using patcher::PatchStatus;
    using patcher::PatchType;

    // user-assigned IDs for the patches table columns, used by the sort logic
    enum PatchColumnId {
        PATCH_COLUMN_NAME = 0,
        PATCH_COLUMN_STATUS = 1,
    };

    std::string PatchManagerWindow::patch_name_filter("");

    PatchManagerWindow::PatchManagerWindow(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Patch Manager";
        this->flags |= ImGuiWindowFlags_AlwaysAutoResize;
        this->toggle_button = games::OverlayButtons::TogglePatchManager;
        this->init_pos = ImVec2(10, 10);
    }

    PatchManagerWindow::~PatchManagerWindow() = default;

    void PatchManagerWindow::build_content() {

        // check if initialized
        if (!local_patches_initialized) {
            if (fileutils::file_exists(config_path)) {
                this->config_load();
            }
            this->reload_local_patches();
        }

        if (avs::game::DLL_NAME == "kamunity.dll") {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Patches are not supported for Unity-based games.");
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Instead, look for downloads of pre-patched DLLs.");
            return;
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
        ImGui::Text("Modules Path: %s", displayPath(MODULE_PATH).c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::DummyMarker();
        ImGui::SameLine();
        if (ACTIVE_JSON_FILE.empty()) {
            ImGui::Text("Patches JSON: built-in");
        } else {
            ImGui::Text("Patches JSON: %s", ACTIVE_JSON_FILE.c_str());
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
        if (ImGui::Checkbox("Auto apply patches on game start", &setting_auto_apply)) {
            config_dirty = true;
        }

        // handle dirty state
        if (cfg::CONFIGURATOR_STANDALONE) {
            // auto save for configurator version
            if (config_dirty) {
                this->config_save();
            }
        } else {
            // manual save for live version: always render the row so the rest
            // of the panel doesn't shift when config_dirty flips. Disable the
            // button when there is nothing to save.
            ImGui::AlignTextToFramePadding();
            ImGui::HelpMarker("Save current patch state to the configuration file.");
            ImGui::SameLine();
            ImGui::BeginDisabled(!config_dirty);
            if (ImGui::Button("Save")) {
                this->config_save();
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

                if (ImGui::Button("Paste from clipboard")) {
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
                // import_remote_patches_to_disk clears out old patches, so regardless of result,
                // reload patches from disk
                reload_local_patches(true);
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
                ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.7);
                ImGui::TextUnformatted(url_fetch_errors.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
            if (!url_fetch_errors.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Copy Error")) {
                    clipboard::copy_text(url_fetch_errors);
                }
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

                // maintain a sorted view of patches so the underlying vector
                // order (used by config save and hashing) is left untouched
                update_sorted_patches();

                const auto search_str_in_lower = strtolower(patch_name_filter);
                size_t patches_shown = 0;
                for (auto patch_index : patches_sorted) {
                    auto &patch = patches[patch_index];

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
                    if (!patch.caution.empty()) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::WarnMarker(patch.description.c_str(), patch.caution.c_str());
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
                    if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                        show_patch_tooltip(patch);
                    }

                    // show range after label for integer patches
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
                    if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                        show_patch_tooltip(patch);
                    }

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
                                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                                    show_patch_tooltip(patch);
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
                                if (ImGui::IsItemHovered(ImGui::TOOLTIP_FLAGS)) {
                                    show_patch_tooltip(patch);
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
            ImGui::PopStyleVar(); // ImGuiStyleVar_CellPadding
        }
    }

    void PatchManagerWindow::update_sorted_patches() {
        // Rebuild/re-sort only when the sort order changes (SpecsDirty) or when
        // the patch list count changed - not every frame. reload_local_patches()
        // clears the cache to avoid dangling pointers when `patches` is rebuilt.
        auto *sort_specs = ImGui::TableGetSortSpecs();
        const bool patches_changed = patches_sorted.size() != patches.size();
        if (!patches_changed && !(sort_specs && sort_specs->SpecsDirty)) {
            return;
        }

        // rebuild the view in the underlying vector order (this is also the
        // default order shown when the sort is cleared / tristate "unsorted")
        patches_sorted.clear();
        patches_sorted.reserve(patches.size());
        for (size_t i = 0; i < patches.size(); i++) {
            patches_sorted.push_back(i);
        }

        // SpecsCount == 0 means no active sort: keep the default order
        if (sort_specs && sort_specs->SpecsCount > 0) {
            const auto &spec = sort_specs->Specs[0];
            const bool ascending = spec.SortDirection != ImGuiSortDirection_Descending;
            std::stable_sort(patches_sorted.begin(), patches_sorted.end(),
                [&](size_t ia, size_t ib) {
                    const PatchData *a = &patches[ia];
                    const PatchData *b = &patches[ib];
                    int cmp;
                    if (spec.ColumnUserID == PATCH_COLUMN_STATUS) {
                        // sort by displayed status (matches the checkbox), then
                        // by name as a tiebreaker
                        const bool a_on = a->last_status == PatchStatus::Enabled;
                        const bool b_on = b->last_status == PatchStatus::Enabled;
                        if (a_on != b_on) {
                            cmp = a_on ? -1 : 1;
                        } else {
                            cmp = a->name_in_lower_case.compare(b->name_in_lower_case);
                        }
                    } else {
                        cmp = a->name_in_lower_case.compare(b->name_in_lower_case);
                    }
                    return ascending ? (cmp < 0) : (cmp > 0);
                });
        }

        if (sort_specs) {
            sort_specs->SpecsDirty = false;
        }
    }

    void PatchManagerWindow::show_patch_tooltip(const patcher::PatchData& patch) {
        if (!patch.caution.empty()) {
            ImGui::WarnTooltip(patch.description.c_str(), patch.caution.c_str());
        } else if (!patch.description.empty()) {
            ImGui::HelpTooltip(patch.description.c_str());
        }
    }
}
