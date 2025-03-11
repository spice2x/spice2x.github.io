#include "coin.h"
#include <functional>
#include "external/rapidjson/document.h"
#include "misc/eamuse.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    Coin::Coin() : Module("coin") {
        functions["get"] = std::bind(&Coin::get, this, _1, _2);
        functions["set"] = std::bind(&Coin::set, this, _1, _2);
        functions["insert"] = std::bind(&Coin::insert, this, _1, _2);
        functions["blocker_get"] = std::bind(&Coin::blocker_get, this, _1, _2);
    }

    /**
     * get()
     */
    void Coin::get(api::Request &req, api::Response &res) {

        // get coin stock
        auto coin_stock = eamuse_coin_get_stock();

        // insert value
        Value coin_stock_val(coin_stock);
        res.add_data(coin_stock_val);
    }

    /**
     * set(amount: int)
     */
    void Coin::set(api::Request &req, api::Response &res) {

        // check params
        if (req.params.Size() < 1)
            return error_params_insufficient(res);
        if (!req.params[0].IsInt())
            return error_type(res, "amount", "int");

        // set coin stock
        eamuse_coin_set_stock(req.params[0].GetInt());
    }

    /**
     * insert()
     * insert(amount: int)
     */
    void Coin::insert(api::Request &req, api::Response &res) {

        // insert()
        if (req.params.Size() == 0) {
            eamuse_coin_add();
            return;
        }

        // check params
        if (!req.params[0].IsInt())
            return error_type(res, "amount", "int");

        // add to coin stock
        eamuse_coin_set_stock(eamuse_coin_get_stock() + std::max(0, req.params[0].GetInt()));
    }

    /*
     * blocker_get()
     */
    void Coin::blocker_get(api::Request &req, api::Response &res) {

        // get block status
        auto block_status = eamuse_coin_get_block();

        // insert value
        Value block_val(block_status);
        res.add_data(block_val);
    }
}
