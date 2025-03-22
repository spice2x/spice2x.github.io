#include "screen_resize.h"

#include "external/rapidjson/document.h"
#include "external/rapidjson/pointer.h"
#include "external/rapidjson/prettywriter.h"
#include "misc/eamuse.h"
#include "util/utils.h"
#include "util/fileutils.h"
#include "hooks/graphics/graphics.h"

namespace cfg {

    // globals
    std::unique_ptr<cfg::ScreenResize> SCREENRESIZE;
    std::optional<std::string> SCREEN_RESIZE_CFG_PATH_OVERRIDE;

    ScreenResize::ScreenResize() {
        if (SCREEN_RESIZE_CFG_PATH_OVERRIDE.has_value()) {
            this->config_path = SCREEN_RESIZE_CFG_PATH_OVERRIDE.value();
        } else {
            this->config_path = std::filesystem::path(_wgetenv(L"APPDATA")) / L"spicetools_screen_resize.json";
        }
        if (fileutils::file_exists(this->config_path)) {
            this->config_load();
        }
    }

    ScreenResize::~ScreenResize() {
    }
    
    void ScreenResize::config_load() {
        if (SCREEN_RESIZE_CFG_PATH_OVERRIDE.has_value()) {
            log_info("ScreenResize", "loading custom config: {}", this->config_path.string());
        } else {
            log_info("ScreenResize", "loading global config from APPDATA");
        }

        std::string config = fileutils::text_read(this->config_path);
        if (config.empty()) {
            log_info("ScreenResize", "config is empty");
            return;
        }

        // parse document
        rapidjson::Document doc;
        doc.Parse(config.c_str());

        // check parse error
        auto error = doc.GetParseError();
        if (error) {
            log_warning("ScreenResize", "config parse error: {}", error);
            return;
        }

        // verify root is a dict
        if (!doc.IsObject()) {
            log_warning("ScreenResize", "config not found");
            return;
        }

        bool use_game_setting = false;

        std::string root("/");
        // try to find game-specific setting, if one exists
        {
            const auto game = rapidjson::Pointer("/sp2x_games/" + eamuse_get_game()).Get(doc);
            if (game && game->IsObject()) {
                use_game_setting = true;
                root = "/sp2x_games/" + eamuse_get_game() + "/";
            }
        }

        log_misc(
            "ScreenResize",
            "Loading fullscreen image settings. Game = {}, is_global = {}, JSON path: {}",
            eamuse_get_game(),
            use_game_setting,
            root);
        
        load_bool_value(doc, root + "enable_screen_resize", this->enable_screen_resize);
        load_bool_value(doc, root + "enable_linear_filter", this->enable_linear_filter);
        for (size_t i = 0; i < std::size(this->scene_settings); i++) {
            auto& scene = this->scene_settings[i];
            std::string prefix = "";
            if (0 < i) {
                prefix += fmt::format("scenes/{}/", i-1);
            }
            load_int_value(doc, root + prefix + "offset_x", scene.offset_x);
            load_int_value(doc, root + prefix + "offset_y", scene.offset_y);
            load_float_value(doc, root + prefix + "scale_x", scene.scale_x);
            load_float_value(doc, root + prefix + "scale_y", scene.scale_y);
            load_bool_value(doc, root + prefix + "keep_aspect_ratio", scene.keep_aspect_ratio);
            load_bool_value(doc, root + prefix + "centered", scene.centered);
        }

        // windowed settings are always under game settings
        root = "/sp2x_games/" + eamuse_get_game() + "/";
        log_misc(
            "ScreenResize",
            "Loading window settings. Game = {}, JSON path: {}",
            eamuse_get_game(),
            root);
        load_bool_value(doc, root + "w_always_on_top", this->window_always_on_top);
        load_bool_value(doc, root + "w_enable_resize", this->enable_window_resize);
        load_bool_value(doc, root + "w_keep_aspect_ratio", this->client_keep_aspect_ratio);
        load_int_value(doc, root + "w_border_type", this->window_decoration);
        load_uint32_value(doc, root + "w_width", this->client_width);
        load_uint32_value(doc, root + "w_height", this->client_height);
        load_int_value(doc, root + "w_offset_x", this->window_offset_x);
        load_int_value(doc, root + "w_offset_y", this->window_offset_y);
    }

