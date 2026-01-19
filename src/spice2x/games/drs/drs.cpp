#include "drs.h"

#include <windows.h>
#include <thread>

#include "avs/game.h"
#include "games/game.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/memutils.h"
#include "rgb_cam.h"

#pragma pack(push)

typedef struct {
    union {
        struct {
            WORD unk1;
            WORD unk2;
            WORD device_id;
            WORD vid;
            WORD pid;
            WORD pvn;
            WORD max_point_num;
        };
        uint8_t raw[2356];
    };
} dev_info_t;

typedef struct {
    DWORD cid;
    DWORD type;
    DWORD unused;
    DWORD y;
    DWORD x;
    DWORD height;
    DWORD width;
    DWORD unk8;
} touch_data_t;

#pragma pack(pop)

enum touch_type {
    TS_DOWN = 1,
    TS_MOVE = 2,
    TS_UP = 3,
};

void *user_data = nullptr;
void (*touch_callback)(
        dev_info_t *dev_info,
        const touch_data_t *touch_data,
        int touch_points,
        int unk1,
        const void *user_data);

namespace games::drs {

	void* TouchSDK_Constructor(void* in) {
		return in;
	}

	bool TouchSDK_SendData(dev_info_t*,
	        unsigned char * const, int, unsigned char * const,
	        unsigned char* output, int output_size) {

        // fake success
        if (output_size >= 4) {
			output[0] = 0xfc;
			output[1] = 0xa5;
		}
		return true;
	}

	bool TouchSDK_SetSignalInit(dev_info_t*, int) {
		return true;
	}

	void TouchSDK_Destructor(void* This) {
	}

	int TouchSDK_GetYLedTotal(dev_info_t*, int) {
		return 53;
	}

	int TouchSDK_GetXLedTotal(dev_info_t*, int) {
		return 41;
	}

