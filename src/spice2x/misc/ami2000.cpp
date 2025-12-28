#include "ami2000.h"
#include "util/utils.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "misc/eamuse.h"
#include "acio/icca/icca.h"


static int CARD_TYPE = 0;
static uint8_t CARD_UID[8];

static void __fastcall AMI2000_Initialize() {
    log_info("ami2000", "ami2000 initialize.");
}

static void __fastcall AMI2000_Startup() {
    log_info("ami2000", "ami2000 startup.");
}

static void __fastcall AMI2000_Cleanup() {
    log_info("ami2000", "ami2000 cleanup.");
}

static int __fastcall AMI2000_IsDeviceError() {
    return 0;
}

static int __fastcall AMI2000_ReadCtrl() {
    return 0;
}

static const char *__fastcall AMI2000_GetRomVersion() {
    return "DUMMY";
}

static int __fastcall AMI2000_GetCardType() {
    return CARD_TYPE;
}

static uint64_t __fastcall AMI2000_GetCardIdentifier() {
    if (eamuse_card_insert_consume(1, 0)) {
        eamuse_get_card(1, 0, CARD_UID);
        CARD_TYPE = is_card_uid_felica(CARD_UID) ? 2 : 1;
        log_info("ami2000", "AMI2000_GetCardIdentifier: {:X}", __builtin_bswap64(*(uint64_t*)CARD_UID));
    }
    else {
        CARD_TYPE = 0;
        return 0;
    }
    return __builtin_bswap64(*(uint64_t *)CARD_UID);
}


void ami2000_attach() {
    auto ami2000 = libutils::try_module("iccr-ami2000-api.dll");
    
    if (!ami2000) {
        log_warning("ami2000", "module not found, skipping.");
        return;
    }

    // card unit
    detour::inline_hook((void *)AMI2000_Initialize, libutils::try_proc(
            ami2000, "AMI2000_Initialize"));
    detour::inline_hook((void *)AMI2000_Startup, libutils::try_proc(
            ami2000, "AMI2000_Startup"));
    detour::inline_hook((void *)AMI2000_IsDeviceError, libutils::try_proc(
            ami2000, "AMI2000_IsDeviceError"));
    detour::inline_hook((void *)AMI2000_ReadCtrl, libutils::try_proc(
            ami2000, "AMI2000_ReadCtrl"));
    detour::inline_hook((void *)AMI2000_Cleanup, libutils::try_proc(
            ami2000, "AMI2000_Cleanup"));
    detour::inline_hook((void *)AMI2000_GetRomVersion, libutils::try_proc(
            ami2000, "AMI2000_GetRomVersion"));
    detour::inline_hook((void *)AMI2000_GetCardType, libutils::try_proc(
            ami2000, "AMI2000_GetCardType"));
    detour::inline_hook((void *)AMI2000_GetCardIdentifier, libutils::try_proc(
            ami2000, "AMI2000_GetCardIdentifier"));
            
    log_info("ami2000", "attached");
}

void ami2000_detach()
{
    
}
