#pragma once

#include <filesystem>
#include <optional>

#include "overlay/window.h"

namespace overlay::windows {

    struct CardEntry {
        std::string name = "unnamed";
        std::string id = "E004010000000000";
        std::string search_string = "";
        bool read_only = false;
        float color[3] {};
    };

    class CardManager : public Window {
    public:

        CardManager(SpiceOverlay *overlay);
        ~CardManager() override;

        void build_content() override;

    private:

        std::filesystem::path config_path;
        bool config_dirty = false;
        std::vector<CardEntry> cards;
        char name_buffer[65] {};
        char card_buffer[17] {};
        float color_buffer[3] {};
        
        std::optional<CardEntry> card_cmd_overrides[2];

        std::optional<CardEntry> loaded_card[2];
        CardEntry *current_card = nullptr;

        std::string search_filter = "";
        std::string search_filter_in_lower_case = "";

        void config_load();
        void config_save();

        void generate_search_string(CardEntry *card);
        void generate_random_color();

        bool build_card(int reader);
        void open_card_editor();
        void build_card_editor();
        void build_card_list();
        void build_card_selectable(CardEntry &card);
        void build_footer();

        void insert_card_over_api(int reader, CardEntry &card);
    };
}
