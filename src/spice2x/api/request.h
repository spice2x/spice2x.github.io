#pragma once

#include <string>

#include <stdint.h>

#include "external/rapidjson/document.h"

namespace api {

    class Request {
    public:
        uint64_t id;
        std::string module;
        std::string function;
        rapidjson::Value params;
        bool parse_error;

        Request(rapidjson::Document &document);
    };
}
