#pragma once

#include <functional>
#include <map>
#include <string>
#include <sstream>
#include <external/robin_hood.h>

#include "response.h"
#include "request.h"

namespace api {

    // logging setting
    extern bool LOGGING;

    // callback
    typedef std::function<void(Request &, Response &)> ModuleFunctionCallback;

    class Module {
    protected:

        // map of available functions
        robin_hood::unordered_map<std::string, ModuleFunctionCallback> functions;

        // default constructor
        explicit Module(std::string name, bool password_force=false);

    public:

        // virtual deconstructor
        virtual ~Module() = default;

        // name of the module (should match namespace)
        std::string name;
        bool password_force;

        // the magic
        void handle(Request &req, Response &res);

        /*
         * Error definitions.
         */

        void error(Response &res, std::string err);
        void error_type(Response &res, const std::string &field, const std::string &type) {
            std::ostringstream s;
            s << field << " must be a " << type;
            error(res, s.str());
        };
        void error_size(Response &res, const std::string &field, size_t size) {
            std::ostringstream s;
            s << field << " must be of size " << size;
            error(res, s.str());
        }
        void error_unknown(Response &res, const std::string &field, const std::string &name) {
            std::ostringstream s;
            s << "Unknown " << field << ": " << name;
            error(res, s.str());
        }

#define ERR(name, err) void error_##name(Response &res) { error(res, err); }
        ERR(function_unknown, "Unknown function.");
        ERR(params_insufficient, "Insufficient number of parameters.");
#undef ERR
    };
}
