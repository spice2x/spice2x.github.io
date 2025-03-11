#pragma once

#if SPICE64

#include <vector>
#include "games/iidx/local_camera.h"

namespace games::iidx {
    extern std::vector<IIDXLocalCamera*> LOCAL_CAMERA_LIST;
    extern bool CAMERA_READY;

    bool init_local_camera();
    bool init_camera_hooks();
    void camera_release();

    bool camera_config_load();
    bool camera_config_save();
    bool camera_config_reset();

    IIDXLocalCamera* add_top_camera(IMFActivate* pActivate);
    IIDXLocalCamera* add_front_camera(IMFActivate* pActivate);
}

#endif
