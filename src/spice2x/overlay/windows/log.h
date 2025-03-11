#pragma once

#include <mutex>
#include "overlay/window.h"
#include "launcher/logger.h"

namespace overlay::windows {

    class Log : public Window {
    private:

        std::vector<std::pair<std::string, logger::Style>> log_data;
        std::mutex log_data_m;
        ImGuiTextFilter filter;
        bool scroll_to_bottom = true;
        bool autoscroll = true;

        void clear();

    public:

        Log(SpiceOverlay *overlay);
        ~Log() override;

        void build_content() override;
        static bool log_hook(void *user, const std::string &data, logger::Style style, std::string &out);
    };
}
