#pragma once

#include <string>
#include <vector>

namespace rawinput {
    class RawInputManager;
}

class Light {
private:
    std::vector<Light> alternatives;
    std::string lightName;
    std::string deviceIdentifier = "";
    unsigned int index = 0;

public:
    float last_state = 0.f;

    // overrides
    bool override_enabled = false;
    float override_state = 0.f;

    explicit Light(std::string lightName) : lightName(std::move(lightName)) {};

    std::string getDisplayString(rawinput::RawInputManager* manager);

    inline std::vector<Light> &getAlternatives() {
        return this->alternatives;
    }

    inline bool isSet() const {
        if (this->override_enabled) {
            return true;
        }
        if (!this->deviceIdentifier.empty()) {
            return true;
        }

        for (auto &alternative : this->alternatives) {
            if (!alternative.deviceIdentifier.empty()) {
                return true;
            }
        }

        return false;
    }

    inline const std::string &getName() const {
        return this->lightName;
    }

    inline const std::string &getDeviceIdentifier() const {
        return this->deviceIdentifier;
    }

    inline void setDeviceIdentifier(std::string deviceIdentifier) {
        this->deviceIdentifier = std::move(deviceIdentifier);
    }

    inline unsigned int getIndex() const {
        return this->index;
    }

    inline void setIndex(unsigned int index) {
        this->index = index;
    }
};
