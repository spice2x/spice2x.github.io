#include "internal.h"

#include <cstring>
#include "util/logging.h"
#include "util/utils.h"

namespace patcher {

    static std::pair<std::string, std::string> make_patch_group_key(
        const std::string& game_code,
        const std::string& group_id) {
        return {strtolower(game_code), group_id};
    }

    static bool has_embedded_null(const rapidjson::Value& value) {
        return strlen(value.GetString()) != value.GetStringLength();
    }

    bool is_patch_group_definition(const rapidjson::Value& patch) {
        if (!patch.IsObject()) {
            return false;
        }

        const auto type_it = patch.FindMember("type");
        return type_it != patch.MemberEnd()
            && type_it->value.IsString()
            && type_it->value.GetStringLength() == strlen("group")
            && !_stricmp(type_it->value.GetString(), "group");
    }

    std::map<std::pair<std::string, std::string>, PatchGroup> parse_patch_group_definitions(
        const rapidjson::Document& doc) {
        std::map<std::pair<std::string, std::string>, PatchGroup> groups;

        for (const auto& patch : doc.GetArray()) {
            if (!is_patch_group_definition(patch)) {
                continue;
            }

            const auto id_it = patch.FindMember("id");
            const auto game_code_it = patch.FindMember("gameCode");
            const auto name_it = patch.FindMember("name");
            if (id_it == patch.MemberEnd() || !id_it->value.IsString()
                || id_it->value.GetStringLength() == 0
                || has_embedded_null(id_it->value)
                || game_code_it == patch.MemberEnd() || !game_code_it->value.IsString()
                || game_code_it->value.GetStringLength() == 0
                || has_embedded_null(game_code_it->value)
                || name_it == patch.MemberEnd() || !name_it->value.IsString()
                || name_it->value.GetStringLength() == 0
                || has_embedded_null(name_it->value)) {
                log_warning("patchmanager", "invalid patch group definition");
                continue;
            }

            PatchGroup group;
            group.name.assign(name_it->value.GetString(), name_it->value.GetStringLength());
            group.name_in_lower_case = strtolower(group.name);

            const std::string group_id(
                id_it->value.GetString(),
                id_it->value.GetStringLength());

            const auto description_it = patch.FindMember("description");
            if (description_it != patch.MemberEnd()) {
                if (!description_it->value.IsString()
                    || has_embedded_null(description_it->value)) {
                    log_warning("patchmanager", "invalid description for patch group {}", group_id);
                    continue;
                }
                group.description.assign(
                    description_it->value.GetString(),
                    description_it->value.GetStringLength());
            }

            const auto caution_it = patch.FindMember("caution");
            if (caution_it != patch.MemberEnd()) {
                if (!caution_it->value.IsString() || has_embedded_null(caution_it->value)) {
                    log_warning("patchmanager", "invalid caution for patch group {}", group_id);
                    continue;
                }
                group.caution.assign(
                    caution_it->value.GetString(),
                    caution_it->value.GetStringLength());
            }

            const std::string game_code(
                game_code_it->value.GetString(),
                game_code_it->value.GetStringLength());
            if (!groups.emplace(
                    make_patch_group_key(game_code, group_id),
                    std::move(group)).second) {
                log_warning(
                    "patchmanager",
                    "duplicate patch group definition for {}/{}, ignoring duplicate",
                    game_code,
                    group_id);
            }
        }

        return groups;
    }

    static const PatchGroup* find_patch_group(
        const std::map<std::pair<std::string, std::string>, PatchGroup>& groups,
        const std::string& game_code,
        const std::string& group_id) {
        const auto group = groups.find(make_patch_group_key(game_code, group_id));
        return group == groups.end() ? nullptr : &group->second;
    }

    const PatchGroup* find_patch_group(const PatchData& patch) {
        return find_patch_group(patch_groups, patch.game_code, patch.group_id);
    }

    std::string resolve_patch_group_id(
        const rapidjson::Value& patch,
        const std::map<std::pair<std::string, std::string>, PatchGroup>& groups,
        const std::string& game_code,
        const char *patch_name) {
        const auto group_it = patch.FindMember("group");
        if (group_it == patch.MemberEnd()) {
            return "";
        }
        if (!group_it->value.IsString()
            || group_it->value.GetStringLength() == 0
            || has_embedded_null(group_it->value)) {
            log_warning("patchmanager", "invalid group reference for {}", patch_name);
            return "";
        }

        const std::string group_id(
            group_it->value.GetString(),
            group_it->value.GetStringLength());
        if (!find_patch_group(groups, game_code, group_id)) {
            log_warning(
                "patchmanager",
                "unknown patch group {}/{} referenced by {}",
                game_code,
                group_id,
                patch_name);
            return "";
        }

        return group_id;
    }

    void register_patch_group(
        PatchData& patch,
        const std::map<std::pair<std::string, std::string>, PatchGroup>& definitions) {
        if (patch.group_id.empty()) {
            return;
        }

        const auto *definition = find_patch_group(
            definitions,
            patch.game_code,
            patch.group_id);
        if (!definition) {
            patch.group_id.clear();
            return;
        }

        const auto key = make_patch_group_key(patch.game_code, patch.group_id);
        const auto [existing, inserted] = patch_groups.emplace(key, *definition);
        if (!inserted
            && (existing->second.name != definition->name
                || existing->second.description != definition->description
                || existing->second.caution != definition->caution)) {
            log_warning(
                "patchmanager",
                "conflicting group metadata for {}/{}, ignoring group on {}",
                patch.game_code,
                patch.group_id,
                patch.name);
            patch.group_id.clear();
        }
    }
}
