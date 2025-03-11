#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace rawinput {
    class SmxDedicabDevice {
    public:
        static constexpr int DEVICE_COUNT = 5;
        static constexpr int LIGHTS_COUNT = DEVICE_COUNT * 3;
        static std::string GetLightNameByIndex(size_t index);

        SmxDedicabDevice();
        bool Initialize();
        void SetLightByIndex(size_t index, uint8_t value);
        void Update();
    private:
        uint8_t* m_lightData[DEVICE_COUNT];
        std::mutex m_lightDataMutex;
    };
}