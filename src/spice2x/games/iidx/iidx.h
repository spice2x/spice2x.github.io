#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "games/game.h"

namespace games::iidx {

    // settings

    extern bool FLIP_CAMS;
    extern bool DISABLE_CAMS;
    extern bool TDJ_CAMERA;
    extern bool TDJ_CAMERA_PREFER_16_9;
    extern std::optional<std::string> TDJ_CAMERA_OVERRIDE;

    extern bool TDJ_MODE;
    extern bool FORCE_720P;
    extern bool DISABLE_ESPEC_IO;
    extern bool NATIVE_TOUCH;
    extern std::optional<std::string> SOUND_OUTPUT_DEVICE;
    extern std::optional<std::string> ASIO_DRIVER;
    extern uint8_t DIGITAL_TT_SENS;
    extern std::optional<std::string> SUBSCREEN_OVERLAY_SIZE;
    extern std::optional<std::string> SCREEN_MODE;

    // state
    extern char IIDXIO_LED_TICKER[10];
    extern bool IIDXIO_LED_TICKER_READONLY;
    extern std::mutex IIDX_LED_TICKER_LOCK;

    constexpr int IIDX_TAPELED_TOTAL = 17;
    // data mapping
    struct TapeLedMapping {
        std::vector<uint8_t[3]> data;
        int index_r, index_g, index_b;

        TapeLedMapping(size_t data_size, int index_r, int index_g, int index_b)
        : index_r(index_r), index_g(index_g), index_b(index_b) {
            data = std::vector<uint8_t[3]>(data_size);
        }
    };
    extern TapeLedMapping TAPELED_MAPPING[IIDX_TAPELED_TOTAL];

    class IIDXGame : public games::Game {
    public:
        IIDXGame();
        virtual ~IIDXGame() override;

        virtual void attach() override;
        virtual void pre_attach() override;
        virtual void detach() override;

    private:
        void detect_sound_output_device();
    };

    // helper methods
    uint32_t get_pad();
    void write_lamp(uint16_t lamp);
    void write_led(uint8_t led);
    void write_top_lamp(uint8_t top_lamp);
    void write_top_neon(uint8_t top_neon);
    unsigned char get_tt(int player, bool slow);
    unsigned char get_slider(uint8_t slider);
    const char* get_16seg();
    bool is_tdj_fhd();
    void apply_audio_hacks();
}
