#include "external/rapidjson/document.h"
#include "external/rapidjson/writer.h"
#include "external/rapidjson/prettywriter.h"

#include "util/logging.h"

#include "response.h"

using namespace api;

Response::Response(uint64_t id) {

    // load template
    document.Parse(
            "{"
            "\"id\": -1,"
            "\"errors\": [],"
            "\"data\": []"
            "}"
    );

    // check for error
    auto error = document.GetParseError();
    if (error)
        log_warning("api", "response template parse error: {}", error);

    // set ID
    document["id"].SetUint64(id);

    // get fields
    this->errors = document["errors"];
    this->data = document["data"];
}

std::string Response::get_string(bool pretty) {

    // apply errors and data
    this->document["errors"] = this->errors;
    this->document["data"] = this->data;

    // generate string
    rapidjson::StringBuffer sb;
    if (pretty) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
        this->document.Accept(writer);
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        this->document.Accept(writer);
    }
    return std::string(sb.GetString());
}
