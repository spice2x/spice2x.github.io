#include "request.h"
#include "../util/logging.h"
#include "module.h"

using namespace rapidjson;

namespace api {

    Request::Request(rapidjson::Document &document) {
        Value::MemberIterator it;
        this->parse_error = false;

        // get ID
        it = document.FindMember("id");
        if (it == document.MemberEnd() || !(*it).value.IsUint64()) {
            log_warning("api", "Request ID is invalid");
            this->parse_error = true;
            return;
        }
        this->id = (*it).value.GetUint64();

        // get module
        it = document.FindMember("module");
        if (it == document.MemberEnd() || !(*it).value.IsString()) {
            log_warning("api", "Request module is invalid");
            this->parse_error = true;
            return;
        }
        this->module = (*it).value.GetString();

        // get function
        it = document.FindMember("function");
        if (it == document.MemberEnd() || !(*it).value.IsString()) {
            log_warning("api", "Request function is invalid");
            this->parse_error = true;
            return;
        }
        this->function = (*it).value.GetString();

        // get params
        it = document.FindMember("params");
        if (it == document.MemberEnd() || !(*it).value.IsArray()) {
            log_warning("api", "Request params is invalid");
            this->parse_error = true;
            return;
        }
        this->params = document["params"];

        // log request
        if (LOGGING) {
            log_info("api", "new request > id: {}, module: {}, function: {}",
                    this->id, this->module, this->function);
        }
    }
}
