#pragma once

#include <vector>
#include "api/module.h"
#include "api/request.h"

namespace api::modules {

    class Touch : public Module {
    public:
        Touch();

    private:
        bool is_sdvx;
        bool is_tdj_fhd;
        bool use_native;

        // resolution the API/companion sends native touch coordinates in (after errata);
        // used to normalize before mapping into the spicetouch touch surface
        int native_canvas_w;
        int native_canvas_h;

        // function definitions
        void read(Request &req, Response &res);
        void write(Request &req, Response &res);
        void write_reset(Request &req, Response &res);
        void apply_touch_errata(int &x, int &y);

    };
}
