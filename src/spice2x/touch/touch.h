#pragma once

#include <vector>
#include <windows.h>

struct TouchPoint {
    DWORD id;
    LONG x, y;
    bool mouse;
};
enum TouchEventType {
    TOUCH_DOWN,
    TOUCH_MOVE,
    TOUCH_UP
};
struct TouchEvent {
    DWORD id;
    LONG x, y;
    TouchEventType type;
    bool mouse;
};

extern bool SPICETOUCH_CARD_DISABLE;
extern HWND SPICETOUCH_TOUCH_HWND;
extern int SPICETOUCH_TOUCH_X;
extern int SPICETOUCH_TOUCH_Y;
extern int SPICETOUCH_TOUCH_WIDTH;
extern int SPICETOUCH_TOUCH_HEIGHT;

bool is_touch_available();

void touch_attach_wnd(HWND hWnd);
void touch_attach_dx_hook();
void touch_create_wnd(HWND hWnd, bool overlay = false);
void touch_detach();

void touch_write_points(std::vector<TouchPoint> *touch_points);
void touch_remove_points(std::vector<DWORD> *touch_point_ids);

void touch_get_points(std::vector<TouchPoint> &touch_points, bool overlay = false);
void touch_get_events(std::vector<TouchEvent> &touch_events, bool overlay = false);

void update_spicetouch_window_dimensions(HWND hWnd);
