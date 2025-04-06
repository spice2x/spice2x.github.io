#include <random>
#include <games/io.h>
#include "card_manager.h"

#include "external/rapidjson/document.h"
#include "external/rapidjson/prettywriter.h"
#include "misc/eamuse.h"
#include "misc/clipboard.h"
#include "util/utils.h"
#include "util/fileutils.h"
#include "cfg/configurator.h"
#include "overlay/imgui/extensions.h"

using namespace rapidjson;


namespace overlay::windows {

    CardManager::CardManager(SpiceOverlay *overlay) : Window(overlay) {
        this->title = "Card Manager";
        this->init_size = ImVec2(420, 420);

        if (cfg::CONFIGURATOR_STANDALONE) {
            this->init_pos = ImVec2(40, 40);
        } else {
            this->init_pos = ImVec2(
                    ImGui::GetIO().DisplaySize.x / 2 - this->init_size.x / 2,
                    ImGui::GetIO().DisplaySize.y / 2 - this->init_size.y / 2);
        }

        this->toggle_button = games::OverlayButtons::ToggleCardManager;
        this->config_path = std::filesystem::path(_wgetenv(L"APPDATA")) / L"spicetools_card_manager.json";
        if (fileutils::file_exists(this->config_path)) {
            this->config_load();
        }

        // load -card0 / -card1
        // -card0 / -card1 override
        {
            std::lock_guard<std::mutex> lock(CARD_OVERRIDES_LOCK);
            if (!CARD_OVERRIDES[0].empty()) {
                const CardEntry card0 = {
                    .name = "P1 Default (-card0)",
                    .id = CARD_OVERRIDES[0],
                    .search_string = "p1 default (-card0)",
                    .read_only = true,
                    .color = {0.9f, 0.9f, 0.9f}
                };
                card_cmd_overrides[0].emplace(card0);
                this->loaded_card[0] = card0;
            }
            if (eamuse_get_game_keypads() > 1 && !CARD_OVERRIDES[1].empty()) {
                const CardEntry card1 = {
                    .name = "P2 Default (-card1)",
                    .id = CARD_OVERRIDES[1],
                    .search_string = "p2 default (-card1)",
                    .read_only = true,
                    .color = {0.9f, 0.9f, 0.9f}
                };
                card_cmd_overrides[1].emplace(card1);
                this->loaded_card[1] = card1;
            }
        }
    }

    CardManager::~CardManager() {
    }

    void CardManager::build_content() {
        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Active card overrides");
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Click to insert card now, or press Insert Card key. Auto Card Insert will also use these cards.\n\n"
            "If no override is set, pressing Insert Card will read from card0.txt / card1.txt.");
        if (ImGui::BeginTable("CardSetTable", eamuse_get_game_keypads() > 1 ? 2 : 1, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Player 1");
            if (eamuse_get_game_keypads() > 1) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Player 2");
            }
            ImGui::TableNextRow();
            for (size_t i = 0; i < 2; i++) {
                if (eamuse_get_game_keypads() > (int)i) {
                    ImGui::TableNextColumn();
                    if (build_card(i) && this->loaded_card[i].has_value()) {
                        insert_card_over_api(i, this->loaded_card[i].value());
                    }
                }
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "Available cards");
        build_card_list();
        ImGui::Separator();
        ImGui::Spacing();
        build_footer();

        build_card_editor();
    }

    bool CardManager::build_card(int reader) {
        ImGui::PushID(reader);
        bool clicked = false;
        if (this->loaded_card[reader].has_value()) {
            const auto &card = this->loaded_card[reader].value();
            const ImVec4 color(card.color[0], card.color[1], card.color[2], 1.f);
            float bg_luminance = (0.299f * card.color[0] + 0.587 * card.color[1] + 0.114 * card.color[2]);

            // text color
            ImVec4 text_color;
            if (0.5f < bg_luminance) {
                text_color = ImVec4(0.f, 0.f, 0.f, 1.f); // black
            } else {
                text_color = ImVec4(1.f, 1.f, 1.f, 1.f); // white
            }

            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
            ImGui::PushStyleColor(ImGuiCol_Text, text_color);
            if (ImGui::Button(fmt::format(
                    "  {}  \n  {} {} {} {}  ",
                    card.name.empty() ? "<blank>" : card.name.substr(0, 19),
                    card.id.substr(0, 4).c_str(),
                    card.id.substr(4, 4).c_str(),
                    card.id.substr(8, 4).c_str(),
                    card.id.substr(12, 4).c_str()
                    ).c_str())) {

                clicked = true;
            }
            ImGui::PopStyleColor(4);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("   (No override set)   \n"
                          "  xxxx xxxx xxxx xxxx  ");
            ImGui::EndDisabled();
        }
        
        ImGui::PopID();
        return clicked;
    }

