#pragma once

#include <string>
#include <vector>
#include <initializer_list>

namespace deferredlogs {

    // some shared error messages
    extern const std::initializer_list<std::string> SUPERSTEP_SOUND_ERROR_MESSAGE;

    void set_softid(const std::string& softid);
    void defer_error_messages(std::initializer_list<std::string> messages);
    void dump_to_logger(bool is_crash=false);
}
