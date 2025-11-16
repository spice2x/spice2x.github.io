#include "games/iidx/local_camera.h"

#if SPICE64

#include "util/logging.h"
#include "util/utils.h"
#include "mf_wrappers.h"

std::string CAMERA_CONTROL_LABELS[] = {
    "Pan",
    "Tilt",
    "Roll",
    "Zoom",
    "Exposure",
    "Iris",
    "Focus"
};

std::string DRAW_MODE_LABELS[] = {
    "Stretch",
    "Crop",
    "Letterbox",
    "Crop to 4:3",
    "Letterbox to 4:3",
};

// static HRESULT printTextureLevelDesc(LPDIRECT3DTEXTURE9 texture) {
//     HRESULT hr = S_OK;
//     D3DSURFACE_DESC desc;
//     hr = texture->GetLevelDesc(0, &desc);
//     log_info("iidx:camhook", "Texture Desc Size: {}x{} Res Type: {} Format: {} Usage: {}", desc.Width, desc.Height, (int) desc.Type, (int) desc.Format, (int) desc.Usage);
//     return hr;
// }

LONG TARGET_SURFACE_WIDTH = 1280;
LONG TARGET_SURFACE_HEIGHT = 720;

double RATIO_16_9 = 16.0 / 9.0;
double RATIO_4_3 = 4.0 / 3.0;

namespace games::iidx {

    IIDXLocalCamera::IIDXLocalCamera(
        std::string name,
        BOOL prefer_16_by_9,
        IMFActivate *pActivate,
        IDirect3DDeviceManager9 *pD3DManager,
        LPDIRECT3DDEVICE9EX device,
        LPDIRECT3DTEXTURE9 *camera_texture_target,
        LPDIRECT3DTEXTURE9 *preview_texture_target
    ):
        m_nRefCount(1),
        m_name(name),
        m_prefer_16_by_9(prefer_16_by_9),
        m_device(device),
        m_camera_texture_target(camera_texture_target),
        m_preview_texture_target(preview_texture_target),
        m_camera_texture_original(*camera_texture_target),
        m_preview_texture_original(*preview_texture_target)
    {
        InitializeCriticalSection(&m_critsec);

        HRESULT hr = S_OK;
        IMFAttributes *pAttributes = nullptr;

        EnterCriticalSection(&m_critsec);

        log_info("iidx:camhook", "[{}] Creating camera", m_name);

        // Get friendly name for log purposes
        WCHAR *pwszFriendlyName = nullptr;
        UINT32 m_cchFriendlyName = 0;
        hr = pActivate->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &pwszFriendlyName,
            &m_cchFriendlyName
        );
        if (SUCCEEDED(hr) && pwszFriendlyName != nullptr) {
            log_misc("iidx:camhook", "[{}] Name: {}", m_name, ws2s(pwszFriendlyName));
            m_friendly_name = ws2s(pwszFriendlyName);
            CoTaskMemFree(pwszFriendlyName);
            pwszFriendlyName = nullptr;
        }