    void CardManager::open_card_editor() {
        if (this->current_card) {
            const auto card = this->current_card;
            strcpy_s(this->name_buffer, std::size(this->name_buffer), card->name.c_str());
            strcpy_s(this->card_buffer, std::size(this->card_buffer), card->id.c_str());
            this->color_buffer[0] = card->color[0];
            this->color_buffer[1] = card->color[1];
            this->color_buffer[2] = card->color[2];
            ImGui::OpenPopup("Card Editor");
        }
    }

    void CardManager::build_card_editor() {
        // new/edit card popup
        if (ImGui::BeginPopupModal("Card Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            // card ID field (only editable for new cards)
            ImGui::BeginDisabled(this->current_card);
            ImGui::InputTextWithHint("Card ID", "E0040123456789AB",
                    this->card_buffer,
                    std::size(this->card_buffer),
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
            ImGui::EndDisabled();
            if (this->current_card == nullptr) {
                ImGui::SameLine();
                if (ImGui::Button("Generate")) {
                    generate_ea_card(this->card_buffer);
                }
            } else {
                ImGui::SameLine();
                if (ImGui::Button("Copy")) {
                    clipboard::copy_text(this->current_card->id);
                }
            }

            // name field
            ImGui::InputTextWithHint("Card Name", "Main Card",
                    this->name_buffer, std::size(this->name_buffer));

            // color
            ImGui::ColorEdit3("Color", this->color_buffer, ImGuiColorEditFlags_DisplayHex);
            ImGui::SameLine();
            if (ImGui::Button("Random")) {
                generate_random_color();
            }

            ImGui::Separator();

            // add/update button
            ImGui::BeginDisabled(strlen(this->card_buffer) != 16);
            if (ImGui::Button(this->current_card ? "Update Card" : "Save Card")) {
                if (this->current_card) {
                    // update existing card
                    this->current_card->name = strtrim(this->name_buffer);
                    this->current_card->color[0] = this->color_buffer[0];
                    this->current_card->color[1] = this->color_buffer[1];
                    this->current_card->color[2] = this->color_buffer[2];
                    generate_search_string(this->current_card);

                    // ensure loaded cards are kept up to date
                    // note: does not handle cases where multiple cards have the same ID
                    for (size_t i = 0; i < 2; i++) {
                        if (this->loaded_card[i].has_value() &&
                            !this->loaded_card[i].value().read_only &&
                            this->loaded_card[i].value().id == this->current_card->id) {
                            this->loaded_card[i] = *this->current_card;
                            break;
                        }
                    }

                } else {
                    // create a new card
                    CardEntry card {
                        .name = strtrim(this->name_buffer),
                        .id = std::string(this->card_buffer),
                        .color = {this->color_buffer[0], this->color_buffer[1], this->color_buffer[2]}
                    };
                    generate_search_string(&card);
                    this->cards.emplace_back(card);
                    
                    // mark this card as the selected one
                    this->current_card = &this->cards.back();
                }
                this->config_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            // delete current card button
            if (this->current_card) {
                ImGui::SameLine();
                if (ImGui::Button("Delete Card")) {
                    std::erase_if(this->cards, [&](CardEntry &card) {
                        return &card == this->current_card;
                    });
                    this->current_card = nullptr;
                    this->config_save();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void CardManager::build_card_selectable(CardEntry &card) {
        // generate card name
        std::string card_name = "";
        if (card.id.length() == 16) {
            card_name += card.id.substr(0, 4);
            card_name += " ";
            card_name += card.id.substr(4, 4);
            card_name += " ";
            card_name += card.id.substr(8, 4);
            card_name += " ";
            card_name += card.id.substr(12, 4);
        } else {
            card_name += card.id;
        }
        if (!card.name.empty()) {
            card_name += " - ";
            card_name += card.name;
        }

        ImGui::PushID(&card);

        // color button
        ImVec4 color(card.color[0], card.color[1], card.color[2], 1.f);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
        ImGui::SmallButton(" ");
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // selectable item
        if (ImGui::Selectable(card_name.c_str(), this->current_card == &card)) {
            this->current_card = &card;
        }

        ImGui::PopID();
    }

    void CardManager::build_card_list() {

        // search for card
        //
        // setting ImGuiInputTextFlags_CallbackCharFilter and pressing escape doesn't cause below 
        // to return true, making it necessary to provide a callback...
        ImGui::SetNextItemWidth(240);
        if (ImGui::InputTextWithHint("", "Type here to search..", &this->search_filter)) {
            this->current_card = nullptr;
            this->search_filter_in_lower_case = strtolower(this->search_filter);
        }
        if (!this->search_filter.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                this->search_filter.clear();
                this->search_filter_in_lower_case.clear();
            }
        }

        // toolbar

        // set card as p1/p2
        for (size_t i = 0; i < 2; i++) {
            if (eamuse_get_game_keypads() > (int)i) {
                if (i != 0) {
                    ImGui::SameLine();
                }
                ImGui::PushID(i);
                ImGui::BeginDisabled(this->current_card == nullptr);
                if (ImGui::Button(i == 0 ? "Load P1" : "Load P2") && this->current_card) {
                    this->loaded_card[i] = *this->current_card;
                    log_info(
                        "cardmanager",
                        "[P{}] update override and insert card: {} ({})",
                        i+1,
                        this->current_card->id,
                        this->current_card->name
                    );

                    // update override
                    std::lock_guard<std::mutex> lock(CARD_OVERRIDES_LOCK);
                    CARD_OVERRIDES[i] = this->current_card->id;

                    // insert card over api
                    insert_card_over_api(i, this->loaded_card[i].value());
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }

        // edit selected card
        ImGui::SameLine();
        ImGui::BeginDisabled(this->current_card == nullptr || this->current_card->read_only);
        if (ImGui::Button("Edit")) {
            open_card_editor();
        }

        // move selected up/down the list
        if (this->search_filter.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Move Up")) {
                for (auto it = this->cards.begin(); it != this->cards.end(); ++it) {
                    if (&*it == this->current_card && it != this->cards.begin()) {
                        std::iter_swap(it, it - 1);
                        this->current_card = &*(it - 1);
                        this->config_dirty = true;
                        break;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Move Down")) {
                for (auto it = this->cards.begin(); it != this->cards.end(); ++it) {
                    if (&*it == this->current_card && (it + 1) != this->cards.end()) {
                        std::iter_swap(it, it + 1);
                        this->current_card = &*(it + 1);
                        this->config_dirty = true;
                        break;
                    }
                }
            }
        }
        ImGui::EndDisabled();

        ImGui::Spacing();

        // cards list
        // use all available vertical space, minus height footer (a row of buttons and separator)
        if (ImGui::BeginChild(
                "cards",
                ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 8.f))) {

            // -card0 / -card1 override
            for (size_t i = 0; i < 2; i++) {
                if (card_cmd_overrides[i].has_value()) {
                    build_card_selectable(card_cmd_overrides[i].value());
                }
            }

            // cards from card manager JSON
            for (auto &card : this->cards) {
                
                if (!this->search_filter_in_lower_case.empty() && !card.search_string.empty()) {
                    const bool matched =
                    card.search_string.find(this->search_filter_in_lower_case) != std::string::npos;
                    
                    if (!matched) {
                        continue;
                    }
                }

                build_card_selectable(card);
            }
        }
        ImGui::EndChild();
    }

    void CardManager::build_footer() {

        // add new card
        if (ImGui::Button("Add New Card")) {
            memset(this->name_buffer, 0, sizeof(this->name_buffer));
            memset(this->card_buffer, 0, sizeof(this->card_buffer));
            generate_random_color();
            this->current_card = nullptr;
            ImGui::OpenPopup("Card Editor");
        }

        // save button
        if (this->config_dirty) {
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                this->config_save();
            }
        }
    }

    void CardManager::config_load() {
        log_info("cardmanager", "loading config");

        // clear cards
        this->cards.clear();

        // read config file
        std::string config = fileutils::text_read(this->config_path);
        if (!config.empty()) {

            // parse document
            Document doc;
            doc.Parse(config.c_str());

            // check parse error
            auto error = doc.GetParseError();
            if (error) {
                log_warning("cardmanager", "config parse error: {}", error);
            }

            // verify root is a dict
            if (doc.IsObject()) {

                // find pages
                auto pages = doc.FindMember("pages");
                if (pages != doc.MemberEnd() && pages->value.IsArray()) {

                    // iterate pages
                    for (auto &page : pages->value.GetArray()) {
                        if (page.IsObject()) {

                            // get cards
                            auto cards = page.FindMember("cards");
                            if (cards != doc.MemberEnd() && cards->value.IsArray()) {

                                // iterate cards
                                for (auto &card : cards->value.GetArray()) {
                                    if (card.IsObject()) {

                                        // find attributes
                                        auto name = card.FindMember("name");
                                        if (name == doc.MemberEnd() || !name->value.IsString()) {
                                            log_warning("cardmanager", "card name not found");
                                            continue;
                                        }
                                        auto id = card.FindMember("id");
                                        if (id == doc.MemberEnd() || !id->value.IsString()) {
                                            log_warning("cardmanager", "card id not found");
                                            continue;
                                        }

                                        // save entry
                                        CardEntry entry {
                                                .name = name->value.GetString(),
                                                .id = id->value.GetString(),
                                        };
                                        generate_search_string(&entry);

                                        // optional color
                                        auto color = card.FindMember("color");
                                        if (color != doc.MemberEnd() && color->value.IsArray()) {
                                            auto c = color->value.GetArray();
                                            if (c.Size() == 3 && c[0].IsFloat()) {
                                                entry.color[0] = c[0].GetFloat();
                                                entry.color[1] = c[1].GetFloat();
                                                entry.color[2] = c[2].GetFloat();
                                            }
                                        }

                                        this->cards.emplace_back(entry);

                                    } else {
                                        log_warning("cardmanager", "card is not an object");
                                    }
                                }
                            } else {
                                log_warning("cardmanager", "cards not found or not an array");
                            }
                        } else {
                            log_warning("cardmanager", "page is not an object");
                        }
                    }
                } else {
                    log_warning("cardmanager", "pages not found or not an array");
                }
            }
        }
    }

    void CardManager::config_save() {
        log_info("cardmanager", "saving config");

        // create document
        Document doc;
        doc.Parse(
                "{"
                "  \"pages\": ["
                "    {"
                "      \"cards\": ["
                "      ]"
                "    }"
                "  ]"
                "}"
        );

        // check parse error
        auto error = doc.GetParseError();
        if (error) {
            log_warning("cardmanager", "template parse error: {}", error);
        }

        // add cards
        auto &cards = doc["pages"][0]["cards"];
        for (auto &entry : this->cards) {
            Value card(kObjectType);
            card.AddMember("name", StringRef(entry.name.c_str()), doc.GetAllocator());
            card.AddMember("id", StringRef(entry.id.c_str()), doc.GetAllocator());

            Value color(kArrayType);
            color.PushBack(entry.color[0], doc.GetAllocator());
            color.PushBack(entry.color[1], doc.GetAllocator());
            color.PushBack(entry.color[2], doc.GetAllocator());
            card.AddMember("color", color, doc.GetAllocator());

            cards.PushBack(card, doc.GetAllocator());
        }

        // build JSON; using pretty writer so people can manually edit it
        StringBuffer buffer;
        PrettyWriter<StringBuffer> writer(buffer);
        doc.Accept(writer);

        // save to file
        if (fileutils::text_write(this->config_path, buffer.GetString())) {
            this->config_dirty = false;
        } else {
            log_warning("cardmanager", "unable to save config file to {}", this->config_path.string());
        }
    }

    void CardManager::generate_search_string(CardEntry *card) {
        card->search_string = strtolower(card->name) + " " + strtolower(card->id);
    }

    void CardManager::generate_random_color() {
        // these are colors on a hue wheel, starting from red
        static const char colors[48][7] = {
            "FF0000","FF2000","FF4000","FF6000","FF8000","FFAA00","FFCC00","FFEE00",
            "FFFF00","DDFF00","CCFF00","AAFF00","80FF00","60FF00","40FF00","20FF00",
            "00FF00","00FF20","00FF40","00FF60","00FF80","00FFAA","00FFCC","00FFDD",
            "00FFFF","00DDFF","00CCFF","0099FF","0080FF","0060FF","0040FF","0020FF",
            "0000FF","2000FF","4000FF","6000FF","8000FF","AA00FF","CC00FF","DD00FF",
            "FF00FF","FF00EE","FF00CC","FF00AA","FF0080","FF0060","FF0040","FF0020"
            };

        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> uniform(12, 36);

        // skip, ignoring half the hue wheel close to current index
        static int index = 0;
        index = (index + uniform(generator)) % 48;

        const auto hex = colors[index];

        uint8_t r, g, b;
        std::sscanf(hex, "%02hhx%02hhx%02hhx", &r, &g, &b);
        this->color_buffer[0] = r / 255.f;
        this->color_buffer[1] = g / 255.f;
        this->color_buffer[2] = b / 255.f;
    }

    void CardManager::insert_card_over_api(int reader, CardEntry &card) {
        uint8_t card_bin[8];
        if (card.id.length() == 16 && hex2bin(card.id.c_str(), card_bin)) {
            eamuse_card_insert(reader, card_bin);
        }
    }
}
