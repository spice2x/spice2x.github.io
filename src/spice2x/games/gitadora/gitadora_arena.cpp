#include "gitadora_arena.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

#include "avs/game.h"
#include "gitadora.h"
#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "util/deferlog.h"
#include "util/logging.h"
#include "util/memutils.h"
#include "util/sigscan.h"
#include "util/utils.h"

namespace games::gitadora {

    static constexpr std::pair<uint32_t, uint32_t> ARENA_NATIVE_RESOLUTION { 3840, 2160 };
    static std::optional<std::pair<uint32_t, uint32_t>> arena_render_resolution;

    static size_t replace_all_patterns(
            HMODULE module,
            const std::string &signature,
            const std::string &replacement)
    {
        size_t count = 0;
        while (replace_pattern(module, signature, replacement, 0, 0) != 0) {
            count++;
        }
        return count;
    }

    static int32_t read_i32(const void *address) {
        int32_t value;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    static void write_i32(int32_t *address, int32_t value) {
        memutils::VProtectGuard guard(address, sizeof(*address));
        *address = value;
    }

    static std::string replacement_i32_le(uint32_t value) {
        return fmt::format(
            "{:02X}{:02X}{:02X}{:02X}",
            value & 0xff,
            (value >> 8) & 0xff,
            (value >> 16) & 0xff,
            (value >> 24) & 0xff);
    }

    static bool is_16_by_9(uint32_t width, uint32_t height) {
        return static_cast<uint64_t>(width) * 9 == static_cast<uint64_t>(height) * 16;
    }

    static bool is_60hz(const DEVMODEA &dm) {
        return dm.dmDisplayFrequency == 0 ||
            dm.dmDisplayFrequency == 1 ||
            (dm.dmDisplayFrequency >= 59 && dm.dmDisplayFrequency <= 61);
    }

    static bool monitor_supports_resolution(uint32_t width, uint32_t height) {
        DEVMODEA dm {};
        dm.dmSize = sizeof(dm);
        for (DWORD mode = 0; EnumDisplaySettingsA(nullptr, mode, &dm); mode++) {
            if (dm.dmPelsWidth == width && dm.dmPelsHeight == height && is_60hz(dm)) {
                return true;
            }
        }
        return false;
    }

    static std::optional<std::pair<uint32_t, uint32_t>> choose_auto_resolution() {
        if (monitor_supports_resolution(
                ARENA_NATIVE_RESOLUTION.first,
                ARENA_NATIVE_RESOLUTION.second)) {
            return std::nullopt;
        }

        std::optional<std::pair<uint32_t, uint32_t>> best;
        DEVMODEA dm {};
        dm.dmSize = sizeof(dm);
        for (DWORD mode = 0; EnumDisplaySettingsA(nullptr, mode, &dm); mode++) {
            const auto width = static_cast<uint32_t>(dm.dmPelsWidth);
            const auto height = static_cast<uint32_t>(dm.dmPelsHeight);
            if (!is_60hz(dm) ||
                    !is_16_by_9(width, height) ||
                    width > ARENA_NATIVE_RESOLUTION.first ||
                    height > ARENA_NATIVE_RESOLUTION.second) {
                continue;
            }
            if (!best.has_value() ||
                    static_cast<uint64_t>(width) * height >
                        static_cast<uint64_t>(best.value().first) * best.value().second) {
                best = { width, height };
            }
        }
        return best;
    }

    static bool parse_arena_resolution(
            const std::string &text,
            std::pair<uint32_t, uint32_t> &resolution) {
        if (parse_width_height(text, resolution)) {
            return true;
        }

        std::string comma_text = text;
        std::replace(comma_text.begin(), comma_text.end(), 'x', ',');
        std::replace(comma_text.begin(), comma_text.end(), 'X', ',');
        return parse_width_height(comma_text, resolution);
    }

    static void apply_arena_overlay_scale(std::pair<uint32_t, uint32_t> resolution) {
        if (overlay::UI_SCALE_PERCENT.has_value()) {
            return;
        }

        const auto scale = std::max(
            100u,
            static_cast<uint32_t>(std::lround(250.0 * resolution.second / ARENA_NATIVE_RESOLUTION.second)));
        overlay::UI_SCALE_PERCENT = scale;
        log_info(
            "gitadora",
            "arena model: UI scale {}% for render resolution {}x{}",
            scale,
            resolution.first,
            resolution.second);
    }

    struct ArenaResolutionGlobalInitializer {
        uint8_t *width_store = nullptr;
        uint8_t *height_store = nullptr;
        int32_t *width = nullptr;
        int32_t *height = nullptr;
    };

    void configure_arena_render_resolution() {
        if (!is_arena_model()) {
            return;
        }

        arena_render_resolution.reset();

        const auto requested = ARENA_RESOLUTION.value_or("auto");
        if (requested == "off" || requested == "disable" || requested == "disabled") {
            log_info("gitadora", "arena model: native 4K render target requested");
            apply_arena_overlay_scale(ARENA_NATIVE_RESOLUTION);
            return;
        }

        std::optional<std::pair<uint32_t, uint32_t>> resolution;
        if (requested == "auto") {
            resolution = choose_auto_resolution();
            if (!resolution.has_value()) {
                log_info(
                    "gitadora",
                    "arena model: primary monitor supports 3840x2160@60, using native render target");
                apply_arena_overlay_scale(ARENA_NATIVE_RESOLUTION);
                return;
            }
        } else {
            std::pair<uint32_t, uint32_t> parsed;
            if (!parse_arena_resolution(requested, parsed)) {
                log_warning("gitadora", "invalid -gdaresolution value: {}", requested);
                deferredlogs::defer_error_messages({
                    "Invalid GITADORA Arena render resolution; using native 4K.",
                });
                apply_arena_overlay_scale(ARENA_NATIVE_RESOLUTION);
                return;
            }
            resolution = parsed;
        }

        if (resolution == ARENA_NATIVE_RESOLUTION) {
            log_info("gitadora", "arena model: using native 3840x2160 render target");
            apply_arena_overlay_scale(ARENA_NATIVE_RESOLUTION);
            return;
        }

        arena_render_resolution = resolution;
        log_info(
            "gitadora",
            "arena model: forcing render target {}x{}",
            resolution.value().first,
            resolution.value().second);

        if (!GRAPHICS_FS_CUSTOM_RESOLUTION.has_value()) {
            GRAPHICS_FS_CUSTOM_RESOLUTION = resolution;
        }
        if (!GRAPHICS_WINDOW_SIZE.has_value()) {
            GRAPHICS_WINDOW_SIZE = resolution;
        }
        GRAPHICS_WINDOW_BACKBUFFER_SCALE = true;
        apply_arena_overlay_scale(resolution.value());
    }

    bool is_arena_resolution_patch_enabled() {
        return is_arena_model() && arena_render_resolution.has_value();
    }

    void apply_arena_resolution_patch() {
        if (!is_arena_resolution_patch_enabled()) {
            return;
        }

        const auto [target_width, target_height] = arena_render_resolution.value();
        auto gdxg_module = avs::game::DLL_INSTANCE;

        std::vector<ArenaResolutionGlobalInitializer> global_initializers;
        size_t global_candidate_count = 0;
        size_t global_count = 0;
        size_t global_write_count = 0;
        for (intptr_t usage = 0;; usage++) {
            const auto match = find_pattern(
                    gdxg_module,
                    "C70500000000000F0000C7050000000070080000",
                    "XX????XXXXXX????XXXX",
                    0,
                    usage);
            if (!match) {
                break;
            }

            auto *width_store = reinterpret_cast<uint8_t *>(match);
            auto *height_store = width_store + 10;
            auto *width = reinterpret_cast<int32_t *>(width_store + 10 + read_i32(width_store + 2));
            auto *height = reinterpret_cast<int32_t *>(height_store + 10 + read_i32(height_store + 2));
            global_candidate_count++;
            global_initializers.push_back({
                .width_store = width_store,
                .height_store = height_store,
                .width = width,
                .height = height,
            });
        }

        for (const auto &initializer : global_initializers) {
            write_i32(reinterpret_cast<int32_t *>(initializer.width_store + 6), target_width);
            write_i32(reinterpret_cast<int32_t *>(initializer.height_store + 6), target_height);
            global_count++;

            // These globals may already be initialized before GitaDoraGame::attach()
            // can patch the init instructions. Keep live runtime state in sync when present.
            if (read_i32(initializer.width) == 3840 && read_i32(initializer.height) == 2160) {
                write_i32(initializer.width, target_width);
                write_i32(initializer.height, target_height);
                global_write_count++;
            }
        }

        const size_t stack_count = replace_all_patterns(
            gdxg_module,
            "C74424??000F0000C74424??70080000",
            "????????" + replacement_i32_le(target_width) + "????????" + replacement_i32_le(target_height));

        if (stack_count == 0 || global_count == 0) {
            log_warning(
                "gitadora",
                "Arena resolution patch may be incomplete: stack patches={}, global patches={}, global candidates={}",
                stack_count,
                global_count,
                global_candidate_count);
            deferredlogs::defer_error_messages({
                "GITADORA Arena resolution patch did not match all expected signatures; "
                "the game may still render at 4K.",
            });
        } else {
            log_info(
                "gitadora",
                "applied Arena resolution patch: target={}x{}, stack patches={}, global patches={}, global writes={}, global candidates={}",
                target_width,
                target_height,
                stack_count,
                global_count,
                global_write_count,
                global_candidate_count);
        }
    }

    void apply_live2d_disable_patch() {
        if (!is_arena_model()) {
            log_warning("gitadora", "-gddisablelive2d is only supported on Arena Model");
            return;
        }

        intptr_t startup_result = replace_pattern(
            avs::game::DLL_INSTANCE,
            "C605????????01E8????????448BC041",
            "488B5C2460488B7424684883C4405FC3",
            0,
            0);

        intptr_t model_result = replace_pattern(
            avs::game::DLL_INSTANCE,
            "4055565741544155415641574881EC8000000048C7442420FEFFFFFF48899C24D8000000C5F829742470C5F8297C2460",
            "33C0C3??????????????????????????????????????????????????????????????????????????????????????????",
            0,
            0);

        if (startup_result == 0 || model_result == 0) {
            log_warning("gitadora", "failed to apply Live2D disable patch");
            deferredlogs::defer_error_messages({
                "GITADORA Live2D disable patch did not match; Live2D will still run.",
            });
        } else {
            log_info(
                "gitadora",
                "applied Live2D disable patch: startup @ 0x{:x}, model @ 0x{:x}",
                startup_result,
                model_result);
        }
    }

    void apply_arena_fullscreen_window_size(
            const std::string &window_name,
            int &x,
            int &y,
            int &width,
            int &height) {
        if (!is_arena_resolution_patch_enabled() ||
                GRAPHICS_WINDOWED ||
                window_name != "GITADORA") {
            return;
        }

        const auto [target_width, target_height] = arena_render_resolution.value();
        if (x == 0 &&
                y == 0 &&
                width == static_cast<int>(target_width) &&
                height == static_cast<int>(target_height)) {
            return;
        }

        log_info(
            "graphics",
            "GITADORA Arena fullscreen window override: pos=({}, {}), size={}x{} => pos=(0, 0), size={}x{}",
            x,
            y,
            width,
            height,
            target_width,
            target_height);

        x = 0;
        y = 0;
        width = static_cast<int>(target_width);
        height = static_cast<int>(target_height);
    }

    void scale_arena_fullscreen_touch_point(LONG &x, LONG &y) {
        if (!is_arena_resolution_patch_enabled() || GRAPHICS_WINDOWED) {
            return;
        }

        const auto [target_width, target_height] = arena_render_resolution.value();
        x = static_cast<LONG>(
            (static_cast<int64_t>(x) * ARENA_NATIVE_RESOLUTION.first) / target_width);
        y = static_cast<LONG>(
            (static_cast<int64_t>(y) * ARENA_NATIVE_RESOLUTION.second) / target_height);
    }
}
