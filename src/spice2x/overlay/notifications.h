#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace overlay::notifications {

    // master switch for the notification system; when false, add() is a no-op.
    // controlled by selecting "none" for the -notifypos launcher option.
    extern bool ENABLED;

    enum class Severity {
        Info,
        Success,
        Warning,
        Error,
    };

    // screen anchor for the toast stack. toasts stack away from the anchored edge.
    enum class Position {
        BottomRight,
        BottomLeft,
        TopRight,
        TopLeft,
    };

    // current toast anchor. defaults to BottomRight; may be reassigned by
    // apply_game_default_position() or by the user via -notifypos.
    extern Position POSITION;

    // apply the default toast position appropriate for a game (by display name,
    // as returned by eamuse_get_game()). called once after game autodetect, before
    // any user -notifypos override is applied.
    void apply_game_default_position(const std::string &game_name);

    // add a notification (thread-safe). returns the assigned id, or 0 if the
    // notification was dropped (overlay disabled or notifications disabled).
    uint64_t add(Severity severity, std::string text);

    // rate-limited variant of add(). suppresses the toast if another call with
    // the same `key` succeeded within the last `cooldown_seconds`. useful for
    // events that can fire every frame (e.g. a button held down). returns the
    // assigned id, or 0 if the toast was suppressed or dropped. thread-safe.
    uint64_t add_throttled(Severity severity, const std::string &key,
                           double cooldown_seconds, std::string text);

    // true if there is at least one notification that still needs to be drawn.
    // safe to call from the render thread without locking the underlying store.
    bool has_pending();

    // draw all active notifications and prune expired ones.
    // must be called from the ImGui render thread inside a NewFrame/EndFrame pair.
    void draw();
}
