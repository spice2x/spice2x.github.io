#pragma once

#include "smx.h"
#include <string>
#include <vector>
#include <mutex>

namespace rawinput {
    class SmxStageDevice {
    public:
        static constexpr int PAD_COUNT = 2;
        static constexpr int PANEL_COUNT = 9;
        static constexpr int LEDS_PER_PAD = 25;
        static constexpr int TOTAL_LIGHT_COUNT = PAD_COUNT*PANEL_COUNT*3;

        static inline size_t GetPanelIndexR(size_t pad, size_t panel) {return ((pad*PANEL_COUNT)+panel)*3 + 0;};
        static inline size_t GetPanelIndexG(size_t pad, size_t panel) {return ((pad*PANEL_COUNT)+panel)*3 + 1;};
        static inline size_t GetPanelIndexB(size_t pad, size_t panel) {return ((pad*PANEL_COUNT)+panel)*3 + 2;};

        static std::string GetLightNameByIndex(size_t index);

        SmxStageDevice();
        bool Initialize();
        void SetLightByIndex(size_t index, uint8_t value);
        void Update();
    private:
        std::vector<uint8_t> m_lightData;
        std::mutex m_lightDataMutex;
    };
}