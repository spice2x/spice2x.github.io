#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>

#pragma once

namespace games::iidx {
    void init_mf_library();

    HRESULT WrappedMFCreateAttributes (
        _Out_ IMFAttributes** ppMFAttributes,
        _In_ UINT32 cInitialSize);

    HRESULT WrappedMFEnumDeviceSources (
        _In_ IMFAttributes* pAttributes,
        _Outptr_result_buffer_(*pcSourceActivate) IMFActivate*** pppSourceActivate,
        _Out_ UINT32* pcSourceActivate);

    HRESULT WrappedMFCreateSourceReaderFromMediaSource (
        _In_ IMFMediaSource *pMediaSource,
        _In_opt_ IMFAttributes *pAttributes,
        _Out_ IMFSourceReader **ppSourceReader);

    HRESULT WrappedMFGetService (
        IUnknown* punkObject,
        REFGUID guidService,
        REFIID riid,
        _Outptr_ LPVOID* ppvObject);
}