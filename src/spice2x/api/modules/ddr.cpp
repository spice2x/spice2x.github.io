#include "ddr.h"
#include <functional>
#include "external/rapidjson/document.h"
#include "games/ddr/ddr.h"

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    DDR::DDR() : Module("ddr") {
        functions["tapeled_get"] = std::bind(&DDR::tapeled_get, this, _1, _2);
    }

    /**
     * Allows fetching of the RGB LED strips that are gold cabinets, via SpiceAPI
     */
    void DDR::tapeled_get(Request &req, Response &res) {
        static const char* device_names[11] = {
            "p1_foot_up",
            "p1_foot_right",
            "p1_foot_left",
            "p1_foot_down",
            "p2_foot_up",
            "p2_foot_right",
            "p2_foot_left",
            "p2_foot_down",
            "top_panel",
            "monitor_left",
            "monitor_right"
        };

        Value response_object(kObjectType);

        // Iterate through each device and dump its lights data into the response
        for (size_t device = 0; device < 11; device++) {
            size_t num_leds = 25;
            if (device > 7)
                num_leds = 50;

            Value light_state(kArrayType);
            light_state.Reserve(num_leds * 3, res.doc()->GetAllocator());
            for (size_t led = 0; led < num_leds; led++) {
                light_state.PushBack(games::ddr::DDR_TAPELEDS[device][led][0], res.doc()->GetAllocator());
                light_state.PushBack(games::ddr::DDR_TAPELEDS[device][led][1], res.doc()->GetAllocator());
                light_state.PushBack(games::ddr::DDR_TAPELEDS[device][led][2], res.doc()->GetAllocator());
            }

            response_object.AddMember(StringRef(device_names[device]), light_state, res.doc()->GetAllocator());
        }

        res.add_data(response_object);
    }
}
