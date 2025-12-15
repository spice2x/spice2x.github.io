#include "smxdedicab.h"

#include "smx.h"
#include "util/logging.h"

namespace {
    static const std::string DEVICE_NAMES[] = {
        "Marquee",
        "Left Vertical Strip",
        "Left Spotlights",
        "Right Vertical Strip",
        "Right Spotlights"
    };

    static const size_t DEVICE_LED_COUNTS[] = {
        24, 28, 8, 28, 8
    };

    void NullSmxCallback(int pad, SMXUpdateCallbackReason reason, void *pUser) {
    }
}

namespace rawinput {
    std::string SmxDedicabDevice::GetLightNameByIndex(size_t index) {
        const size_t subpixel = index % 3;
        const size_t device = (index / 3);

        switch (subpixel) {
            case 0: return fmt::format("{} Red", DEVICE_NAMES[device]);
            case 1: return fmt::format("{} Green", DEVICE_NAMES[device]);
            case 2: return fmt::format("{} Blue", DEVICE_NAMES[device]);
        }

        return "StepManiaX Dedicated Cabinet - Invalid Index";
    }

    SmxDedicabDevice::SmxDedicabDevice() :
        m_lightData {
            new uint8_t[DEVICE_LED_COUNTS[0] * 3],
            new uint8_t[DEVICE_LED_COUNTS[1] * 3],
            new uint8_t[DEVICE_LED_COUNTS[2] * 3],
            new uint8_t[DEVICE_LED_COUNTS[3] * 3],
            new uint8_t[DEVICE_LED_COUNTS[4] * 3]
        },
        m_lightDataMutex()
    {
    }

    bool SmxDedicabDevice::Initialize() {
        SMXWrapper::getInstance().SMX_Start(&NullSmxCallback, nullptr);

        // After initializing the connection, we need to momentarily pause in order for the
        // IO device to not miss our first packets. Then we need to send 0x00 to flush out each
        // channel on initialization. If we don't do this, the lights have garbage data for each
        // channel until they're fully flushed.
        Sleep(1000);
        for (size_t device = 0; device < DEVICE_COUNT; device++) {
            const size_t numLeds = DEVICE_LED_COUNTS[device];
            for (size_t j = 0; j < numLeds * 3; j++) {
                m_lightData[device][j] = 0x00;
            }

            SMXWrapper::getInstance().SMX_SetDedicatedCabinetLights(
                static_cast<SMXDedicatedCabinetLights>(device),
                reinterpret_cast<const char*>(&(m_lightData[device][0])),
                numLeds * 3
            );
        }
        return true;
    }

    void SmxDedicabDevice::SetLightByIndex(size_t index, uint8_t value) {
        const size_t subpixel = index % 3;
        const size_t device = (index / 3);
        const size_t numLeds = DEVICE_LED_COUNTS[device];

        m_lightDataMutex.lock();
        for (size_t i = 0; i < numLeds; i++) {
            m_lightData[device][(i * 3) + subpixel] = value;
        }
        m_lightDataMutex.unlock();
    }

    void SmxDedicabDevice::Update() {
        m_lightDataMutex.lock();
        for (int i = 0; i < DEVICE_COUNT; i++) {
            const size_t numLeds = DEVICE_LED_COUNTS[i];
            SMXWrapper::getInstance().SMX_SetDedicatedCabinetLights(
                static_cast<SMXDedicatedCabinetLights>(i),
                reinterpret_cast<const char*>(&(m_lightData[i][0])),
                numLeds * 3
            );
        }
        m_lightDataMutex.unlock();
    }
}