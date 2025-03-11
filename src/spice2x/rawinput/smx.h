#pragma once

#include <thread>
#include <windows.h>
#include <setupapi.h>
#include <string>
#include "util/libutils.h"
#include "util/logging.h"
#include "device.h"

enum SMXUpdateCallbackReason {
    // This is called when a generic state change happens: connection or disconnection, inputs changed,
    // test data updated, etc.  It doesn't specify what's changed.  We simply check the whole state.
    SMXUpdateCallback_Updated,

    // This is called when SMX_FactoryReset completes, indicating that SMX_GetConfig will now return
    // the reset configuration.
    SMXUpdateCallback_FactoryResetCommandComplete
};

enum SMXDedicatedCabinetLights {
    MARQUEE = 0,
    LEFT_STRIP = 1,
    LEFT_SPOTLIGHTS = 2,
    RIGHT_STRIP = 3,
    RIGHT_SPOTLIGHTS = 4
};


typedef void SMXUpdateCallback(int pad, SMXUpdateCallbackReason reason, void *pUser);

/**
 * Wrapper for the StepManiaX SDK DLL, so we can use its functionality without having to import the source
 * of the SDK wholesale.
 */
class SMXWrapper {
public:
    static SMXWrapper& getInstance() {
        static SMXWrapper instance;
        return instance;
    }
    ~SMXWrapper();
    void SMX_Start(SMXUpdateCallback UpdateCallback, void *pUser);
    void SMX_SetLights2(const char *lightData, int lightDataSize);
    void SMX_SetDedicatedCabinetLights(SMXDedicatedCabinetLights lightDevice, const char* lightData, int lightDataSize);
private:
    SMXWrapper();
};
