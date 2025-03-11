#include "card.h"
#include <functional>
#include "external/rapidjson/document.h"
#include "util/logging.h"
#include "util/utils.h"
#include "misc/eamuse.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    Card::Card() : Module("card") {
        functions["insert"] = std::bind(&Card::insert, this, _1, _2);
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
