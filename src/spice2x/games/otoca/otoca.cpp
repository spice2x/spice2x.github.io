#include "otoca.h"
#include "p4io.h"
#include "games/shared/printer.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/sigscan.h"

#define OTOCA_DEBUG_VERBOSE 0
#if OTOCA_DEBUG_VERBOSE
#define log_debug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_debug(module, format_str, ...)
#endif

namespace games::otoca {

    bool BYPASS_CAMERA = false;

    // for dev only
    static bool CAMLIB_CALL_ORIGINAL = false;

    OtocaGame::OtocaGame() : Game("Otoca D'or") {
    }

    OtocaGame::~OtocaGame() {
    }

    typedef void (__cdecl *LibCameraInit_t)();
    static LibCameraInit_t LibCameraInit_orig = nullptr;
    static void __cdecl LibCameraInit_hook() {
        if (CAMLIB_CALL_ORIGINAL) {
            LibCameraInit_orig();
            log_debug("otoca::cam", "LibCameraInit returned");
        }
    }

    typedef int (__cdecl *LibCameraOpen_t)(double, int, int);
    static LibCameraOpen_t LibCameraOpen_orig = nullptr;
    static int __cdecl LibCameraOpen_hook(double a1, int a2, int a3) {
        int ret = 0;
        if (CAMLIB_CALL_ORIGINAL) {
            // with webcam, (15, 2, 0) returned 0
            ret = LibCameraOpen_orig(a1, a2, a3);
            log_debug("otoca::cam", "LibCameraOpen({}, {}, {}) returned {}", a1, a2, a3, ret);
        }
        return ret;
    }

    typedef int (__cdecl *LibCameraStop_t)(int);
    static LibCameraStop_t LibCameraStop_orig = nullptr;
    static int __cdecl LibCameraStop_hook(int a1) {
        int ret = 0;
        if (CAMLIB_CALL_ORIGINAL) {
            // with webcam,
            ret = LibCameraStop_orig(a1);
            log_debug("otoca::cam", "LibCameraStop({}) returned {}", a1, ret);
        }
        return ret;
    }

    typedef void (__cdecl *LibCameraGetCameraNr_t)();
    static LibCameraGetCameraNr_t LibCameraGetCameraNr_orig = nullptr;
    static void __cdecl LibCameraGetCameraNr_hook() {
        if (CAMLIB_CALL_ORIGINAL) {
            LibCameraGetCameraNr_orig();
            log_debug("otoca::cam", "LibCameraGetCameraNr returned");
        }
    }

    typedef int (__cdecl *LibCameraRun_t)(int);
    static LibCameraRun_t LibCameraRun_orig = nullptr;
    static int __cdecl LibCameraRun_hook(int a1) {
        int ret = 0;
        if (CAMLIB_CALL_ORIGINAL) {
            // with webcam, (0) returned 0
            ret = LibCameraRun_orig(a1);
            log_debug("otoca::cam", "LibCameraRun({}) returned {}", a1, ret);
        }
        return ret;
    }

    typedef int (__cdecl *LibCameraGetImage_t)(int, void *);
    static LibCameraGetImage_t LibCameraGetImage_orig = nullptr;
    static int __cdecl LibCameraGetImage_hook(int a1, void *a2) {
        int ret = 0;
        if (CAMLIB_CALL_ORIGINAL) {
            // not sure if this is called
            ret = LibCameraGetImage_orig(a1, a2);
            log_debug("otoca::cam", "LibCameraGetImage({}, {}) returned {}", a1, fmt::ptr(a2), ret);
        }
        return ret;
    }

    typedef int (__cdecl *LibCameraGetBrightness_t)(int *, int);
    static LibCameraGetBrightness_t LibCameraGetBrightness_orig = nullptr;
    static int __cdecl LibCameraGetBrightness_hook(int *a1, int a2) {
        int ret = 0;
        if (CAMLIB_CALL_ORIGINAL) {
            // with webcam, (ptr,) returned 
            ret = LibCameraGetBrightness_orig(a1, a2);
        }
        log_debug("otoca::cam", "LibCameraGetBrightness({}, {}) returned {}", fmt::ptr(a1), a2, ret);
        return ret;
    }

    typedef int (__cdecl *LibCameraGetBrightnessRange_t)(int *, int *, int);
    static LibCameraGetBrightnessRange_t LibCameraGetBrightnessRange_orig = nullptr;
    static int __cdecl LibCameraGetBrightnessRange_hook(int *a1, int *a2, int a3) {
        int ret = 0;
        if (CAMLIB_CALL_ORIGINAL) {
            // with webcam, (ptr, ptr, 0) returned 0
            ret = LibCameraGetBrightnessRange_orig(a1, a2, a3);
            log_debug("otoca::cam", "LibCameraGetBrightnessRange({}, {}, {}) returned {}", fmt::ptr(a1), fmt::ptr(a2), a3, ret);
        }
        return ret;
    }

