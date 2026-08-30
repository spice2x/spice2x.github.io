#include "games/drs/motion_cam.h"

#include <cstdint>

#include "avs/game.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/sigscan.h"
#include "io.h"

namespace games::drs {

    // the game reads down movement from a depth camera whose driver is compiled into the game DLL,
    // so there is no import to replace.
    //
    // with no camera the tracked height never changes, so the motion state never leaves "still"
    // and DOWN notes can never be hit.
    //
    // rather than answer CInputManager::IsDown, the button drives the same state pair the camera
    // would; that leaves the game's own edge test untouched and keeps every note in a frame seeing
    // one answer.
    static int32_t *MOTION_STATE = nullptr;
    static int32_t *MOTION_STATE_PREV = nullptr;
    static constexpr int32_t MOTION_STATE_STILL = 1;
    static constexpr int32_t MOTION_STATE_DOWN = 2;

    static void (*UpdateMotion_orig)() = nullptr;
    static bool DOWN_MOTION_HELD = false;

    static void InputManager_UpdateMotion() {
        UpdateMotion_orig();

        auto &buttons = get_buttons();
        const bool held = GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::DownMotion));

        // a state of DOWN only counts as movement when the frame before it was something else
        if (held) {
            *MOTION_STATE_PREV = DOWN_MOTION_HELD ? MOTION_STATE_DOWN : MOTION_STATE_STILL;
            *MOTION_STATE = MOTION_STATE_DOWN;
        }

        DOWN_MOTION_HELD = held;
    }

    void init_down_motion_hook() {
        // only hook motion camera if the user has down motion button bound to something
        auto &buttons = get_buttons();
        if (!buttons.at(Buttons::DownMotion).isSet()) {
            return;
        }

        // CInputManager::IsDown - read the state pair out of its two operands.
        // Identical in game versions 2020121400 and 2022121400 / 2024120300:
        //
        //   83 3D xx xx xx xx 02   cmp dword [rip+prev], 2  ; state one frame ago
        //   74 0C                  je  FALSE                ; already down, so no new edge
        //   83 3D xx xx xx xx 02   cmp dword [rip+cur], 2   ; state this frame
        //   75 03                  jne FALSE                ; not down
        //   B0 01                  mov al, 1                ; went down on this frame
        //   C3                     ret
        //   32 C0           FALSE: xor al, al
        //   C3                     ret
        auto is_down = reinterpret_cast<uint8_t *>(find_pattern(
                avs::game::DLL_INSTANCE,
                "833D0000000002740C833D00000000027503B001C332C0C3",
                "XX????XXXXX????XXXXXXXXX",
                0, 0));

        if (is_down == nullptr) {
            log_warning("drs", "motion sensor state not found, DOWN motion button unavailable");
            return;
        }

        // rip-relative displacements resolve against the end of their own instruction, and each
        // cmp above is 7 bytes with its displacement 4 bytes in: the first ends at +7 with its
        // displacement at +2, the second starts at +9 so it ends at +16 with its own at +11.
        // read signed, since a target sitting below the instruction encodes as negative.
        MOTION_STATE_PREV = (int32_t *) (is_down + 7 + *(int32_t *) (is_down + 2));
        MOTION_STATE = (int32_t *) (is_down + 16 + *(int32_t *) (is_down + 11));

        // CInputManager::UpdateMotion is hooked for the sole purpose of reading the Down Motion
        // button mapping frequently enough so that the rising edge can be detected.
        //
        // Matched over the prologue up to the camera read; the wildcards are a float and a callee:
        //
        //   48 83 EC 38               sub    rsp, 0x38
        //   0F 29 74 24 20            movaps [rsp+0x20], xmm6
        //   F3 0F 10 35 xx xx xx xx   movss  xmm6, [rip+height]  ; reading as of last frame
        //   E8 xx xx xx xx            call   GetPlayVideoProcess
        //   48 8B C8                  mov    rcx, rax
        //   48 8B 10                  mov    rdx, [rax]
        //   FF 52 60                  call   [rdx+0x60]          ; reading off the newest frame
        auto update = reinterpret_cast<void *>(find_pattern(
                avs::game::DLL_INSTANCE,
                "4883EC380F29742420F30F103500000000E800000000488BC8488B10FF5260",
                "XXXXXXXXXXXXX????X????XXXXXXXXX",
                0, 0));

        if (update == nullptr ||
            !detour::trampoline_try(
                update, (void *) InputManager_UpdateMotion, (void **) &UpdateMotion_orig)) {
            log_warning("drs", "motion update not hooked, DOWN motion button unavailable");
            return;
        }

        log_info("drs", "hooked DOWN motion at +{:#x}",
                (uintptr_t) ((uint8_t *) update - (uint8_t *) avs::game::DLL_INSTANCE));
    }
}
