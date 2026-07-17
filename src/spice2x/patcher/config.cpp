#include "internal.h"

#include <algorithm>
#include "avs/game.h"
#include "cfg/configurator.h"
#include "external/hash-library/sha256.h"
#include "external/rapidjson/document.h"
#include "external/rapidjson/prettywriter.h"
#include "external/rapidjson/stringbuffer.h"
#include "util/fileutils.h"
#include "util/logging.h"

using namespace rapidjson;

namespace patcher {

    bool is_game_id_wildcard_matched(const std::string& id_from_config) {
        return ((id_from_config.compare(0, 3, avs::game::MODEL) == 0) &&
                (id_from_config.compare(10, 10, avs::game::EXT) == 0));
    }

    void config_load() {
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
                log_warning("patchmanager", "config file parse error: {}", static_cast<uint32_t>(error));
            }

            // verify root is a dict
            if (doc.IsObject()) {

                // read auto apply settings
                auto auto_apply = doc.FindMember("auto_apply");
                if (auto_apply != doc.MemberEnd() && auto_apply->value.IsArray()) {

                    // get game id
                    const auto game_id = avs::game::get_identifier();

                    // iterate entries
                    setting_auto_apply = false;
                    setting_auto_apply_list.clear();
                    for (auto &entry : auto_apply->value.GetArray()) {
                        if (entry.IsString()) {

                            // check if this is our game identifier
                            const std::string entry_id = entry.GetString();

                            if (!setting_auto_apply) {
                                if (game_id == entry_id) {
                                    // exact match
                                    setting_auto_apply = true;
                                    log_misc(
                                        "patchmanager",
                                        "matched auto apply entry by full game identifier: {}",
                                        entry_id);

                                } else if (is_game_id_wildcard_matched(entry_id)) {
                                    // match on model and ext, ignoring dest/spec/rev
                                    // sample: LDJ:J:E:A:2025011400
                                    setting_auto_apply = true;
                                    log_misc(
                                        "patchmanager",
                                        "matched auto apply entry by partial game identifier: {}:?:?:?:{}",
                                        avs::game::MODEL, avs::game::EXT);
                                }
                            }

                            // move to list
                            setting_auto_apply_list.emplace_back(entry_id);
                        }
                    }
                    if (!setting_auto_apply) {
                        log_misc(
                            "patchmanager",
                            "no auto apply entry matched, patches will not load automatically");
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

    std::string patch_hash(PatchData &patch) {
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

    void config_save() {

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
            log_warning("patchmanager", "template parse error: {}", static_cast<uint32_t>(error));
        }

        // auto apply setting
        auto &auto_apply_list = doc["auto_apply"];
        auto game_id = avs::game::get_identifier();
        bool game_id_added = false;
        for (auto &entry : setting_auto_apply_list) {
            if (entry == game_id || is_game_id_wildcard_matched(entry)) {
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
        if (fileutils::write_config_file("patchmanager", config_path, buffer.GetString())) {
            config_dirty = false;
        } else {
            log_warning("patchmanager", "unable to save config file");
        }
    }
}
