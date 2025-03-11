#pragma once

#include <windows.h>
#include <unknwn.h>

enum NUI_IMAGE_TYPE {
    NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX = 0x0,
    NUI_IMAGE_TYPE_COLOR = 0x1,
    NUI_IMAGE_TYPE_COLOR_YUV = 0x2,
    NUI_IMAGE_TYPE_COLOR_RAW_YUV = 0x3,
    NUI_IMAGE_TYPE_DEPTH = 0x4,
    NUI_IMAGE_TYPE_COLOR_INFRARED = 0x5,
    NUI_IMAGE_TYPE_COLOR_RAW_BAYER = 0x6,
};

enum NUI_IMAGE_RESOLUTION {
    NUI_IMAGE_RESOLUTION_80x60 = 0x0,
    NUI_IMAGE_RESOLUTION_320x240 = 0x1,
    NUI_IMAGE_RESOLUTION_640x480 = 0x2,
    NUI_IMAGE_RESOLUTION_1280x960 = 0x3,
    NUI_IMAGE_RESOLUTION_INVALID = 0xFFFFFFFF,
};

#pragma pack(push, 8)
struct INuiSensor {
    struct INuiSensorVtbl *lpVtbl;
};

struct INuiSensorVtbl {
    HRESULT (__stdcall *QueryInterface)(IUnknown *This, const IID *const riid, void **ppvObject);
    ULONG (__stdcall *AddRef)(IUnknown *This);
    ULONG (__stdcall *Release)(IUnknown *This);

    HRESULT (__stdcall *NuiInitialize)(
        INuiSensor *This,
        DWORD dwFlags);

    void (__stdcall *NuiShutdown)(INuiSensor *This);

    HRESULT (__stdcall *NuiSetFrameEndEvent)(
        INuiSensor *This,
        HANDLE hEvent,
        DWORD dwFrameEventFlag);

    HRESULT (__stdcall *NuiImageStreamOpen)(
        INuiSensor *This,
        NUI_IMAGE_TYPE eImageType,
        NUI_IMAGE_RESOLUTION eResolution,
        DWORD dwImageFrameFlags,
        DWORD dwFrameLimit,
        HANDLE hNextFrameEvent,
        HANDLE *phStreamHandle);

    HRESULT (__stdcall *NuiImageStreamSetImageFrameFlags)(
        INuiSensor *This,
        HANDLE hStream,
        DWORD dwImageFrameFlags);

    HRESULT (__stdcall *NuiImageStreamGetImageFrameFlags)(
        INuiSensor *This,
        HANDLE hStream,
        DWORD *pdwImageFrameFlags);

    HRESULT (__stdcall *NuiImageStreamGetNextFrame)(
        INuiSensor *This,
        HANDLE hStream,
        DWORD dwMillisecondsToWait,
        void *pImageFrame);

    HRESULT (__stdcall *NuiImageStreamReleaseFrame)(
        INuiSensor *This,
        HANDLE hStream,
        void *pImageFrame);

    HRESULT (__stdcall *NuiImageGetColorPixelCoordinatesFromDepthPixel)(
        INuiSensor *This,
        DWORD eColorResolution,
        const void *pcViewArea,
        LONG lDepthX,
        LONG lDepthY,
        USHORT usDepthValue,
        LONG *plColorX,
        LONG *plColorY);

    HRESULT (__stdcall *NuiImageGetColorPixelCoordinatesFromDepthPixelAtResolution)(
        INuiSensor *This,
        DWORD eColorResolution,
        DWORD eDepthResolution,
        const void *pcViewArea,
        LONG lDepthX,
        LONG lDepthY,
        USHORT usDepthValue,
        LONG *plColorX,
        LONG *plColorY);

    HRESULT (__stdcall *NuiImageGetColorPixelCoordinateFrameFromDepthPixelFrameAtResolution)(
        INuiSensor *This,
        DWORD eColorResolution,
        DWORD eDepthResolution,
        DWORD cDepthValues,
        USHORT *pDepthValues,
        DWORD cColorCoordinates,
        LONG *pColorCoordinates);

    HRESULT (__stdcall *NuiCameraElevationSetAngle)(
        INuiSensor *This,
        LONG lAngleDegrees);

    HRESULT (__stdcall *NuiCameraElevationGetAngle)(
        INuiSensor *This,
        LONG *plAngleDegrees);

    HRESULT (__stdcall *NuiSkeletonTrackingEnable)(
        INuiSensor *This,
        HANDLE hNextFrameEvent,
        DWORD dwFlags);

    HRESULT (__stdcall *NuiSkeletonTrackingDisable)(
        INuiSensor *This);

    HRESULT (__stdcall *NuiSkeletonSetTrackedSkeletons)(
        INuiSensor *This,
        DWORD *TrackingIDs);

    HRESULT (__stdcall *NuiSkeletonGetNextFrame)(
        INuiSensor *This,
        DWORD dwMillisecondsToWait,
        void *pSkeletonFrame);

    HRESULT (__stdcall *NuiTransformSmooth)(
        INuiSensor *This,
        void *pSkeletonFrame,
        const void *pSmoothingParams);

    HRESULT (__stdcall *NuiGetAudioSource)(
        INuiSensor *This,
        void **ppDmo);

    int (__stdcall *NuiInstanceIndex)(
        INuiSensor *This);

    BSTR (__stdcall *NuiDeviceConnectionId)(
        INuiSensor *This);

    BSTR (__stdcall *NuiUniqueId)(
        INuiSensor *This);

    BSTR (__stdcall *NuiAudioArrayId)(
        INuiSensor *This);

    HRESULT (__stdcall *NuiStatus)(
        INuiSensor *This);

    DWORD (__stdcall *NuiInitializationFlags)(
        INuiSensor *This);

    HRESULT (__stdcall *NuiGetCoordinateMapper)(
        INuiSensor *This,
        void **pMapping);

    HRESULT (__stdcall *NuiImageFrameGetDepthImagePixelFrameTexture)(
        INuiSensor *This,
        HANDLE hStream,
        void *pImageFrame,
        BOOL *pNearMode,
        void **ppFrameTexture);

    HRESULT (__stdcall *NuiGetColorCameraSettings)(
        INuiSensor *This,
        void **pCameraSettings);

    BOOL (__stdcall *NuiGetForceInfraredEmitterOff)(
        INuiSensor *This);

    HRESULT (__stdcall *NuiSetForceInfraredEmitterOff)(
        INuiSensor *This,
        BOOL fForceInfraredEmitterOff);

    HRESULT (__stdcall *NuiAccelerometerGetCurrentReading)(
        INuiSensor *This,
        void *pReading);
};
#pragma pack(pop)
