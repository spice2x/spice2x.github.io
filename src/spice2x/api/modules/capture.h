#pragma once

#include <optional>

#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    extern std::optional<uint32_t> CAPTURE_QUALITY;
    extern std::optional<uint32_t> CAPTURE_DIVIDE;

    class Capture : public Module {
    public:
        Capture();

    private:

        // function definitions
        void get_screens(Request &req, Response &res);
        void get_jpg(Request &req, Response &res);
    };
}
