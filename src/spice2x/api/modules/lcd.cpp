#include "lcd.h"
#include "external/rapidjson/document.h"
#include "games/shared/lcdhandle.h"

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    LCD::LCD() : Module("lcd") {
        functions["info"] = std::bind(&LCD::info, this, _1, _2);
    }

    /*
     * info()
     */
    void LCD::info(Request &req, Response &res) {

        // get allocator
        auto &alloc = res.doc()->GetAllocator();

        // build info object
        Value info(kObjectType);
        info.AddMember("enabled", games::shared::LCD_ENABLED, alloc);
        info.AddMember("csm", StringRef(games::shared::LCD_CSM.c_str()), alloc);
        info.AddMember("bri", games::shared::LCD_BRI, alloc);
        info.AddMember("con", games::shared::LCD_CON, alloc);
        info.AddMember("bl", games::shared::LCD_BL, alloc);
        info.AddMember("red", games::shared::LCD_RED, alloc);
        info.AddMember("green", games::shared::LCD_GREEN, alloc);
        info.AddMember("blue", games::shared::LCD_BLUE, alloc);

        // add info object
        res.add_data(info);
    }
}
