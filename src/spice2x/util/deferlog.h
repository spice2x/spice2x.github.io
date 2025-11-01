#pragma once

#include <string>
#include <vector>
#include <initializer_list>

namespace deferredlogs {

    // some shared error messages
    extern const std::initializer_list<std::string> SUPERSTEP_SOUND_ERROR_MESSAGE;

    void defer_error_messages(std::initializer_list<std::string> messages);
    void dump_to_logger(bool is_crash=false);
    void show_popup_for_crash();
}