    bool ScreenResize::load_bool_value(rapidjson::Document& doc, std::string path, bool& value) {
        const auto v = rapidjson::Pointer(path).Get(doc);
        if (!v) {
            log_misc("ScreenResize", "{} not found", path);
            return false;
        }
        if (!v->IsBool()) {
            log_warning("ScreenResize", "{} is invalid type", path);
            return false;
        }
        value = v->GetBool();
        return true;
    }

    bool ScreenResize::load_int_value(rapidjson::Document& doc, std::string path, int& value) {
        const auto v = rapidjson::Pointer(path).Get(doc);
        if (!v) {
            log_misc("ScreenResize", "{} not found", path);
            return false;
        }
        if (!v->IsInt()) {
            log_warning("ScreenResize", "{} is invalid type", path);
            return false;
        }
        value = v->GetInt();
        return true;
    }

    bool ScreenResize::load_uint32_value(rapidjson::Document& doc, std::string path, uint32_t& value) {
        const auto v = rapidjson::Pointer(path).Get(doc);
        if (!v) {
            log_misc("ScreenResize", "{} not found", path);
            return false;
        }
        if (!v->IsUint()) {
            log_warning("ScreenResize", "{} is invalid type", path);
            return false;
        }
        value = v->GetUint();
        return true;
    }

    bool ScreenResize::load_float_value(rapidjson::Document& doc, std::string path, float& value) {
        const auto v = rapidjson::Pointer(path).Get(doc);
        if (!v) {
            log_misc("ScreenResize", "{} not found", path);
            return false;
        }
        if (v->IsInt()) {
            value = v->GetInt();
            return true;
        }
        if (v->IsDouble()) {
            value = v->GetDouble();
            return true;
        }
        if (v->IsFloat()) {
            value = v->GetFloat();
            return true;
        }
        return false;
    }

    void ScreenResize::config_save() {
        log_info("ScreenResize", "saving config: {}", this->config_path.string());

        rapidjson::Document doc;
        std::string config = fileutils::text_read(this->config_path);
        if (!config.empty()) {
            doc.Parse(config.c_str());
            log_misc("ScreenResize", "existing config file found");
        }
        if (!doc.IsObject()) {
            log_misc("ScreenResize", "clearing out config file");
            doc.SetObject();
        }

        // always save under per-game settings
        std::string root("/sp2x_games/" + eamuse_get_game() + "/");

        log_misc(
            "ScreenResize",
            "Game = {}, JSON path = {}",
            eamuse_get_game(),
            root);

        // full screen image settings
        rapidjson::Pointer(root + "enable_screen_resize").Set(doc, this->enable_screen_resize);
        rapidjson::Pointer(root + "enable_linear_filter").Set(doc, this->enable_linear_filter);
        for (size_t i = 0; i < std::size(this->scene_settings); i++) {
            auto& scene = this->scene_settings[i];
            std::string prefix = "";
            if (0 < i) {
                prefix += fmt::format("scenes/{}/", i-1);
            }
            rapidjson::Pointer(root + prefix + "offset_x").Set(doc, scene.offset_x);
            rapidjson::Pointer(root + prefix + "offset_y").Set(doc, scene.offset_y);
            rapidjson::Pointer(root + prefix + "scale_x").Set(doc, scene.scale_x);
            rapidjson::Pointer(root + prefix + "scale_y").Set(doc, scene.scale_y);
            rapidjson::Pointer(root + prefix + "keep_aspect_ratio").Set(doc, scene.keep_aspect_ratio);
            rapidjson::Pointer(root + prefix + "centered").Set(doc, scene.centered);
        }

        // windowed mode settings
        rapidjson::Pointer(root + "w_always_on_top").Set(doc, this->window_always_on_top);
        rapidjson::Pointer(root + "w_enable_resize").Set(doc, this->enable_window_resize);
        rapidjson::Pointer(root + "w_keep_aspect_ratio").Set(doc, this->client_keep_aspect_ratio);
        rapidjson::Pointer(root + "w_border_type").Set(doc, this->window_decoration);
        rapidjson::Pointer(root + "w_width").Set(doc, this->client_width);
        rapidjson::Pointer(root + "w_height").Set(doc, this->client_height);
        rapidjson::Pointer(root + "w_offset_x").Set(doc, this->window_offset_x);
        rapidjson::Pointer(root + "w_offset_y").Set(doc, this->window_offset_y);

        // build JSON
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        // save to file
        if (fileutils::text_write(this->config_path, buffer.GetString())) {
            // this->config_dirty = false;
        } else {
            log_warning("ScreenResize", "unable to save config file to {}", this->config_path.string());
        }
    }
}