    static void patch_holo_print_hang() {

        // arkkep.dll's print method sets the printer busy (0x66 / Printing_Busy),
        // then bails out with -1 WITHOUT printing when its holo parameter is
        // nonzero (the card path passes 0 and works). The completion callback that
        // clears busy never fires, so the page's poll loop spins forever -> hang.
        // The image band is already built, so flip the je that skips the bail-out
        // into a jmp (74 -> EB) to route holo through the print path like card.
        //
        //   je   issue_print     ; <- patched to jmp
        //   or   eax, -1         ; holo: return without printing
        //   ret  10h
        auto arkkep = libutils::try_module("arkkep.dll");
        if (arkkep == nullptr) {
            log_warning("otoca", "arkkep.dll not loaded; skipping holo print hang fix");
            return;
        }

        auto result = replace_pattern(
            arkkep,
            "C7465866000000895DC0C74710080700007416",
            "C7465866000000895DC0C7471008070000EB16",
            0, 0);
        if (result) {
            log_info("otoca", "patched hologram print hang in arkkep.dll");
        } else {
            log_warning("otoca", "could not patch hologram print hang (incompatible arkkep.dll?)");
        }
    }

    static void patch_holo_print_search() {

        // before printing, arkkep walks the page's printer list looking for one
        // whose capability flag (byte at entry+120h) matches the job type
        // (0 = card, 1 = holo). Our emulated printer is only card-capable, so a
        // holo job finds no match and aborts without printing ("not found holo
        // printer"). The emulated printer serves both media, so force the compare
        // to always match by turning `cmp cl,[edi+120h]` into `cmp cl,cl` + NOPs,
        // which always sets ZF. Two such compares exist, so patch both.
        //
        //   cmp  cl, [edi+120h]  ; <- patched to `cmp cl,cl` (3A 8F.. -> 3A C9..)
        //   je   printer_found
        auto arkkep = libutils::try_module("arkkep.dll");
        if (arkkep == nullptr) {
            log_warning("otoca", "arkkep.dll not loaded; skipping holo print search fix");
            return;
        }

        int patched = 0;
        while (patched < 2) {
            auto result = replace_pattern(
                arkkep,
                "3A8F20010000",
                "3AC990909090",
                0, 0);
            if (!result) {
                break;
            }
            patched++;
        }
        if (patched > 0) {
            log_info("otoca", "patched hologram printer search in arkkep.dll ({} site(s))", patched);
        } else {
            log_warning("otoca", "could not patch hologram printer search (incompatible arkkep.dll?)");
        }
    }

    void OtocaGame::attach() {
        Game::attach();

        /*
         * arkkep.dll uses LoadLibrary to access game.dll
         * we preload it so that hooks (e.g. for graphics) will work
         */
        libutils::try_library("game.dll");

        // initialize IO hooks
        p4io_hook();
        games::shared::printer_attach();

        // fix hologram print hard hang in arkkep.dll
        patch_holo_print_hang();

        // make hologram jobs find the emulated printer (else they abort unprinted)
        patch_holo_print_search();

        if (BYPASS_CAMERA) {
            libutils::try_library("libcamera.dll");
            const auto libcamera = "libcamera.dll";
            detour::trampoline_try(
                libcamera,
                "?LibCameraInit@@YAXXZ",
                LibCameraInit_hook, &LibCameraInit_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraOpen@@YA?AW4LIBCAMERA_STATUS@@NHPAUt_libcamera_open_param@@@Z",
                LibCameraOpen_hook, &LibCameraOpen_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraStop@@YA?AW4LIBCAMERA_STATUS@@H@Z",
                LibCameraStop_hook, &LibCameraStop_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraGetCameraNr@@YAHXZ",
                LibCameraGetCameraNr_hook, &LibCameraGetCameraNr_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraRun@@YA?AW4LIBCAMERA_STATUS@@H@Z",
                LibCameraRun_hook, &LibCameraRun_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraGetImage@@YA?AW4LIBCAMERA_STATUS@@HPAX@Z",
                LibCameraGetImage_hook, &LibCameraGetImage_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraGetBrightness@@YA?AW4LIBCAMERA_STATUS@@PAJH@Z",
                LibCameraGetBrightness_hook, &LibCameraGetBrightness_orig);
            detour::trampoline_try(
                libcamera,
                "?LibCameraGetBrightnessRange@@YA?AW4LIBCAMERA_STATUS@@PAJ0H@Z",
                LibCameraGetBrightnessRange_hook, &LibCameraGetBrightnessRange_orig);
        }
    }
}
