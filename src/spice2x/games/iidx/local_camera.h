#pragma once

#if SPICE64

#include <d3d9.h>
#include <dxva2api.h>
#include <mutex>
#include <mferror.h>
#include <string>
#include <thread>
#include <strmif.h>
#include <vector>
#include "mf_wrappers.h"

#define CAMERA_CONTROL_PROP_SIZE 7
#define DRAW_MODE_SIZE 5

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

typedef void (*IMAGE_TRANSFORM_FN)(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
);

struct CameraControlProp {
    long minValue = 0;
    long maxValue = 0;
    long delta = 0;
    long defaultValue = 0;
    long defFlags = 0;
    long value = 0;
    long valueFlags = 0;
};

struct MediaTypeInfo {
    GUID            subtype = GUID_NULL;
    UINT32          width = 0;
    UINT32          height = 0;
    double          frameRate = 0.0;
    IMFMediaType*   p_mediaType = nullptr;
    std::string     description = "";
    LONG            *plStride = nullptr;
};

typedef enum {
    DrawModeStretch = 0,
    DrawModeCrop = 1,
    DrawModeLetterbox = 2,
    DrawModeCrop4_3 = 3,
    DrawModeLetterbox4_3 = 4,
} LocalCameraDrawMode;

extern std::string CAMERA_CONTROL_LABELS[];

extern std::string DRAW_MODE_LABELS[];

namespace games::iidx {
    namespace Camera {
        struct PlayVideoCamera {
            IDirect3DTexture9** d3d9_texture(const uintptr_t offset) {
                auto const afp_texture = *reinterpret_cast<uint8_t**>
                    (reinterpret_cast<uint8_t*>(this) + offset);
                return reinterpret_cast<IDirect3DTexture9**>(afp_texture + 0x8);
            }
        };

        struct CCameraManager2 {
            using camera_pointers = struct {
                PlayVideoCamera* a;
                PlayVideoCamera* b;
            };
            void* vftbl;
            camera_pointers* cameras;
        };
    }

    class IIDXLocalCamera {
    protected:
        virtual ~IIDXLocalCamera() {};

        ULONG m_nRefCount;
        CRITICAL_SECTION m_critsec;

        std::string m_name;
        std::string m_friendly_name;
        BOOL m_prefer_16_by_9;
        WCHAR *m_pwszSymbolicLink = nullptr;
        UINT32 m_cchSymbolicLink = 0;

        // For reading frames from Camera
        IMFMediaSource *m_pSource = nullptr;
        IMFSourceReader *m_pSourceReader = nullptr;

        // Camera Format information
        double                  m_frameRate = 0;
        LONG                    m_cameraWidth;
        LONG                    m_cameraHeight;

        // Draw rectangles
        RECT                    m_rcSource;
        RECT                    m_rcDest;

        // Thread to draw texture asynchorously
        std::thread             *m_drawThread = nullptr;

        // DirectX9 DeviceEx
        LPDIRECT3DDEVICE9EX m_device;

        // Address to hook camera textures onto the game
        LPDIRECT3DTEXTURE9 *m_camera_texture_target = nullptr;
        LPDIRECT3DTEXTURE9 *m_preview_texture_target = nullptr;

        // Target texture (to be shown in the game)
        LPDIRECT3DTEXTURE9 m_texture = nullptr;
        IDirect3DSurface9 *m_pDestSurf = nullptr;

        // Texture for color space conversion
        LPDIRECT3DTEXTURE9 m_conversionTexture = nullptr;
        IDirect3DSurface9 *m_pConversionSurf = nullptr;

        // Texture for custom transform (e.g. horizontal flip)
        LPDIRECT3DTEXTURE9 m_transformTexture = nullptr;
        IDirect3DSurface9 *m_pTransformSurf = nullptr;

        // Texture to store "transformed" camera content
        LPDIRECT3DTEXTURE9 m_transformResultTexture = nullptr;
        IDirect3DSurface9 *m_pTransformResultSurf = nullptr;

        // Storing original textures for clean up
        LPDIRECT3DTEXTURE9 m_camera_texture_original = nullptr;
        LPDIRECT3DTEXTURE9 m_preview_texture_original = nullptr;

        IAMCameraControl *m_pCameraControl = nullptr;

        // Camera Control
        std::vector<CameraControlProp> m_controlProps = {};

        BOOL m_controlOptionsInitialized = false;

    public:
        // True if first part of the setup steps (those in the constructor) succeeded
        BOOL m_initialized = false;

        // True if all the setup steps succeeded
        BOOL m_active = false;

        // Media type select
        std::vector<MediaTypeInfo> m_mediaTypeInfos = {};
        int m_selectedMediaTypeIndex = 0;
        bool m_useAutoMediaType = true;
        IMFMediaType *m_pAutoMediaType = nullptr;
        std::string m_selectedMediaTypeDescription = "";
        bool m_allowManualControl = false;

        LocalCameraDrawMode m_drawMode = DrawModeCrop4_3;

        // Render processing
        bool m_flipHorizontal = false;
        bool m_flipVertical = false;

        IIDXLocalCamera(
            std::string name,
            BOOL prefer_16_by_9,
            IMFActivate *pActivate,
            IDirect3DDeviceManager9 *pD3DManager,
            LPDIRECT3DDEVICE9EX device,
            LPDIRECT3DTEXTURE9 *camera_texture_target,
            LPDIRECT3DTEXTURE9 *preview_texture_target
        );
        LPDIRECT3DTEXTURE9 GetTexture();
        ULONG Release();
        IAMCameraControl* GetCameraControl();
        HRESULT GetCameraControlProp(int index, CameraControlProp *pProp);
        HRESULT SetCameraControlProp(int index, long value, long flags);
        HRESULT ResetCameraControlProps();
        HRESULT FlushDrawCommands();
        std::string GetName();
        std::string GetFriendlyName();
        std::string GetSymLink();
        HRESULT ChangeMediaType(IMFMediaType *pType);
        HRESULT StartCapture();
        void UpdateDrawRect();

    private:
        HRESULT CreateD3DDeviceManager();
        void CreateThread();
        MediaTypeInfo GetMediaTypeInfo(IMFMediaType *pType);
        std::string GetVideoFormatName(GUID subtype);
        HRESULT TryMediaType(IMFMediaType *pType, UINT32 *pBestWidth, double *pBestFrameRate);
        HRESULT InitTargetTexture();
        HRESULT InitCameraControl();
        HRESULT DrawSample(IMFMediaBuffer *pSrcBuffer);
        HRESULT ReadSample();
        LPDIRECT3DTEXTURE9 Render();
    };
}

#endif
