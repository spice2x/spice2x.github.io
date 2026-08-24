#include "card.h"
#include <fstream>
#include <functional>
#include "external/rapidjson/document.h"
#include "util/logging.h"
#include "util/utils.h"
#include "misc/eamuse.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    static bool normalize_card_id(const std::string &value, std::string &card_id) {
        if (value.size() != 16) {
            return false;
        }

        uint8_t card_bin[8] {};
        if (!hex2bin(value.c_str(), card_bin)) {
            return false;
        }

        card_id = bin2hex(card_bin, std::size(card_bin));
        return true;
    }

    static bool read_card_id(const std::filesystem::path &path, std::string &card_id) {
        std::ifstream file(path);
        char buffer[16] {};
        if (!file.read(buffer, std::size(buffer))) {
            return false;
        }

        return normalize_card_id(std::string(buffer, std::size(buffer)), card_id);
    }

    Card::Card() : Module("card") {
        functions["get_cards"] = std::bind(&Card::get_cards, this, _1, _2);
        functions["insert"] = std::bind(&Card::insert, this, _1, _2);
        require_password("get_cards");
    }

    /**
     * get_cards()
     */
    void Card::get_cards(Request &req, Response &res) {
        auto &alloc = res.doc()->GetAllocator();

        for (int index = 0; index < eamuse_get_game_keypads(); index++) {
            std::string card_id;
            std::string filename;
            const auto card_override = eamuse_get_card_override(index);
            const bool has_override = !card_override.empty();

            if (has_override) {
                if (!normalize_card_id(card_override, card_id)) {
                    continue;
                }
            } else {
                const auto path = eamuse_get_card_path(index);
                if (!read_card_id(path, card_id)) {
                    continue;
                }

                const auto filename_u8 = path.filename().u8string();
                filename.assign(filename_u8.begin(), filename_u8.end());
            }

            Value card(kObjectType);
            card.AddMember("index", index, alloc);
            card.AddMember("card_id", Value(card_id.c_str(), alloc), alloc);
            card.AddMember("source", Value(has_override ? "override" : "file", alloc), alloc);
            if (!has_override) {
                card.AddMember("file_name", Value(filename.c_str(), alloc), alloc);
            }
            res.add_data(card);
        }
    }

    /**
     * insert(index, card_id)
     * index: uint in range [0, 1]
     * card_id: hex string of length 16
     */
    void Card::insert(Request &req, Response &res) {

        // check params
        if (req.params.Size() < 2)
            return error_params_insufficient(res);
        if (!req.params[0].IsUint())
            return error_type(res, "index", "uint");
        if (!req.params[1].IsString())
            return error_type(res, "card_id", "hex string");
        if (req.params[1].GetStringLength() != 16)
            return error_size(res, "card_id", 16);

        // get params
        auto index = req.params[0].GetUint();
        auto card_hex = req.params[1].GetString();

        // convert to binary
        uint8_t card_bin[8] {};
        if (!hex2bin(card_hex, card_bin)) {
            return error_type(res, "card_id", "hex string");
        }

        // log
        if (LOGGING) {
            log_info("api::card", "inserting card: {}", card_hex);
        }

        // insert card
        eamuse_card_insert(index & 1, card_bin);
    }
}
