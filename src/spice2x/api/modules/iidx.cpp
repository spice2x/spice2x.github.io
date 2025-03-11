#include "iidx.h"
#include <functional>
#include <vector>
#include "games/iidx/iidx.h"
#include "external/rapidjson/document.h"

using namespace std::placeholders;
using namespace rapidjson;


namespace api::modules {

    // settings
    static const size_t TICKER_SIZE = 9;

    IIDX::IIDX() : Module("iidx") {
        functions["ticker_get"] = std::bind(&IIDX::ticker_get, this, _1, _2);
        functions["ticker_set"] = std::bind(&IIDX::ticker_set, this, _1, _2);
        functions["ticker_reset"] = std::bind(&IIDX::ticker_reset, this, _1, _2);
    }

    /**
     * ticker_get()
     */
    void IIDX::ticker_get(api::Request &req, Response &res) {

        // get led ticker
        games::iidx::IIDX_LED_TICKER_LOCK.lock();
        Value led_ticker(StringRef(games::iidx::IIDXIO_LED_TICKER, TICKER_SIZE), res.doc()->GetAllocator());
        games::iidx::IIDX_LED_TICKER_LOCK.unlock();

        // add to response
        res.add_data(led_ticker);
    }

    /**
     * ticker_set(text: str)
     */
    void IIDX::ticker_set(api::Request &req, api::Response &res) {

        // check param
        if (req.params.Size() < 1)
            return error_params_insufficient(res);
        if (!req.params[0].IsString())
            return error_type(res, "text", "str");

        // get param
        auto text = req.params[0].GetString();
        auto text_len = req.params[0].GetStringLength();

        // lock
        std::lock_guard<std::mutex> ticker_lock(games::iidx::IIDX_LED_TICKER_LOCK);

        // set to read only
        games::iidx::IIDXIO_LED_TICKER_READONLY = true;

        // set led ticker
        memset(games::iidx::IIDXIO_LED_TICKER, ' ', TICKER_SIZE);
        for (size_t i = 0; i < TICKER_SIZE && i < text_len; i++) {
            games::iidx::IIDXIO_LED_TICKER[i] = text[i];
        }
    }

    void IIDX::ticker_reset(api::Request &req, api::Response &res) {

        // lock
        std::lock_guard<std::mutex> ticker_lock(games::iidx::IIDX_LED_TICKER_LOCK);

        // disable read only
        games::iidx::IIDXIO_LED_TICKER_READONLY = false;
    }
}
