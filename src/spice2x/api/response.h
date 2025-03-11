#pragma once

#include "external/rapidjson/document.h"

namespace api {

    class Response {
    private:
        rapidjson::Document document;
        rapidjson::Value errors;
        rapidjson::Value data;

    public:
        std::string password;
        bool password_changed = false;

        Response(uint64_t id);

        template <class T> void add_error(T& error) {
            this->errors.PushBack(error, document.GetAllocator());
        };
        template <class T> void add_data(T& data) {
            this->data.PushBack(data, document.GetAllocator());
        }

        std::string get_string(bool pretty=false);

        inline rapidjson::Document* doc() {
            return &document;
        }

        inline void password_change(std::string password) {
            this->password = password;
            this->password_changed = true;
        }
    };
}
