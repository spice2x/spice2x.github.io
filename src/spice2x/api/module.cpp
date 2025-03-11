#include <utility>

#include "util/logging.h"

#include "module.h"

using namespace rapidjson;

namespace api {

    // logging setting
    bool LOGGING = false;

    Module::Module(std::string name, bool password_force) {
        this->name = std::move(name);
        this->password_force = password_force;
    }

    void Module::handle(Request &req, Response &res) {

        // log module access
        if (LOGGING)
            log_info("api::" + this->name, "handling request");

        // find function
        auto pos = functions.find(req.function);
        if (pos == functions.end())
            return error_function_unknown(res);

        // call function
        pos->second(req, res);
    }

    void Module::error(Response &res, std::string err) {

        // log the warning
        log_warning("api::" + this->name, "error: {}", err);

        // add error to response
        Value val(err.c_str(), res.doc()->GetAllocator());
        res.add_error(val);
    }
}
