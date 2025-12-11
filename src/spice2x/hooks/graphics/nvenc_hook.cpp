#include <d3d9.h>

#include <optional>

#include "avs/game.h"
#include "external/nvenc/nvEncodeAPI.h"
#include "hooks/libraryhook.h"
#include "hooks/graphics/backends/d3d9/d3d9_device.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

#include "nvenc_hook.h"

#ifdef SPICE64

typedef NVENCSTATUS(NVENCAPI *NvEncodeAPICreateInstance_Type)(NV_ENCODE_API_FUNCTION_LIST*);
static NvEncodeAPICreateInstance_Type NvEncodeAPICreateInstance_orig = nullptr;

static PNVENCOPENENCODESESSIONEX nvEncOpenEncodeSessionEx_orig = nullptr;
static PNVENCINITIALIZEENCODER nvEncInitializeEncoder_orig = nullptr;
static PNVENCGETENCODEPRESETCONFIG nvEncGetEncodePresetConfig_orig = nullptr;
static PNVENCGETENCODEPRESETCONFIGEX nvEncGetEncodePresetConfigEx_orig = nullptr;
static BOOL initialized = false;

namespace nvenc_hook {

    std::optional<std::string> VIDEO_CQP_STRING_OVERRIDE;
    std::optional<NV_ENC_QP> VIDEO_CQP_OVERRIDE;

    void parse_qcp_params();

    NVENCSTATUS NVENCAPI nvEncOpenEncodeSessionEx_hook(
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *openSessionExParams, 
        void **encoder
    ) {
        WrappedIDirect3DDevice9 *wrappedDevice;
        try {
            wrappedDevice = (WrappedIDirect3DDevice9*)openSessionExParams->device;
            // log_misc("nvenc_hook", 
            //     "nvEncOpenEncodeSessionEx hook hit (wrapped: {}) (real: {})", 
            //     fmt::ptr(wrappedDevice), 
            //     fmt::ptr(wrappedDevice->pReal)
            // );
            openSessionExParams->device = wrappedDevice->pReal;
        } catch (const std::exception &ex) {}
        return nvEncOpenEncodeSessionEx_orig(openSessionExParams, encoder);
    }

