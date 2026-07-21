#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include "api/module.h"
#include "api/request.h"
#include "touch/touch.h"

namespace api::modules {

    class Touch : public Module {
    public:
        Touch();

    private:
        bool is_sdvx;
        bool is_tdj_fhd;
        bool use_native;
        std::mutex native_touch_mutex;
        std::optional<uint32_t> native_touch_id;

        // resolution the API/companion sends native touch coordinates in (after errata);
        // used to normalize before mapping into the native injection window
        int native_canvas_w;
        int native_canvas_h;

        // function definitions
        void read(Request &req, Response &res);
        void write(Request &req, Response &res);
        void write_reset(Request &req, Response &res);
        void write_native(const std::vector<TouchPoint> &touch_points);
        void write_reset_native(const std::vector<DWORD> &touch_point_ids);
        void apply_touch_errata(int &x, int &y);

    };
}
