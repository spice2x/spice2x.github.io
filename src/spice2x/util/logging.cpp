#include "logging.h"

std::string_view log_get_datetime() {
    return log_get_datetime(time(nullptr));
}

std::string_view log_get_datetime(std::time_t now) {
    static thread_local char buf[64];

    // `localtime` on Windows is thread-safe
    strftime(buf, sizeof(buf), "[%Y/%m/%d %X]", localtime(&now));

    return buf;
}