    NVENCSTATUS NVENCAPI nvEncInitializeEncoder_hook(
        void* encoder,
        NV_ENC_INITIALIZE_PARAMS* createEncodeParams
    ) {
        try {
            // check input params
            if (createEncodeParams == nullptr || createEncodeParams->encodeConfig == nullptr) {
                goto done;
            }

            log_misc("nvenc_hook", "nvEncInitializeEncoder called with NV_ENC_INITIALIZE_PARAMS:");
            log_misc("nvenc_hook", "  version: {:x}", createEncodeParams->version);
            log_misc("nvenc_hook", "  encodeGUID: {}", guid2s(createEncodeParams->encodeGUID));
            log_misc("nvenc_hook", "  presetGUID: {}", guid2s(createEncodeParams->presetGUID));
            log_misc("nvenc_hook", "  encodeWidth: {}", createEncodeParams->encodeWidth);
            log_misc("nvenc_hook", "  encodeHeight: {}", createEncodeParams->encodeHeight);
            log_misc("nvenc_hook", "  darWidth: {}", createEncodeParams->darWidth);
            log_misc("nvenc_hook", "  darHeight: {}", createEncodeParams->darHeight);
            log_misc("nvenc_hook", "  frameRateNum: {}", createEncodeParams->frameRateNum);
            log_misc("nvenc_hook", "  frameRateDen: {}", createEncodeParams->frameRateDen);
            log_misc("nvenc_hook", "  enableEncodeAsync: {}", createEncodeParams->enableEncodeAsync);
            log_misc("nvenc_hook", "  enablePTD: {}", createEncodeParams->enablePTD);
            log_misc("nvenc_hook", "  maxEncodeWidth: {}", createEncodeParams->maxEncodeWidth);
            log_misc("nvenc_hook", "  maxEncodeHeight: {}", createEncodeParams->maxEncodeHeight);
            log_misc("nvenc_hook", "  tuningInfo: {}", static_cast<uint32_t>(createEncodeParams->tuningInfo));
            log_misc("nvenc_hook", "  bufferFormat: {}", static_cast<uint32_t>(createEncodeParams->bufferFormat));
            log_misc("nvenc_hook", "  encodeConfig.version: {:x}", createEncodeParams->encodeConfig->version);
            log_misc("nvenc_hook", "  encodeConfig.profileGUID: {}", guid2s(createEncodeParams->encodeConfig->profileGUID));
            log_misc("nvenc_hook", "  encodeConfig.gopLength: {}", createEncodeParams->encodeConfig->gopLength);
            log_misc("nvenc_hook", "  encodeConfig.frameIntervalP: {}", createEncodeParams->encodeConfig->frameIntervalP);
            log_misc("nvenc_hook", "  encodeConfig.monoChromeEncoding: {}", createEncodeParams->encodeConfig->monoChromeEncoding);

            const auto h264 = &createEncodeParams->encodeConfig->encodeCodecConfig.h264Config;
            log_misc("nvenc_hook", "  encodeConfig.encodeCodecConfig.h264Config.sliceMode: {}", h264->sliceMode);
            log_misc("nvenc_hook", "  encodeConfig.encodeCodecConfig.h264Config.sliceModeData: {}", h264->sliceModeData);

            const auto rc = &createEncodeParams->encodeConfig->rcParams;
            log_misc("nvenc_hook", "  encodeConfig.rcParams.version: {:x}", rc->version);
            log_misc("nvenc_hook", "  encodeConfig.rcParams.rateControlMode: {}", static_cast<uint32_t>(rc->rateControlMode));
            if (rc->rateControlMode != NV_ENC_PARAMS_RC_CONSTQP) {
                log_warning(
                    "nvenc_hook",
                    "nvEncInitializeEncoder: unexpected rateControlMode, expected: NV_ENC_PARAMS_RC_CONSTQP, actual: {}",
                    rc->rateControlMode);
                goto done;
            }

            log_misc("nvenc_hook", "nvEncInitializeEncoder hook hit with expected params");

            // print out most relevant video quality settings
            // note: NvEncoder.cpp sample uses {28, 31, 25} (and that's what some hex edits modify)
            //       but bm2dx later adds 8 to each value, before calling this routine
            log_misc(
                "nvenc_hook", "nvEncInitializeEncoder: original constQP p={}, b={}, i={}",
                rc->constQP.qpInterP, rc->constQP.qpInterB, rc->constQP.qpIntra);

            // video quality override
            if (VIDEO_CQP_OVERRIDE.has_value()) {
                rc->constQP = VIDEO_CQP_OVERRIDE.value();
                log_misc(
                    "nvenc_hook", "nvEncInitializeEncoder: user overriden constQP p={}, b={}, i={}",
                    rc->constQP.qpInterP, rc->constQP.qpInterB, rc->constQP.qpIntra);
            }

        } catch (const std::exception &ex) {}

    done:
        const auto status = nvEncInitializeEncoder_orig(encoder, createEncodeParams);
        log_misc(
            "nvenc_hook",
            "nvEncInitializeEncoder returned 0x{:x}",
            static_cast<uint32_t>(status));

        return status;
    }

    NVENCSTATUS NVENCAPI nvEncGetEncodePresetConfig_hook (
        void* encoder, GUID encodeGUID, GUID presetGUID, NV_ENC_PRESET_CONFIG* presetConfig) {

        const auto status = nvEncGetEncodePresetConfig_orig(
            encoder, encodeGUID, presetGUID, presetConfig);

        log_misc(
            "nvenc_hook",
            "NvEncGetEncodePresetConfig called with encodeGUID = {}, presetGUID = {}. and returned 0x{:x}",
            guid2s(encodeGUID),
            guid2s(presetGUID),
            static_cast<uint32_t>(status));

        return status;
    }

