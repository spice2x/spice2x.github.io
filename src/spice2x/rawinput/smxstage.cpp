#include "smxstage.h"

#include "smx.h"
#include "util/logging.h"
#include <fmt/format.h>

namespace {
    static const std::string PANEL_NAMES[] = {
        "Top-Left", "Top-Center", "Top-Right",
        "Middle-Left", "Middle-Center", "Middle-Right",
        "Bottom-Left", "Bottom-Center", "Bottom-Right"
    };

    void NullSmxCallback(int pad, SMXUpdateCallbackReason reason, void *pUser) {
    }
}

namespace rawinput {
    std::string SmxStageDevice::GetLightNameByIndex(size_t index) {
        const size_t subpixel = index%3;
        const size_t panel = (index/3)%PANEL_COUNT;
        const size_t pad = (index/3)/PANEL_COUNT;

        switch (subpixel) {
            case 0: return fmt::format("Pad {} Panel {} Red", pad+1, PANEL_NAMES[panel]);
            case 1: return fmt::format("Pad {} Panel {} Green", pad+1, PANEL_NAMES[panel]);
            case 2: return fmt::format("Pad {} Panel {} Blue", pad+1, PANEL_NAMES[panel]);
        }

        return "StepmaniaX invalid index";
    }

    SmxStageDevice::SmxStageDevice() :
        m_lightData(TOTAL_LIGHT_COUNT*LEDS_PER_PAD),
        m_lightDataMutex()
    {
    }

    bool SmxStageDevice::Initialize() {
        SMXWrapper::getInstance().SMX_Start(&NullSmxCallback, nullptr);

        return true;
    }

    void SmxStageDevice::SetLightByIndex(size_t index, uint8_t value) {
        const size_t subpixel = index%3;
        const size_t panel = (index/3)%PANEL_COUNT;
        const size_t pad = (index/3)/PANEL_COUNT;

        m_lightDataMutex.lock();
        for(size_t i = 0; i < LEDS_PER_PAD; ++i) {
            m_lightData[(pad*PANEL_COUNT*LEDS_PER_PAD*3)+(panel*LEDS_PER_PAD*3)+(i*3)+subpixel] = value;
        }
        m_lightDataMutex.unlock();
    }

    void SmxStageDevice::Update() {
        m_lightDataMutex.lock();
        SMXWrapper::getInstance().SMX_SetLights2(reinterpret_cast<const char*>(&m_lightData[0]), m_lightData.size());
        m_lightDataMutex.unlock();
    }
}