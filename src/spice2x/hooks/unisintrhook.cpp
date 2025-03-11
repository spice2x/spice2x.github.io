#include "unisintrhook.h"

#include "avs/game.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/libutils.h"

bool CreateInstance() {
    return true;
}

int GetGMT() {

    // fallback to avs clock
    return -1;
}

int GetQRcodeLen(void*) {
    return 0;
}

void GetQRcodeURL(void*, char* dest) {
}

void InitModel(char* model, int) {
}

void InitPlayerCount(int players) {
}

void InitPrivilege(int) {
}

void InitVersion(int unis_ver_major, int unis_ver_minor) {
}

bool IsConnectServer(void) {
    if (avs::game::is_model("KFC") || avs::game::is_model("REC")) {
        return false;
    }
    return true;
}

bool IsInComm(void) {
    return true;
}

bool IsPlayerForbidState(int) {
    return false;
}

void RcfAddCoin(void* callback) {}
void RcfCommFailed(void* callback) {}
void RcfCommSucceed(void* callback) {}
void RcfCountdown(void* callback) {}
void RcfDebugLog(void* callback) {}
void RcfGameSwitch(void* callback) {}
void RcfMachineEffects(void* callback) {}
void RcfMachineMode(void* callback) {}
void RcfOtherInfo(void* callback) {}
void RcfPayoutFailed(void* callback) {}
void RcfPayoutSucceed(void* callback) {}
void RcfPlayerInfo(void* callback) {}
void RcfPrintTicket(void* callback) {}
void RcfPrivilege(void* callback) {}
void RcfRankingResult(void* callback) {}
void RcfRecvComm(void* callback) {}
void RcfRecvPayout(void* callback) {}
void RcfRecvTransp(void* callback) {}
void RcfTranspFailed(void* callback) {}
void RcfTranspSucceed(void* callback) {}
void RcfWinPrize(void* callback) {}

int RefreshPlayerState(int player) {
    return 0;
}

void ReleaseInstance(void) {
}

int SendCoinSignal(int, int, int) {
    return 0;
}

int SendTransp(int, int) {
    return 1;
}

void StartDevice(void) {
}

void unisintrhook_init(void) {

    // check for module
    auto unisintr = libutils::try_module("unisintr.dll");
    if (unisintr != nullptr) {
        log_info("unisintrhook", "attaching...");

        // TODO: SDVX CN doesn't like this...?
        if (!avs::game::is_model("KFC")) {
            detour::iat_try("GetGMT", GetGMT, nullptr, "unisintr.dll");
        }

        // hooks
        detour::iat_try("CreateInstance", CreateInstance, nullptr, "unisintr.dll");
        detour::iat_try("GetQRcodeLen", GetQRcodeLen, nullptr, "unisintr.dll");
        detour::iat_try("GetQRcodeURL", GetQRcodeURL, nullptr, "unisintr.dll");
        detour::iat_try("InitModel", InitModel, nullptr, "unisintr.dll");
        detour::iat_try("InitPlayerCount", InitPlayerCount, nullptr, "unisintr.dll");
        detour::iat_try("InitPrivilege", InitPrivilege, nullptr, "unisintr.dll");
        detour::iat_try("InitVersion", InitVersion, nullptr, "unisintr.dll");
        detour::iat_try("IsConnectServer", IsConnectServer, nullptr, "unisintr.dll");
        detour::iat_try("IsInComm", IsInComm, nullptr, "unisintr.dll");
        detour::iat_try("IsPlayerForbidState", IsPlayerForbidState, nullptr, "unisintr.dll");
        detour::iat_try("RcfAddCoin", RcfAddCoin, nullptr, "unisintr.dll");
        detour::iat_try("RcfCommFailed", RcfCommFailed, nullptr, "unisintr.dll");
        detour::iat_try("RcfCommSucceed", RcfCommSucceed, nullptr, "unisintr.dll");
        detour::iat_try("RcfCountdown", RcfCountdown, nullptr, "unisintr.dll");
        detour::iat_try("RcfDebugLog", RcfDebugLog, nullptr, "unisintr.dll");
        detour::iat_try("RcfGameSwitch", RcfGameSwitch, nullptr, "unisintr.dll");
        detour::iat_try("RcfMachineEffects", RcfMachineEffects, nullptr, "unisintr.dll");
        detour::iat_try("RcfMachineMode", RcfMachineMode, nullptr, "unisintr.dll");
        detour::iat_try("RcfOtherInfo", RcfOtherInfo, nullptr, "unisintr.dll");
        detour::iat_try("RcfPayoutFailed", RcfPayoutFailed, nullptr, "unisintr.dll");
        detour::iat_try("RcfPayoutSucceed", RcfPayoutSucceed, nullptr, "unisintr.dll");
        detour::iat_try("RcfPlayerInfo", RcfPlayerInfo, nullptr, "unisintr.dll");
        detour::iat_try("RcfPrintTicket", RcfPrintTicket, nullptr, "unisintr.dll");
        detour::iat_try("RcfPrivilege", RcfPrivilege, nullptr, "unisintr.dll");
        detour::iat_try("RcfRankingResult", RcfRankingResult, nullptr, "unisintr.dll");
        detour::iat_try("RcfRecvComm", RcfRecvComm, nullptr, "unisintr.dll");
        detour::iat_try("RcfRecvPayout", RcfRecvPayout, nullptr, "unisintr.dll");
        detour::iat_try("RcfRecvTransp", RcfRecvTransp, nullptr, "unisintr.dll");
        detour::iat_try("RcfTranspFailed", RcfTranspFailed, nullptr, "unisintr.dll");
        detour::iat_try("RcfTranspSucceed", RcfTranspSucceed, nullptr, "unisintr.dll");
        detour::iat_try("RcfWinPrize", RcfWinPrize, nullptr, "unisintr.dll");
        detour::iat_try("RefreshPlayerState", RefreshPlayerState, nullptr, "unisintr.dll");
        detour::iat_try("ReleaseInstance", ReleaseInstance, nullptr, "unisintr.dll");
        detour::iat_try("SendCoinSignal", SendCoinSignal, nullptr, "unisintr.dll");
        detour::iat_try("SendTransp", SendTransp, nullptr, "unisintr.dll");
        detour::iat_try("StartDevice", StartDevice, nullptr, "unisintr.dll");

        log_info("unisintrhook", "attached");
    }
}