    NVENCSTATUS NVENCAPI NvEncodeAPICreateInstance_hook(NV_ENCODE_API_FUNCTION_LIST *pFunctionList) {        
        // log_misc("nvenc_hook", "NvEncodeAPICreateInstance hook hit");
        auto status = NvEncodeAPICreateInstance_orig(pFunctionList);

        // The game will call NvEncodeAPICreateInstance multiple times
        // Using a flag to avoid creating trampoline repeatedly
        if (!initialized) {
            // hook functions
            detour::trampoline_try(
                pFunctionList->nvEncOpenEncodeSessionEx,
                nvEncOpenEncodeSessionEx_hook,
                &nvEncOpenEncodeSessionEx_orig);
            detour::trampoline_try(
                pFunctionList->nvEncInitializeEncoder,
                nvEncInitializeEncoder_hook,
                &nvEncInitializeEncoder_orig);
            detour::trampoline_try(
                pFunctionList->nvEncGetEncodePresetConfig,
                nvEncGetEncodePresetConfig_hook,
                &nvEncGetEncodePresetConfig_orig);
            nvEncGetEncodePresetConfigEx_orig = pFunctionList->nvEncGetEncodePresetConfigEx;
            log_misc("nvenc_hook", "NvEncodeAPICreateInstance_hook called and functions hooked");
            initialized = true;
        }

        return status;
    }

    void initialize() {
        HMODULE nvenc = libutils::try_library("nvEncodeAPI64.dll");
        if (nvenc == nullptr) {
            return;
        }
        bool success = detour::trampoline_try(
            (NvEncodeAPICreateInstance_Type)libutils::try_proc(nvenc, "NvEncodeAPICreateInstance"), 
            NvEncodeAPICreateInstance_hook, 
            &NvEncodeAPICreateInstance_orig
            );
        if (success) {
            log_misc("nvenc_hook", "created hook for NvEncodeAPICreateInstance");
        } else {
            log_warning("nvenc_hook", "failed to hook NvEncodeAPICreateInstance");
        }

        parse_qcp_params();
    }

    void parse_qcp_params() {
        if (!VIDEO_CQP_STRING_OVERRIDE.has_value() || VIDEO_CQP_STRING_OVERRIDE.value().empty()) {
            return;
        }
        const auto remove_spaces = [](const char& c) {
            return c == ' ';
        };

        auto s = VIDEO_CQP_STRING_OVERRIDE.value();
        s.erase(std::remove_if(s.begin(), s.end(), remove_spaces), s.end());

        int cqp = -1;
        NV_ENC_QP nvenc_qp;

        // try 3-param format first, and then fall back to single parameter format
        if (sscanf(
                s.c_str(),
                "%i,%i,%i",
                (int*)&(nvenc_qp.qpInterP),
                (int*)&(nvenc_qp.qpInterB),
                (int*)&(nvenc_qp.qpIntra)) == 3) {

            VIDEO_CQP_OVERRIDE = nvenc_qp;
        } else if (sscanf(s.c_str(), "%i", &cqp) == 1) {
            nvenc_qp.qpInterP = cqp;
            nvenc_qp.qpInterB = cqp;
            nvenc_qp.qpIntra = cqp;
            VIDEO_CQP_OVERRIDE = nvenc_qp;
        } else {
            log_fatal("nvenc_hook", "failed to parse -iidxreccqp");
        }

        log_info("nvenc_hook", "parsed NVENC ConstQP params: p={}, b={}, i={} (-iidxreccqp)",
            nvenc_qp.qpInterP, nvenc_qp.qpInterB, nvenc_qp.qpIntra);
    }
}

#else

namespace nvenc_hook {
    void initialize() {
        return;
    }
}

#endif
 