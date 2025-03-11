#include "mf_wrappers.h"
#include "util/libutils.h"
#include "util/logging.h"

namespace games::iidx {

    static bool INITIALIZED = false;

    static HMODULE mf_dll = nullptr;
    static HMODULE mfreadwrite_dll = nullptr;
    static HMODULE mfplat_dll = nullptr;

    typedef HRESULT (__stdcall * MFCreateAttributes_t)(
        _Out_ IMFAttributes** ppMFAttributes,
        _In_ UINT32 cInitialSize
        );

    typedef HRESULT (__stdcall * MFEnumDeviceSources_t)(
        _In_ IMFAttributes* pAttributes,
        _Outptr_result_buffer_(*pcSourceActivate) IMFActivate*** pppSourceActivate,
        _Out_ UINT32* pcSourceActivate
        );

    typedef HRESULT (__stdcall * MFCreateSourceReaderFromMediaSource_t)(
        _In_ IMFMediaSource *pMediaSource,
        _In_opt_ IMFAttributes *pAttributes,
        _Out_ IMFSourceReader **ppSourceReader
        );

    typedef HRESULT (__stdcall * MFGetService_t)(
        IUnknown* punkObject,
        REFGUID guidService,
        REFIID riid,
        _Outptr_ LPVOID* ppvObject
        );

    static MFCreateAttributes_t MFCreateAttributes = nullptr;
    static MFEnumDeviceSources_t MFEnumDeviceSources = nullptr;
    static MFCreateSourceReaderFromMediaSource_t MFCreateSourceReaderFromMediaSource = nullptr;
    static MFGetService_t MFGetService = nullptr;

    void init_mf_library() {

        // why was all of this needed?
        //
        // when iidx camhook was initially implemented, we linked to mf.lib, mfreadwrite.lib, and mfplat.lib
        // this made Unity-based really unhappy, causing them to skip over the logic that loads mf library
        // causing videos to not play ("Initializing Microsoft Media Foundation failed." in the cmd prompt)
        //
        // as a result, the static linking to mf libs were removed, and we are now doing the mess that is this file

        if (INITIALIZED) {
            return;
        }
        INITIALIZED = true;

        log_misc("mf_wrappers", "creating delay-loaded wrappers for MF routines - BEGIN");

        mf_dll = libutils::load_library("mf.dll", true);
        mfreadwrite_dll = libutils::load_library("mfreadwrite.dll", true);
        mfplat_dll = libutils::load_library("mfplat.dll", true);

        MFCreateAttributes = (MFCreateAttributes_t)
            libutils::get_proc(mfplat_dll, "MFCreateAttributes");
        if (!MFCreateAttributes) {
            log_fatal("mf_wrappers", "MFCreateAttributes failed to hook");
        }

        MFEnumDeviceSources = (MFEnumDeviceSources_t)
            libutils::get_proc(mf_dll, "MFEnumDeviceSources");
        if (!MFEnumDeviceSources) {
            log_fatal("mf_wrappers", "MFEnumDeviceSources failed to hook");
        }

        MFCreateSourceReaderFromMediaSource = (MFCreateSourceReaderFromMediaSource_t)
            libutils::get_proc(mfreadwrite_dll, "MFCreateSourceReaderFromMediaSource");
        if (!MFCreateSourceReaderFromMediaSource) {
            log_fatal("mf_wrappers", "MFCreateSourceReaderFromMediaSource failed to hook");
        }

        MFGetService = (MFGetService_t)libutils::get_proc(mf_dll, "MFGetService");
        if (!MFGetService) {
            log_fatal("mf_wrappers", "MFGetService failed to hook");
        }

        log_misc("mf_wrappers", "creating delay-loaded wrappers for MF routines - DONE");
    }

    HRESULT WrappedMFCreateAttributes (
        _Out_ IMFAttributes** ppMFAttributes,
        _In_ UINT32 cInitialSize) {
        return MFCreateAttributes(ppMFAttributes, cInitialSize);
    }

    HRESULT WrappedMFEnumDeviceSources (
        _In_ IMFAttributes* pAttributes,
        _Outptr_result_buffer_(*pcSourceActivate) IMFActivate*** pppSourceActivate,
        _Out_ UINT32* pcSourceActivate) {
        return MFEnumDeviceSources(pAttributes, pppSourceActivate, pcSourceActivate);
    }

    HRESULT WrappedMFCreateSourceReaderFromMediaSource (
        _In_ IMFMediaSource *pMediaSource,
        _In_opt_ IMFAttributes *pAttributes,
        _Out_ IMFSourceReader **ppSourceReader) {
        return MFCreateSourceReaderFromMediaSource(pMediaSource, pAttributes, ppSourceReader);
    }

    HRESULT WrappedMFGetService (
        IUnknown* punkObject,
        REFGUID guidService,
        REFIID riid,
        _Outptr_ LPVOID* ppvObject) {
        return MFGetService(punkObject, guidService, riid, ppvObject);
    }
}