	bool TouchSDK_DisableTouch(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_DisableDrag(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_DisableWheel(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_DisableRightClick(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_SetMultiTouchMode(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_EnableTouchWidthData(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_EnableRawData(dev_info_t*, int) {
		return true;
	}

	bool TouchSDK_SetAllEnable(dev_info_t*, bool, int) {
		return true;
	}

	int TouchSDK_GetTouchDeviceCount(void* This) {
		return 1;
	}

	unsigned int TouchSDK_GetTouchSDKVersion(void) {
		return 0x01030307;
	}

	int TouchSDK_InitTouch(void* This, dev_info_t *devices, int max_devices, void* touch_event_cb,
	        void* hotplug_callback, void* userdata) {

        // fake touch device
        memset(devices, 0, sizeof(devices[0].raw));
        devices[0].unk1 = 0x1122;
        devices[0].unk2 = 0x3344;
        devices[0].device_id = 0;
        devices[0].vid = 0xDEAD;
        devices[0].pid = 0xBEEF;
        devices[0].pvn = 0xC0DE;
        devices[0].max_point_num = 16;

        // remember provided callback and userdata
        touch_callback = (decltype(touch_callback)) touch_event_cb;
        user_data = userdata;

        // success
		return 1;
	}

    char DRS_TAPELED[DRS_TAPELED_COLS_TOTAL][3] {};
    bool DISABLE_TOUCH = false;
    bool TRANSPOSE_TOUCH = false;
    bool RGB_CAMERA_HOOK = false;

    std::vector<TouchEvent> TOUCH_EVENTS;

    inline DWORD scale_double_to_xy(double val) {
        return static_cast<DWORD>(val * 32768);
    }

    inline DWORD scale_double_to_height(double val) {
        return static_cast<DWORD>(val * 1312);
    }

    inline DWORD scale_double_to_width(double val) {
        return static_cast<DWORD>(val * 1696);
    }

    void fire_touches(drs_touch_t *events, size_t event_count) {

        // check callback first
        if (!touch_callback) {
            return;
        }

        // generate touch data
        auto game_touches = std::make_unique<touch_data_t[]>(event_count);
        for (size_t i = 0; i < event_count; i++) {

            // initialize touch value
            game_touches[i].cid = (DWORD) events[i].id;
            game_touches[i].unk8 = 0;

            // copy scaled values
            game_touches[i].x = scale_double_to_xy(events[i].x);
            game_touches[i].y = scale_double_to_xy(events[i].y);
            game_touches[i].width = scale_double_to_width(events[i].width);
            game_touches[i].height = scale_double_to_height(events[i].height);

            // decide touch type
            switch(events[i].type) {
                case DRS_DOWN:
                    game_touches[i].type = TS_DOWN;
                    break;
                case DRS_UP:
                    game_touches[i].type = TS_UP;
                    break;
                case DRS_MOVE:
                    game_touches[i].type = TS_MOVE;
                    break;
                default:
                    break;
            }
        }

        // build device information
        dev_info_t dev;
        dev.unk1 = 0;
        dev.unk2 = 0;
        dev.device_id = 0;
        dev.vid = 0xDEAD;
        dev.pid = 0xBEEF;
        dev.pvn = 0xC0DE;
        dev.max_point_num = 16;

        // fire callback
        touch_callback(&dev, game_touches.get(), (int) event_count, 0, user_data);
    }

    void start_touch() {
        std::thread t([] {
            log_info("drs", "starting touch input thread");

            // main loop
            while (TRUE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                TOUCH_EVENTS.clear();
                touch_get_events(TOUCH_EVENTS);
                for (auto &te : TOUCH_EVENTS) {
                    drs_touch_t drs_event;
                    switch (te.type) {
                        case TOUCH_DOWN:
                            drs_event.type = DRS_DOWN;
                            break;
                        case TOUCH_UP:
                            drs_event.type = DRS_UP;
                            break;
                        case TOUCH_MOVE:
                            drs_event.type = DRS_MOVE;
                            break;
                    }
                    drs_event.id = te.id;

                    // DRS uses 100x100 (px) as default foot size, so use that
                    const float w = 100.1/1920.f;
                    const float h = 100.1/1080.f;
                    drs_event.width = w;
                    drs_event.height = h;

                    const float x = te.x / 1920.f;
                    const float y = te.y / 1080.f;
                    // note that only x-y are transposed, not w-h
                    if (TRANSPOSE_TOUCH) {
                        drs_event.x = y;
                        drs_event.y = x;
                    } else {
                        drs_event.x = x;
                        drs_event.y = y;
                    }
                    drs::fire_touches(&drs_event, 1);
                }
            }

            return nullptr;
        });
        t.detach();
    }

    DRSGame::DRSGame() : Game("DANCERUSH") {
    }

    void DRSGame::attach() {
        Game::attach();

        // TouchSDK hooks
        detour::iat("??0TouchSDK@@QEAA@XZ",
                (void *) &TouchSDK_Constructor, avs::game::DLL_INSTANCE);
        detour::iat("?SendData@TouchSDK@@QEAA_NU_DeviceInfo@@QEAEH1HH@Z",
                (void *) &TouchSDK_SendData, avs::game::DLL_INSTANCE);
        detour::iat("?SetSignalInit@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_SetSignalInit, avs::game::DLL_INSTANCE);
        detour::iat("??1TouchSDK@@QEAA@XZ",
                (void *) &TouchSDK_Destructor, avs::game::DLL_INSTANCE);
        detour::iat("?GetYLedTotal@TouchSDK@@QEAAHU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_GetYLedTotal, avs::game::DLL_INSTANCE);
        detour::iat("?GetXLedTotal@TouchSDK@@QEAAHU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_GetXLedTotal, avs::game::DLL_INSTANCE);
        detour::iat("?DisableTouch@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_DisableTouch, avs::game::DLL_INSTANCE);
        detour::iat("?DisableDrag@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_DisableDrag, avs::game::DLL_INSTANCE);
        detour::iat("?DisableWheel@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_DisableWheel, avs::game::DLL_INSTANCE);
        detour::iat("?DisableRightClick@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_DisableRightClick, avs::game::DLL_INSTANCE);
        detour::iat("?SetMultiTouchMode@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_SetMultiTouchMode, avs::game::DLL_INSTANCE);
        detour::iat("?EnableTouchWidthData@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_EnableTouchWidthData, avs::game::DLL_INSTANCE);
        detour::iat("?EnableRawData@TouchSDK@@QEAA_NU_DeviceInfo@@H@Z",
                (void *) &TouchSDK_EnableRawData, avs::game::DLL_INSTANCE);
        detour::iat("?SetAllEnable@TouchSDK@@QEAA_NU_DeviceInfo@@_NH@Z",
                (void *) &TouchSDK_SetAllEnable, avs::game::DLL_INSTANCE);
        detour::iat("?GetTouchDeviceCount@TouchSDK@@QEAAHXZ",
                (void *) &TouchSDK_GetTouchDeviceCount, avs::game::DLL_INSTANCE);
        detour::iat("?GetTouchSDKVersion@TouchSDK@@QEAAIXZ",
                (void *) &TouchSDK_GetTouchSDKVersion, avs::game::DLL_INSTANCE);
        detour::iat("?InitTouch@TouchSDK@@QEAAHPEAU_DeviceInfo@@HP6AXU2@PEBU_TouchPointData@@HHPEBX@ZP6AX1_N3@ZPEAX@Z",
                (void *) &TouchSDK_InitTouch, avs::game::DLL_INSTANCE);

        if (!DISABLE_TOUCH) {
            start_touch();
        } else {
            log_info("drs", "no native input method detected");
        }

        if (RGB_CAMERA_HOOK) {
            init_rgb_camera_hook();
        }
    }

    void DRSGame::detach() {
        Game::detach();
    }
}