        // Retrive symlink of Camera for control configurations
        hr = pActivate->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &m_pwszSymbolicLink,
            &m_cchSymbolicLink
        );
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] GetAllocatedString failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        log_misc("iidx:camhook", "[{}] Symlink: {}", m_name, GetSymLink());

        // Create the media source object.
        hr = pActivate->ActivateObject(IID_PPV_ARGS(&m_pSource));
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] ActivateObject failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // Retain reference to the camera
        m_pSource->AddRef();
        log_misc("iidx:camhook", "[{}] Activated", m_name);

        // Create an attribute store to hold initialization settings.
        hr = WrappedMFCreateAttributes(&pAttributes, 2);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] MFCreateAttributes failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, pD3DManager);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUnknown(MF_SOURCE_READER_D3D_MANAGER) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        hr = pAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUINT32(MF_SOURCE_READER_DISABLE_DXVA) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // TODO: Color space conversion
        // if (SUCCEEDED(hr)) {
        //     hr = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        // }       
        // if (SUCCEEDED(hr)) {
		// 	hr = pAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        // }

        // Create the source reader.
        hr = WrappedMFCreateSourceReaderFromMediaSource(
            m_pSource,
            pAttributes,
            &m_pSourceReader
        );
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] MFCreateSourceReaderFromMediaSource failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        log_misc("iidx:camhook", "[{}] Created source reader", m_name);

        hr = InitTargetTexture();
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] InitTargetTexture failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // Camera should be still usable even if camera control is not supported
        InitCameraControl();

    done:
        if (SUCCEEDED(hr)) {
            m_initialized = true;
            log_misc("iidx:camhook", "[{}] Initialized", m_name);
        } else {
            log_warning("iidx:camhook", "[{}] Failed to create camera: {:#x}", m_name, (ULONG)hr);
        }
        SafeRelease(&pAttributes);
        LeaveCriticalSection(&m_critsec);
    }

    HRESULT IIDXLocalCamera::StartCapture() {
        HRESULT hr = S_OK;
        IMFMediaType *pType = nullptr;

        if (!m_initialized) {
            log_warning("iidx:camhook", "[{}] Camera not initialized", m_name);
            return E_FAIL;
        }

        // Try to find a suitable output type.
        log_misc("iidx:camhook", "[{}] Find best media type", m_name);
        UINT32 bestWidth = 0;
        double bestFrameRate = 0;

        // The loop should terminate by MF_E_NO_MORE_TYPES
        // Adding a hard limit just in case
        for (DWORD i = 0; i < 1000; i++) {
            hr = m_pSourceReader->GetNativeMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                i,
                &pType
                );

            if (FAILED(hr)) { 
                if (hr != MF_E_NO_MORE_TYPES) {
                    log_warning("iidx:camhook", "[{}] Cannot get media type {} {:#x}", m_name, i, (ULONG)hr);
                }
                break; 
            }

            hr = TryMediaType(pType, &bestWidth, &bestFrameRate);
            if (SUCCEEDED(hr)) {
                MediaTypeInfo info = GetMediaTypeInfo(pType);
                m_mediaTypeInfos.push_back(info);
                if (hr == S_OK) {
                    m_pAutoMediaType = pType;
                }
            } else {
                // Invalid media type (e.g. no conversion function)
                SafeRelease(&pType);
            }
        }

        // Sort available media types
        std::sort(m_mediaTypeInfos.begin(), m_mediaTypeInfos.end(), [](const MediaTypeInfo &a, const MediaTypeInfo &b) {
            if (a.width != b.width) {
                return a.width > b.width;
            }
            if (a.height != b.height) {
                return a.height > b.height;
            }
            if (a.frameRate != b.frameRate) {
                return (int)a.frameRate > (int)b.frameRate;
            }
            return a.subtype.Data1 > b.subtype.Data1;
        });

        if (!m_pAutoMediaType) {
            m_pAutoMediaType = m_mediaTypeInfos.front().p_mediaType;
        }

        IMFMediaType *pSelectedMediaType = nullptr;

        // Find media type specified by user configurations
        if (!m_useAutoMediaType && m_selectedMediaTypeDescription.length() > 0) {
            log_info("iidx:camhook", "[{}] Use media type from config {}", m_name, m_selectedMediaTypeDescription);
            auto it = std::find_if(m_mediaTypeInfos.begin(), m_mediaTypeInfos.end(), [this](const MediaTypeInfo &item){
                return item.description.compare(this->m_selectedMediaTypeDescription) == 0;
            });
            if (it != m_mediaTypeInfos.end()) {
                pSelectedMediaType = (*it).p_mediaType;
            }
        }

        hr = S_OK;

        if (!pSelectedMediaType) {
            pSelectedMediaType = m_pAutoMediaType;
        }

        if (SUCCEEDED(hr)) { 
            hr = ChangeMediaType(pSelectedMediaType);
        }

        if (SUCCEEDED(hr)) {
            log_info("iidx:camhook", "[{}] Creating thread", m_name);
            CreateThread();
        }
        return hr;
    }

    HRESULT IIDXLocalCamera::ChangeMediaType(IMFMediaType *pType) {
        HRESULT hr = S_OK;
        MediaTypeInfo info = GetMediaTypeInfo(pType);
        log_info("iidx:camhook", "[{}] Changing media type: {}", m_name, info.description);

        auto it = std::find_if(m_mediaTypeInfos.begin(), m_mediaTypeInfos.end(), [pType](const MediaTypeInfo &item) {
            return item.p_mediaType == pType;
        });
        m_selectedMediaTypeIndex = it - m_mediaTypeInfos.begin();
        m_selectedMediaTypeDescription = info.description;

        if (SUCCEEDED(hr)) {
            hr = m_pSourceReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                NULL,
                pType
                );
        }

        if (SUCCEEDED(hr)) {
            m_cameraWidth = info.width;
            m_cameraHeight = info.height;
            UpdateDrawRect();
        }
      
        return hr;
    }

    void IIDXLocalCamera::UpdateDrawRect() {
        double cameraRatio = (double)m_cameraWidth / m_cameraHeight;

        RECT cameraRect = {0, 0, m_cameraWidth, m_cameraHeight};
        RECT targetRect = {0, 0, TARGET_SURFACE_WIDTH, TARGET_SURFACE_HEIGHT};

        switch (m_drawMode) {
            case DrawModeStretch: {
                CopyRect(&m_rcSource, &cameraRect);
                CopyRect(&m_rcDest, &targetRect);
                break;
            }
            case DrawModeCrop: {
                if (cameraRatio > RATIO_16_9) {
                    // take full source height, crop left/right
                    LONG croppedWidth = m_cameraHeight * RATIO_16_9;
                    m_rcSource.left = (LONG)(m_cameraWidth - croppedWidth) / 2;
                    m_rcSource.top = 0;
                    m_rcSource.right = m_rcSource.left + croppedWidth;
                    m_rcSource.bottom = m_cameraHeight;
                } else {
                    // take full source width, crop top/bottom
                    LONG croppedHeight = m_cameraWidth / RATIO_16_9;
                    m_rcSource.left = 0;
                    m_rcSource.top = (LONG)(m_cameraHeight - croppedHeight) / 2;
                    m_rcSource.right = m_cameraWidth;
                    m_rcSource.bottom = m_rcSource.top + croppedHeight;
                }
                CopyRect(&m_rcDest, &targetRect);
                break;
            }
            case DrawModeLetterbox: {
                CopyRect(&m_rcSource, &cameraRect);
                if (cameraRatio > RATIO_16_9) {
                    // take full dest width, empty top/bottom
                    LONG boxedHeight = TARGET_SURFACE_WIDTH / cameraRatio;
                    m_rcDest.left = 0;
                    m_rcDest.top = (LONG)(TARGET_SURFACE_HEIGHT - boxedHeight) / 2;
                    m_rcDest.right = TARGET_SURFACE_WIDTH;
                    m_rcDest.bottom = m_rcDest.top + boxedHeight;
                } else {
                    // take full dest height, empty top/bottom
                    LONG boxedWidth = TARGET_SURFACE_HEIGHT * cameraRatio;
                    m_rcDest.left = (LONG)(TARGET_SURFACE_WIDTH - boxedWidth) / 2;
                    m_rcDest.top = 0;
                    m_rcDest.right = m_rcDest.left + boxedWidth;
                    m_rcDest.bottom = TARGET_SURFACE_HEIGHT;
                }
                break;
            }
            case DrawModeCrop4_3: {
                if (cameraRatio > RATIO_4_3) {
                    // take full source height, crop left/right
                    LONG croppedWidth = m_cameraHeight * RATIO_4_3;
                    m_rcSource.left = (LONG)(m_cameraWidth - croppedWidth) / 2;
                    m_rcSource.top = 0;
                    m_rcSource.right = m_rcSource.left + croppedWidth;
                    m_rcSource.bottom = m_cameraHeight;
                } else {
                    // take full source width, crop top/bottom
                    LONG croppedHeight = m_cameraWidth / RATIO_4_3;
                    m_rcSource.left = 0;
                    m_rcSource.top = (LONG)(m_cameraHeight - croppedHeight) / 2;
                    m_rcSource.right = m_cameraWidth;
                    m_rcSource.bottom = m_rcSource.top + croppedHeight;
                }
                CopyRect(&m_rcDest, &targetRect);
                break;
            }
            case DrawModeLetterbox4_3: {
                CopyRect(&m_rcSource, &cameraRect);
                if (cameraRatio > RATIO_4_3) {
                    // take full dest width, empty top/bottom
                    LONG boxedHeight = TARGET_SURFACE_HEIGHT / RATIO_4_3;
                    m_rcDest.left = 0;
                    m_rcDest.top = (LONG)(TARGET_SURFACE_HEIGHT - boxedHeight) / 2;
                    m_rcDest.right = TARGET_SURFACE_WIDTH;
                    m_rcDest.bottom = m_rcDest.top + boxedHeight;
                } else {
                    // take full dest height, empty top/bottom
                    LONG boxedWidth = TARGET_SURFACE_WIDTH * RATIO_4_3;
                    m_rcDest.left = (LONG)(TARGET_SURFACE_WIDTH - boxedWidth) / 2;
                    m_rcDest.top = 0;
                    m_rcDest.right = m_rcDest.left + boxedWidth;
                    m_rcDest.bottom = TARGET_SURFACE_HEIGHT;
                }
                break;
            }
        }

        // ensure the rects are valid
        IntersectRect(&m_rcSource, &m_rcSource, &cameraRect);
        IntersectRect(&m_rcDest, &m_rcDest, &targetRect);

        log_info(
            "iidx:camhook", "[{}] Update draw rect mode={} src=({}, {}, {}, {}) dest=({}, {}, {}, {})",
            m_name,
            DRAW_MODE_LABELS[m_drawMode],
            m_rcSource.left,
            m_rcSource.top,
            m_rcSource.right,
            m_rcSource.bottom,
            m_rcDest.left,
            m_rcDest.top,
            m_rcDest.right,
            m_rcDest.bottom
        );

        m_device->ColorFill(m_pDestSurf, &targetRect, D3DCOLOR_XRGB(0, 0, 0));
    }
  
    void IIDXLocalCamera::CreateThread() {
        // Create thread
        m_drawThread = new std::thread([this]() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

            double accumulator = 0.0;
            while (this->m_active) {
                this->Render();
                double frameTimeMicroSec = (1000000.0 / this->m_frameRate);
                int floorFrameTimeMicroSec = floor(frameTimeMicroSec);
                // This maybe an overkill but who knows
                accumulator += (frameTimeMicroSec - floorFrameTimeMicroSec);
                if (accumulator > 1.0) {
                    accumulator -= 1.0;
                    floorFrameTimeMicroSec += 1;
                }                
                std::this_thread::sleep_for(std::chrono::microseconds(floorFrameTimeMicroSec));
            }
        });
    }

    LPDIRECT3DTEXTURE9 IIDXLocalCamera::GetTexture() {
        return m_texture;
    }

    IAMCameraControl* IIDXLocalCamera::GetCameraControl() {
        return m_pCameraControl;
    }

    HRESULT IIDXLocalCamera::InitCameraControl() {
        HRESULT hr = S_OK;

        log_misc("iidx:camhook", "[{}] Init camera control", m_name);

        hr = m_pSource->QueryInterface(IID_IAMCameraControl, (void**)&m_pCameraControl); 
        if (FAILED(hr)) { 
            // The device does not support IAMCameraControl 
            log_warning("iidx:camhook", "[{}] Camera control not supported", m_name);
            return E_FAIL;
        } 

        for (size_t i = 0; i < CAMERA_CONTROL_PROP_SIZE; i++) {
            long minValue = 0;
            long maxValue = 0;
            long delta = 0;
            long defaultValue = 0;
            long defFlags = 0;
            long value = 0;
            long valueFlags = 0;

            m_pCameraControl->GetRange(
                i,
                &minValue,
                &maxValue,
                &delta,
                &defaultValue,
                &defFlags
            );
            m_pCameraControl->Get(
                i,
                &value,
                &valueFlags
            );
            m_controlProps.push_back({
                minValue,
                maxValue,
                delta,
                defaultValue,
                defFlags,
                value,
                valueFlags,
            });

            CameraControlProp prop = m_controlProps.at(i);

            log_misc(
                "iidx:camhook", "[{}] >> {} range=({}, {}) default={} delta={} dFlags={} value={} vFlags={}", 
                m_name,
                CAMERA_CONTROL_LABELS[i], 
                prop.minValue, prop.maxValue,
                prop.defaultValue,
                prop.delta,
                prop.defFlags,
                prop.value,
                prop.valueFlags
            );
        }

        m_controlOptionsInitialized = true;

        return hr;
    }

    HRESULT IIDXLocalCamera::GetCameraControlProp(int index, CameraControlProp *pProp) {
        if (!m_controlOptionsInitialized) {
            return E_FAIL;
        }

        auto targetProp = m_controlProps.at(index);

        pProp->minValue = targetProp.minValue;
        pProp->maxValue = targetProp.maxValue;
        pProp->defaultValue = targetProp.defaultValue;
        pProp->delta = targetProp.delta;
        pProp->defFlags = targetProp.defFlags;
        pProp->value = targetProp.value;
        pProp->valueFlags = targetProp.valueFlags;

        return S_OK;
    }

    HRESULT IIDXLocalCamera::SetCameraControlProp(int index, long value, long flags) {
        if (!m_controlOptionsInitialized || !m_allowManualControl) {
            return E_FAIL;
        }

        if (index < 0 || index >= CAMERA_CONTROL_PROP_SIZE) {
            return E_INVALIDARG;
        }

        auto targetProp = &(m_controlProps.at(index));
        HRESULT hr = m_pCameraControl->Set(index, value, flags);
        if (SUCCEEDED(hr)) {
            m_pCameraControl->Get(
                index,
                &targetProp->value,
                &targetProp->valueFlags
            );
        }

        return hr;
    }

    HRESULT IIDXLocalCamera::ResetCameraControlProps() {
        log_info("iidx:camhook", "[{}] Reset camera control", m_name);
        for (size_t i = 0; i < CAMERA_CONTROL_PROP_SIZE; i++) {
            CameraControlProp prop = m_controlProps.at(i);
            SetCameraControlProp(i, prop.defaultValue, prop.defFlags);
        }
        return S_OK;
    }

    std::string IIDXLocalCamera::GetName() {
        return m_name;
    }

    std::string IIDXLocalCamera::GetFriendlyName() {
        return m_friendly_name;
    }

    std::string IIDXLocalCamera::GetSymLink() {
        if (!m_pwszSymbolicLink) {
            return "(unknown)";
        }
        return ws2s(m_pwszSymbolicLink);
    }

    MediaTypeInfo IIDXLocalCamera::GetMediaTypeInfo(IMFMediaType *pType) {
        MediaTypeInfo info = {};
        HRESULT hr = S_OK;
        MFRatio frameRate = { 0, 0 };

        info.p_mediaType = pType;

        // Find the video subtype.
        hr = pType->GetGUID(MF_MT_SUBTYPE, &info.subtype);
        if (FAILED(hr)) { goto done; }

        // Get the frame size.
        hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &info.width, &info.height);
        if (FAILED(hr)) { goto done; }

        // Get frame rate
        hr = MFGetAttributeRatio(
            pType, 
            MF_MT_FRAME_RATE, 
            (UINT32*)&frameRate.Numerator, 
            (UINT32*)&frameRate.Denominator
        );
        if (FAILED(hr)) { goto done; }

        info.frameRate = frameRate.Numerator / frameRate.Denominator;

        info.description = fmt::format(
            "{}x{} @{}FPS {}", 
            info.width,
            info.height,
            (int)info.frameRate,
            GetVideoFormatName(info.subtype)
        );
    done:
        return info;
    }

    std::string IIDXLocalCamera::GetVideoFormatName(GUID subtype) {
        if (subtype == MFVideoFormat_YUY2) {
            return "YUY2";
        }

        if (subtype == MFVideoFormat_NV12) {
            return "NV12";
        }

        if (subtype == MFVideoFormat_MJPG) {
            return "MJPG";
        }

        return "Unknown";
    }

    /** 
     * Return values:
     *   S_OK:      this is a "better" media type than the existing one
     *   S_FALSE:   valid media type, but not "better"
     *   E_*:       invalid media type
     */
    HRESULT IIDXLocalCamera::TryMediaType(IMFMediaType *pType, UINT32 *pBestWidth, double *pBestFrameRate) {
        HRESULT hr = S_OK;
        UINT32 width = 0, height = 0;
        GUID subtype = { 0, 0, 0, 0 };
        MFRatio frameRate = { 0, 0 };

        hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);

        if (FAILED(hr))  { 
            log_warning("iidx:camhook", "[{}] Failed to get subtype: {:#x}", m_name, (ULONG)hr);
            return hr;    
        }

        hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr))  { 
            log_warning("iidx:camhook", "[{}] Failed to get frame size: {:#x}", m_name, (ULONG)hr);
            return hr;
        }
        
        // Only support format with converters
        // TODO: verify conversion support with DXVA
        if (subtype != MFVideoFormat_YUY2 && subtype != MFVideoFormat_NV12) {
            return E_FAIL;
        }

        // Frame rate
        hr = MFGetAttributeRatio(
            pType, 
            MF_MT_FRAME_RATE, 
            (UINT32*)&frameRate.Numerator, 
            (UINT32*)&frameRate.Denominator
        );
        if (FAILED(hr))  { 
            log_warning("iidx:camhook", "[{}] Failed to get frame rate: {:#x}", m_name, (ULONG)hr);
            return hr;
        }
        double frameRateValue = frameRate.Numerator / frameRate.Denominator;

        // Filter by aspect ratio
        auto aspect_ratio = 4.f / 3.f;
        if (m_prefer_16_by_9) {
            aspect_ratio = 16.f / 9.f;
        }
        if (fabs((height * aspect_ratio) - width) > 0.01f) {
            return S_FALSE;
        }

        // If we have 1280x720 already, only try for better frame rate
        if ((*pBestWidth >= (UINT32)TARGET_SURFACE_WIDTH) && (width > *pBestWidth) && (frameRateValue < *pBestFrameRate)) {
            return S_FALSE;
        }

        // Check if this format has better resolution / frame rate
        if ((width > *pBestWidth) || (width >= (UINT32)TARGET_SURFACE_WIDTH && frameRateValue >= *pBestFrameRate)) {
            // log_misc(
            //     "iidx:camhook", "Better media type {} ({}x{}) @({} FPS)", 
            //     GetVideoFormatName(subtype),
            //     width,
            //     height,
            //     (int)frameRateValue
            // );

            *pBestWidth = width;
            *pBestFrameRate = frameRateValue;
            return S_OK;
        }

        return S_FALSE;
    }

    HRESULT IIDXLocalCamera::InitTargetTexture() {
        HRESULT hr = S_OK;

        // Create a new destination texture
        hr = m_device->CreateTexture(TARGET_SURFACE_WIDTH, TARGET_SURFACE_HEIGHT, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &m_texture, NULL);
        if (FAILED(hr)) { goto done; }

        // Create a D3D9 surface for the destination texture so that camera sample can be drawn onto it
        hr = m_texture->GetSurfaceLevel(0, &m_pDestSurf);
        if (FAILED(hr)) { goto done; }

        // Make the game use this new texture as camera stream source
        *m_camera_texture_target = m_texture;
        *m_preview_texture_target = m_texture;
        m_active = TRUE;

        // Create texture for colour conversion
        hr = m_device->CreateTexture(TARGET_SURFACE_WIDTH, TARGET_SURFACE_HEIGHT, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &m_conversionTexture, NULL);
        if (FAILED(hr)) { goto done; }
        hr = m_conversionTexture->GetSurfaceLevel(0, &m_pConversionSurf);
        if (FAILED(hr)) { goto done; }

        // Create texture for transformation
        hr = m_device->CreateTexture(TARGET_SURFACE_WIDTH, TARGET_SURFACE_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &m_transformTexture, NULL);
        if (FAILED(hr)) { goto done; }
        hr = m_transformTexture->GetSurfaceLevel(0, &m_pTransformSurf);
        if (FAILED(hr)) { goto done; }

        // Create texture for transformation result so that we don't have to screw our brain doing in-memory flipping
        hr = m_device->CreateTexture(TARGET_SURFACE_WIDTH, TARGET_SURFACE_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &m_transformResultTexture, NULL);
        if (FAILED(hr)) { goto done; }
        hr = m_transformResultTexture->GetSurfaceLevel(0, &m_pTransformResultSurf);
        if (FAILED(hr)) { goto done; }

        // printTextureLevelDesc(m_texture);
        // printTextureLevelDesc(m_texture_original);

    done:
        if (SUCCEEDED(hr)) {
            log_misc("iidx:camhook", "[{}] Created texture", m_name);
        } else {
            log_warning("iidx:camhook", "[{}] Failed to create texture: {:#x}", m_name, (ULONG)hr);
        }
        return hr;
    }

    HRESULT IIDXLocalCamera::FlushDrawCommands() {
        IDirect3DQuery9* pEventQuery = nullptr;
        // It is necessary to flush the command queue 
        // or the data is not ready for the receiver to read.
        // Adapted from : https://msdn.microsoft.com/en-us/library/windows/desktop/bb172234%28v=vs.85%29.aspx
        // Also see : http://www.ogre3d.org/forums/viewtopic.php?f=5&t=50486
        m_device->CreateQuery(D3DQUERYTYPE_EVENT, &pEventQuery);
        if (pEventQuery) {
            pEventQuery->Issue(D3DISSUE_END);
            while (S_FALSE == pEventQuery->GetData(NULL, 0, D3DGETDATA_FLUSH));
            pEventQuery->Release(); // Must be released or causes a leak and reference count increment
        }
        return S_OK;
    }

    HRESULT IIDXLocalCamera::DrawSample(IMFMediaBuffer *pSrcBuffer) {
        if (!m_active) {
            return E_FAIL;
        }

        // snap variables now so they don't change while inside the critical section
        const auto flip_h = m_flipHorizontal;
        const auto flip_v = m_flipVertical;

        EnterCriticalSection(&m_critsec);

        HRESULT hr = S_OK;
        IDirect3DSurface9 *pCameraSurf = NULL;

        hr = WrappedMFGetService(pSrcBuffer, MR_BUFFER_SERVICE, IID_PPV_ARGS(&pCameraSurf));

        if (flip_h || flip_v) {

            // Stretch Camera content to texture and perform color space conversion
            hr = m_device->StretchRect(pCameraSurf, &m_rcSource, m_pConversionSurf, &m_rcDest, D3DTEXF_LINEAR);

            // Copy converted camera content to dynamic texture for vertical/horizontal flipping
            hr = m_device->StretchRect(m_pConversionSurf, NULL, m_pTransformSurf, NULL, D3DTEXF_NONE);

            // Transform
            D3DLOCKED_RECT srcLockedRect;
            hr = m_pTransformSurf->LockRect(&srcLockedRect, NULL, D3DLOCK_NOSYSLOCK | D3DLOCK_READONLY);
            D3DLOCKED_RECT destLockedRect;
            hr = m_pTransformResultSurf->LockRect(&destLockedRect, NULL, D3DLOCK_NOSYSLOCK);

            BYTE* pSrc = (BYTE*)srcLockedRect.pBits;
            BYTE* pDest = (BYTE*)destLockedRect.pBits;
            const int pixelSize = 4;

            if (flip_v) {
                pDest += destLockedRect.Pitch * (TARGET_SURFACE_HEIGHT - 1);
            }

            for (int y = 0; y < TARGET_SURFACE_HEIGHT; y++) {
                for (int x = 0; x < TARGET_SURFACE_WIDTH; x++) {
                    memcpy(
                        pDest + x * pixelSize, 
                        pSrc + (flip_h ? (TARGET_SURFACE_WIDTH - x - 1) : x) * pixelSize, 
                        pixelSize
                    );
                }

                pSrc += srcLockedRect.Pitch;
                if (flip_v) {
                    pDest -= destLockedRect.Pitch;
                } else {
                    pDest += destLockedRect.Pitch;
                }
            }

            m_pTransformSurf->UnlockRect();
            m_pTransformResultSurf->UnlockRect();

            // Stretch camera texture to transform surface
            hr = m_device->StretchRect(m_pTransformResultSurf, NULL, m_pDestSurf, NULL, D3DTEXF_NONE);

        } else {
            // No transformation needed, stretch to destination texture directly
            hr = m_device->StretchRect(pCameraSurf, &m_rcSource, m_pDestSurf, &m_rcDest, D3DTEXF_LINEAR);
        }

        FlushDrawCommands();

        if (FAILED(hr)) {
            log_warning("iidx:camhook", "Error in DrawSample {:#x}", (ULONG)hr);
        }
        SafeRelease(&pCameraSurf);
        LeaveCriticalSection(&m_critsec);
        return hr;
    }

    HRESULT IIDXLocalCamera::ReadSample() {
        HRESULT hr;
        DWORD streamIndex, flags;
        LONGLONG llTimeStamp;
        IMFSample *pSample = nullptr;
        IMFMediaBuffer *pBuffer = nullptr;

        hr = m_pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &llTimeStamp,
            &pSample
        );

        if (pSample) {
            // Draw to D3D
            pSample->GetBufferByIndex(0, &pBuffer);
            hr = DrawSample(pBuffer);
        }

        SafeRelease(&pBuffer);
        SafeRelease(&pSample);

        return hr;
    }

    LPDIRECT3DTEXTURE9 IIDXLocalCamera::Render() {
        if (!m_active) {
            return nullptr;
        }
        HRESULT hr = ReadSample();
        if (FAILED(hr)) {
            return nullptr;
        }
        return m_texture;
    }

    ULONG IIDXLocalCamera::Release() {
        log_info("iidx:camhook", "[{}] Release camera", m_name);
        m_active = false;

        ULONG uCount = InterlockedDecrement(&m_nRefCount);

        *m_camera_texture_target = m_camera_texture_original;
        *m_preview_texture_target = m_preview_texture_original;

        for (size_t i = 0; i < m_mediaTypeInfos.size(); i++) {
            SafeRelease(&(m_mediaTypeInfos.at(i).p_mediaType));
        }

        SafeRelease(&m_pDestSurf);
        SafeRelease(&m_pTransformSurf);
        SafeRelease(&m_pConversionSurf);
        SafeRelease(&m_pTransformResultSurf);
        SafeRelease(&m_texture);
        SafeRelease(&m_conversionTexture);
        SafeRelease(&m_transformTexture);
        SafeRelease(&m_transformResultTexture);

        if (m_pSource) {
            m_pSource->Shutdown();
            m_pSource->Release();
        }

        CoTaskMemFree(m_pwszSymbolicLink);
        m_pwszSymbolicLink = NULL;
        m_cchSymbolicLink = 0;

        if (uCount == 0) {
            delete this;
        }
        // For thread safety, return a temporary variable.
        return uCount;
    }
}

#endif
