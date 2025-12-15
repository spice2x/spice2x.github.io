#pragma once

#include "games/game.h"
#include "touch/touch.h"

namespace games::drs {

    enum DRS_TOUCH_TYPE {
        DRS_DOWN = 0,
        DRS_UP   = 1,
        DRS_MOVE = 2,
    };

    typedef struct drs_touch {
        int type = DRS_UP;
        int id = 0;
        double x = 0.0;
        double y = 0.0;
        double width = 1;
        double height = 1;
    } drs_touch_t;

#define DRS_TAPELED_ROWS 49
#define DRS_TAPELED_COLS 38
#define DRS_TAPELED_COLS_TOTAL (DRS_TAPELED_ROWS * DRS_TAPELED_COLS)

// each r,g,b value is stored in a char but 215 is the highest you'll see from the game at 100% brightness in the test menu
#define DRS_TAPELED_MAX_VAL 215

    extern char DRS_TAPELED[DRS_TAPELED_COLS_TOTAL][3];
    extern std::vector<TouchEvent> TOUCH_EVENTS;
    extern bool DISABLE_TOUCH;
    extern bool TRANSPOSE_TOUCH;

    void fire_touches(drs_touch_t *events, size_t event_count);
    void start_touch();

    class DRSGame : public games::Game {
    public:
        DRSGame();
        virtual void attach() override;
        virtual void detach() override;
    };
}
