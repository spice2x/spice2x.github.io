#include "games/iidx/local_camera.h"

#if SPICE64 && !SPICE_XP

#include <algorithm>
#include <condition_variable>

#include "util/logging.h"
#include "util/precise_timer.h"
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

    // async reads keep mode changes and shutdown interruptible without blocking the UI thread
    class IIDXCameraSourceReaderCallback final : public IMFSourceReaderCallback {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
            if (!object) {
                return E_POINTER;
            }
            if (riid == IID_IUnknown || riid == IID_IMFSourceReaderCallback) {
                *object = static_cast<IMFSourceReaderCallback *>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return InterlockedIncrement(&m_refCount);
        }

        ULONG STDMETHODCALLTYPE Release() override {
            const ULONG refCount = InterlockedDecrement(&m_refCount);
            if (refCount == 0) {
                delete this;
            }
            return refCount;
        }

        HRESULT STDMETHODCALLTYPE OnReadSample(
                HRESULT status,
                DWORD,
                DWORD,
                LONGLONG,
                IMFSample *sample) override {
            const std::lock_guard<std::mutex> lock(m_mutex);
            SafeRelease(&m_sample);
            m_status = status;
            m_sample = sample;
            if (m_sample) {
                m_sample->AddRef();
            }
            m_readComplete = true;
            m_condition.notify_all();
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override {
            const std::lock_guard<std::mutex> lock(m_mutex);
            if (m_flushPending) {
                m_flushComplete = true;
                m_condition.notify_all();
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent *) override {
            return S_OK;
        }

        void PrepareRead() {
            const std::lock_guard<std::mutex> lock(m_mutex);
            SafeRelease(&m_sample);
            m_status = S_OK;
            m_readComplete = false;
            m_interrupted = false;
        }

        HRESULT WaitForRead(IMFSample **sample) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this] {
                return m_readComplete || m_interrupted || m_shutdown;
            });
            if (m_shutdown) {
                return MF_E_SHUTDOWN;
            }
            if (m_interrupted) {
                return S_FALSE;
            }
            const HRESULT status = m_status;
            *sample = m_sample;
            m_sample = nullptr;
            return status;
        }

        void InterruptRead() {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_interrupted = true;
            m_condition.notify_all();
        }

        void BeginShutdown() {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_shutdown = true;
            m_interrupted = true;
            m_condition.notify_all();
        }

        bool IsShutdown() {
            const std::lock_guard<std::mutex> lock(m_mutex);
            return m_shutdown;
        }

        bool BeginFlush() {
            const std::lock_guard<std::mutex> lock(m_mutex);
            if (m_flushPending) {
                return false;
            }
            m_flushPending = true;
            m_flushComplete = false;
            return true;
        }

        void CancelFlush() {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_flushPending = false;
            m_flushComplete = false;
        }

        HRESULT WaitForFlush() {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_condition.wait_for(lock, std::chrono::seconds(2), [this] {
                    return m_flushComplete || m_shutdown;
                })) {
                return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            if (m_shutdown) {
                return MF_E_SHUTDOWN;
            }
            m_flushPending = false;
            m_flushComplete = false;
            return S_OK;
        }

    private:
        ~IIDXCameraSourceReaderCallback() {
            SafeRelease(&m_sample);
        }

        LONG m_refCount = 1;
        std::mutex m_mutex;
        std::condition_variable m_condition;
        IMFSample *m_sample = nullptr;
        HRESULT m_status = S_OK;
        bool m_readComplete = false;
        bool m_flushPending = false;
        bool m_flushComplete = false;
        bool m_interrupted = false;
        bool m_shutdown = false;
    };

    IIDXLocalCamera::~IIDXLocalCamera() {
        DeleteCriticalSection(&m_critsec);
    }

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

        // retrieve the camera symlink for control configuration
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

        log_misc("iidx:camhook", "[{}] Activated", m_name);

        // Create an attribute store to hold initialization settings.
        hr = WrappedMFCreateAttributes(&pAttributes, 5);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] MFCreateAttributes failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // provide the D3D9 device used to allocate decoded video surfaces
        hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, pD3DManager);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUnknown(MF_SOURCE_READER_D3D_MANAGER) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // allow hardware-accelerated video decoding through DXVA
        hr = pAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUINT32(MF_SOURCE_READER_DISABLE_DXVA) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // allow decoders and format converters in the Source Reader pipeline
        hr = pAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUINT32(MF_READWRITE_DISABLE_CONVERTERS) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        // enable video processing such as MJPG-to-RGB32 color conversion
        hr = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

        m_pSourceReaderCallback = new IIDXCameraSourceReaderCallback();
        hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m_pSourceReaderCallback);
        if (FAILED(hr)) {
            log_warning("iidx:camhook", "[{}] SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK) failed with {:#x}", m_name, (ULONG)hr);
            goto done;
        }

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

        if (FAILED(m_pSourceReader->QueryInterface(IID_PPV_ARGS(&m_pSourceReaderEx)))) {
            log_misc(
                "iidx:camhook",
                "[{}] IMFSourceReaderEx unavailable; using legacy media type selection",
                m_name
            );
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
        UINT32 bestNV12Width = 0;
        double bestNV12FrameRate = 0;
        IMFMediaType *pAutoNV12Type = nullptr;
        UINT32 bestYUY2Width = 0;
        double bestYUY2FrameRate = 0;
        IMFMediaType *pAutoYUY2Type = nullptr;

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
                if (info.subtype == MFVideoFormat_NV12 &&
                    TryMediaType(pType, &bestNV12Width, &bestNV12FrameRate) == S_OK) {
                    pAutoNV12Type = pType;
                }
                if (info.subtype == MFVideoFormat_YUY2 &&
                    TryMediaType(pType, &bestYUY2Width, &bestYUY2FrameRate) == S_OK) {
                    pAutoYUY2Type = pType;
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

        if (m_mediaTypeInfos.empty()) {
            log_warning("iidx:camhook", "[{}] No supported media types", m_name);
            return MF_E_INVALIDMEDIATYPE;
        }

        // prefer native GPU-ready formats to avoid MJPG decoding; NV12 uses less bandwidth than YUY2
        if (pAutoNV12Type) {
            m_pAutoMediaType = pAutoNV12Type;
        } else if (pAutoYUY2Type) {
            m_pAutoMediaType = pAutoYUY2Type;
        } else if (!m_pAutoMediaType) {
            m_pAutoMediaType = m_mediaTypeInfos.front().p_mediaType;
        }

        IMFMediaType *pSelectedMediaType = nullptr;

        // Find media type specified by user configurations
        const auto selectedMediaTypeDescription = GetSelectedMediaTypeDescription();
        if (!m_useAutoMediaType && !selectedMediaTypeDescription.empty()) {
            log_info("iidx:camhook", "[{}] Use media type from config {}", m_name, selectedMediaTypeDescription);
            auto it = std::find_if(m_mediaTypeInfos.begin(), m_mediaTypeInfos.end(), [&selectedMediaTypeDescription](const MediaTypeInfo &item){
                return item.description == selectedMediaTypeDescription;
            });
            if (it != m_mediaTypeInfos.end()) {
                pSelectedMediaType = (*it).p_mediaType;
            }
        }

        hr = S_OK;

        if (!pSelectedMediaType) {
            pSelectedMediaType = m_pAutoMediaType;
        }

        hr = ChangeMediaType(pSelectedMediaType);

        if (SUCCEEDED(hr)) {
            log_info("iidx:camhook", "[{}] Creating thread", m_name);
            CreateThread();
        }
        return hr;
    }

    HRESULT IIDXLocalCamera::ChangeMediaType(IMFMediaType *pType) {
        if (!pType) {
            return E_POINTER;
        }

        HRESULT hr = S_OK;
        IMFMediaType *pDecodedType = nullptr;
        GUID subtype = GUID_NULL;
        DWORD streamFlags = 0;

        auto it = std::find_if(m_mediaTypeInfos.begin(), m_mediaTypeInfos.end(), [pType](const MediaTypeInfo &item) {
            return item.p_mediaType == pType;
        });
        if (it == m_mediaTypeInfos.end()) {
            log_warning("iidx:camhook", "[{}] Requested unknown media type", m_name);
            return E_INVALIDARG;
        }

        MediaTypeInfo info = GetMediaTypeInfo(pType);
        log_info("iidx:camhook", "[{}] Changing media type: {}", m_name, info.description);

        if (SUCCEEDED(hr)) {
            hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
        }

        if (SUCCEEDED(hr) && m_pSourceReaderEx) {
            hr = m_pSourceReaderEx->SetNativeMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                pType,
                &streamFlags
            );
            if (SUCCEEDED(hr)) {
                log_info(
                    "iidx:camhook",
                    "[{}] Native media type applied, flags={:#x}",
                    m_name,
                    streamFlags
                );
            }
        } else if (SUCCEEDED(hr)) {
            hr = m_pSourceReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                NULL,
                pType
                );
        }

        if (SUCCEEDED(hr) && subtype == MFVideoFormat_MJPG) {
            hr = WrappedMFCreateMediaType(&pDecodedType);
            if (SUCCEEDED(hr)) {
                hr = pDecodedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            }

            const GUID uint64Attributes[] = {
                MF_MT_FRAME_SIZE,
                MF_MT_FRAME_RATE,
                MF_MT_PIXEL_ASPECT_RATIO,
            };
            for (const auto &attribute : uint64Attributes) {
                UINT64 value = 0;
                if (SUCCEEDED(pType->GetUINT64(attribute, &value))) {
                    hr = pDecodedType->SetUINT64(attribute, value);
                    if (FAILED(hr)) {
                        break;
                    }
                }
            }

            UINT32 interlaceMode = MFVideoInterlace_Progressive;
            if (SUCCEEDED(hr) && SUCCEEDED(pType->GetUINT32(MF_MT_INTERLACE_MODE, &interlaceMode))) {
                hr = pDecodedType->SetUINT32(MF_MT_INTERLACE_MODE, interlaceMode);
            }

            const GUID decodedSubtypes[] = {
                MFVideoFormat_RGB32,
                MFVideoFormat_YUY2,
                MFVideoFormat_NV12,
            };
            if (SUCCEEDED(hr)) {
                for (const auto &decodedSubtype : decodedSubtypes) {
                    hr = pDecodedType->SetGUID(MF_MT_SUBTYPE, decodedSubtype);
                    if (SUCCEEDED(hr)) {
                        hr = m_pSourceReader->SetCurrentMediaType(
                            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                            NULL,
                            pDecodedType
                            );
                    }
                    if (SUCCEEDED(hr)) {
                        log_info(
                            "iidx:camhook",
                            "[{}] Decoding MJPG as {} ({}x{} @{}FPS)",
                            m_name,
                            GetVideoFormatName(decodedSubtype),
                            info.width,
                            info.height,
                            (int)info.frameRate
                        );
                        m_decodedSubtype = decodedSubtype;
                        break;
                    }
                }
            }

            if (SUCCEEDED(hr)) {
                auto decodedFormat = D3DFMT_X8R8G8B8;
                if (m_decodedSubtype == MFVideoFormat_YUY2) {
                    decodedFormat = D3DFMT_YUY2;
                } else if (m_decodedSubtype == MFVideoFormat_NV12) {
                    decodedFormat = static_cast<D3DFORMAT>(MAKEFOURCC('N', 'V', '1', '2'));
                }
                SafeRelease(&m_pDecodedSurf);
                const HRESULT surfaceHr = m_device->CreateOffscreenPlainSurface(
                    info.width,
                    info.height,
                    decodedFormat,
                    D3DPOOL_DEFAULT,
                    &m_pDecodedSurf,
                    NULL
                );
                if (FAILED(surfaceHr)) {
                    log_warning(
                        "iidx:camhook",
                        "[{}] Failed to create {} upload surface: {:#x}",
                        m_name,
                        GetVideoFormatName(m_decodedSubtype),
                        (ULONG)surfaceHr
                    );
                }
            }
        }

        if (SUCCEEDED(hr) && subtype != MFVideoFormat_MJPG && m_pSourceReaderEx) {
            hr = m_pSourceReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                NULL,
                pType
            );
        }

        if (SUCCEEDED(hr) && subtype != MFVideoFormat_MJPG) {
            SafeRelease(&m_pDecodedSurf);
            m_decodedSubtype = GUID_NULL;
        }

        if (SUCCEEDED(hr)) {
            SetSelectedMediaType(it - m_mediaTypeInfos.begin(), info.description);
            EnterCriticalSection(&m_critsec);
            m_outputSubtype = subtype == MFVideoFormat_MJPG
                ? m_decodedSubtype
                : subtype;
            m_cameraWidth = info.width;
            m_cameraHeight = info.height;
            m_frameRate = info.frameRate;
            LeaveCriticalSection(&m_critsec);
            UpdateDrawRect();
            m_drawErrorLogged = false;
        }

        SafeRelease(&pDecodedType);
        return hr;
    }

    void IIDXLocalCamera::RequestMediaType(IMFMediaType *pType) {
        if (!pType || !m_active || !m_pSourceReaderCallback) {
            return;
        }

        const auto info = GetMediaTypeInfo(pType);
        log_info(
            "iidx:camhook",
            "[{}] Requested media type: {}",
            m_name,
            info.description
        );

        {
            const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
            m_pendingMediaType = pType;
        }
        m_pSourceReaderCallback->InterruptRead();
    }

    int IIDXLocalCamera::GetSelectedMediaTypeIndex() {
        const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
        return m_selectedMediaTypeIndex;
    }

    std::string IIDXLocalCamera::GetSelectedMediaTypeDescription() {
        const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
        return m_selectedMediaTypeDescription;
    }

    void IIDXLocalCamera::SetSelectedMediaTypeDescription(const std::string &description) {
        const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
        m_selectedMediaTypeDescription = description;
    }

    void IIDXLocalCamera::SetSelectedMediaType(int index, const std::string &description) {
        const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
        m_selectedMediaTypeIndex = index;
        m_selectedMediaTypeDescription = description;
    }

    bool IIDXLocalCamera::HasPendingMediaType() {
        const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
        return m_pendingMediaType != nullptr;
    }

    HRESULT IIDXLocalCamera::ApplyPendingMediaType() {
        if (!HasPendingMediaType()) {
            return S_FALSE;
        }

        const bool startFlush = m_pSourceReaderCallback->BeginFlush();
        HRESULT hr = S_OK;
        if (startFlush) {
            hr = m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            if (FAILED(hr)) {
                m_pSourceReaderCallback->CancelFlush();
            }
        }
        if (SUCCEEDED(hr)) {
            hr = m_pSourceReaderCallback->WaitForFlush();
        }

        IMFMediaType *pType = nullptr;
        if (SUCCEEDED(hr)) {
            const std::lock_guard<std::mutex> lock(m_mediaTypeMutex);
            pType = m_pendingMediaType;
            m_pendingMediaType = nullptr;
        }
        if (SUCCEEDED(hr) && !m_active) {
            return S_FALSE;
        }
        if (SUCCEEDED(hr) && pType) {
            hr = ChangeMediaType(pType);
        }
        if (FAILED(hr)) {
            log_warning(
                "iidx:camhook",
                "[{}] Failed to apply requested media type: {:#x}",
                m_name,
                (ULONG)hr
            );
        }
        return hr;
    }

    void IIDXLocalCamera::UpdateDrawRect() {
        EnterCriticalSection(&m_critsec);

        double cameraRatio = (double)m_cameraWidth / m_cameraHeight;
        const auto drawMode = m_drawMode.load();

        RECT cameraRect = {0, 0, m_cameraWidth, m_cameraHeight};
        RECT targetRect = {0, 0, TARGET_SURFACE_WIDTH, TARGET_SURFACE_HEIGHT};

        switch (drawMode) {
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

        if (m_outputSubtype == MFVideoFormat_YUY2 || m_outputSubtype == MFVideoFormat_NV12) {
            m_rcSource.left &= ~1L;
            m_rcSource.right = (m_rcSource.right + 1) & ~1L;
        }
        if (m_outputSubtype == MFVideoFormat_NV12) {
            m_rcSource.top &= ~1L;
            m_rcSource.bottom = (m_rcSource.bottom + 1) & ~1L;
        }

        // ensure the rects are valid
        IntersectRect(&m_rcSource, &m_rcSource, &cameraRect);
        IntersectRect(&m_rcDest, &m_rcDest, &targetRect);

        log_info(
            "iidx:camhook", "[{}] Update draw rect mode={} src=({}, {}, {}, {}) dest=({}, {}, {}, {})",
            m_name,
            DRAW_MODE_LABELS[drawMode],
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
        LeaveCriticalSection(&m_critsec);
    }

    void IIDXLocalCamera::CreateThread() {
        // Create thread
        m_drawThread = new std::thread([this]() {
            timeutils::PreciseSleepTimer timer;
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
                timer.sleep(std::chrono::microseconds(floorFrameTimeMicroSec));
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

        if (subtype == MFVideoFormat_RGB32) {
            return "RGB32";
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

        // accept native D3D formats and MJPG decoded by the Source Reader
        if (subtype != MFVideoFormat_YUY2 && subtype != MFVideoFormat_NV12 && subtype != MFVideoFormat_MJPG) {
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

    HRESULT IIDXLocalCamera::UploadDecodedSample(IMFMediaBuffer *pSrcBuffer) {
        if (!m_pDecodedSurf || m_decodedSubtype == GUID_NULL) {
            return E_UNEXPECTED;
        }

        IMF2DBuffer *p2DBuffer = nullptr;
        BYTE *pSrc = nullptr;
        LONG srcPitch = 0;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        bool locked2D = false;
        D3DLOCKED_RECT destLockedRect = {};

        LONG rowBytes = m_cameraWidth * 4;
        LONG rowCount = m_cameraHeight;
        if (m_decodedSubtype == MFVideoFormat_YUY2) {
            rowBytes = m_cameraWidth * 2;
        } else if (m_decodedSubtype == MFVideoFormat_NV12) {
            rowBytes = m_cameraWidth;
            rowCount += m_cameraHeight / 2;
        }
        if (rowBytes <= 0 || rowCount <= 0) {
            return MF_E_INVALIDMEDIATYPE;
        }

        const DWORD rowBytesValue = static_cast<DWORD>(rowBytes);
        const DWORD rowCountValue = static_cast<DWORD>(rowCount);
        const ULONGLONG minimumLength =
            static_cast<ULONGLONG>(rowBytesValue) * rowCountValue;

        HRESULT hr = pSrcBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
        if (SUCCEEDED(hr)) {
            hr = p2DBuffer->Lock2D(&pSrc, &srcPitch);
            locked2D = SUCCEEDED(hr);
        }

        if (!locked2D) {
            SafeRelease(&p2DBuffer);
            hr = pSrcBuffer->Lock(&pSrc, &maxLength, &currentLength);
            if (SUCCEEDED(hr)) {
                if (currentLength < minimumLength) {
                    hr = MF_E_BUFFERTOOSMALL;
                } else {
                    const DWORD inferredPitch = currentLength % rowCountValue == 0
                        ? currentLength / rowCountValue
                        : rowBytesValue;
                    if (inferredPitch > LONG_MAX) {
                        hr = MF_E_INVALIDMEDIATYPE;
                    } else {
                        srcPitch = static_cast<LONG>(inferredPitch);
                    }
                }
            }
        }

        if (SUCCEEDED(hr) && std::abs(static_cast<int64_t>(srcPitch)) < rowBytes) {
            hr = MF_E_INVALIDMEDIATYPE;
        }

        if (SUCCEEDED(hr)) {
            hr = m_pDecodedSurf->LockRect(&destLockedRect, NULL, D3DLOCK_NOSYSLOCK);
        }

        if (SUCCEEDED(hr)) {
            auto *pDest = static_cast<BYTE *>(destLockedRect.pBits);
            for (LONG row = 0; row < rowCount; row++) {
                memcpy(pDest, pSrc, rowBytes);
                pSrc += srcPitch;
                pDest += destLockedRect.Pitch;
            }
            m_pDecodedSurf->UnlockRect();
        }

        if (locked2D) {
            p2DBuffer->Unlock2D();
            SafeRelease(&p2DBuffer);
        } else if (pSrc) {
            pSrcBuffer->Unlock();
        }

        return hr;
    }

    HRESULT IIDXLocalCamera::DrawSample(IMFMediaBuffer *pSrcBuffer) {
        if (!m_active) {
            return E_FAIL;
        }

        // snap variables now so they don't change while inside the critical section
        const auto flip_h = m_flipHorizontal.load();
        const auto flip_v = m_flipVertical.load();

        EnterCriticalSection(&m_critsec);

        HRESULT hr = S_OK;
        IDirect3DSurface9 *pCameraSurf = NULL;

        hr = WrappedMFGetService(pSrcBuffer, MR_BUFFER_SERVICE, IID_PPV_ARGS(&pCameraSurf));
        if (FAILED(hr) && m_pDecodedSurf) {
            hr = UploadDecodedSample(pSrcBuffer);
            if (SUCCEEDED(hr)) {
                pCameraSurf = m_pDecodedSurf;
                pCameraSurf->AddRef();
            }
        }

        if (FAILED(hr)) {
            log_warning(
                "iidx:camhook",
                "[{}] Failed to access decoded frame: {:#x}",
                m_name,
                (ULONG)hr
            );
            LeaveCriticalSection(&m_critsec);
            return hr;
        }

        if (flip_h || flip_v) {

            // Stretch Camera content to texture and perform color space conversion
            hr = m_device->StretchRect(pCameraSurf, &m_rcSource, m_pConversionSurf, &m_rcDest, D3DTEXF_LINEAR);

            // Copy converted camera content to dynamic texture for vertical/horizontal flipping
            if (SUCCEEDED(hr)) {
                hr = m_device->StretchRect(m_pConversionSurf, NULL, m_pTransformSurf, NULL, D3DTEXF_NONE);
            }

            // Transform
            D3DLOCKED_RECT srcLockedRect = {};
            D3DLOCKED_RECT destLockedRect = {};
            bool srcLocked = false;
            bool destLocked = false;
            if (SUCCEEDED(hr)) {
                hr = m_pTransformSurf->LockRect(&srcLockedRect, NULL, D3DLOCK_NOSYSLOCK | D3DLOCK_READONLY);
                srcLocked = SUCCEEDED(hr);
            }
            if (SUCCEEDED(hr)) {
                hr = m_pTransformResultSurf->LockRect(&destLockedRect, NULL, D3DLOCK_NOSYSLOCK);
                destLocked = SUCCEEDED(hr);
            }

            if (SUCCEEDED(hr)) {
                const auto *srcBase = static_cast<const BYTE *>(srcLockedRect.pBits);
                auto *destBase = static_cast<BYTE *>(destLockedRect.pBits);
                const size_t rowBytes = TARGET_SURFACE_WIDTH * sizeof(uint32_t);
                for (int y = 0; y < TARGET_SURFACE_HEIGHT; y++) {
                    const auto *srcRow = srcBase + y * srcLockedRect.Pitch;
                    const int destY = flip_v ? TARGET_SURFACE_HEIGHT - y - 1 : y;
                    auto *destRow = destBase + destY * destLockedRect.Pitch;
                    if (flip_h) {
                        const auto *srcPixels = reinterpret_cast<const uint32_t *>(srcRow);
                        auto *destPixels = reinterpret_cast<uint32_t *>(destRow);
                        std::reverse_copy(
                            srcPixels,
                            srcPixels + TARGET_SURFACE_WIDTH,
                            destPixels
                        );
                    } else {
                        memcpy(destRow, srcRow, rowBytes);
                    }
                }
            }

            if (destLocked) {
                m_pTransformResultSurf->UnlockRect();
            }
            if (srcLocked) {
                m_pTransformSurf->UnlockRect();
            }

            // Stretch camera texture to transform surface
            if (SUCCEEDED(hr)) {
                hr = m_device->StretchRect(m_pTransformResultSurf, NULL, m_pDestSurf, NULL, D3DTEXF_NONE);
            }

        } else {
            // No transformation needed, stretch to destination texture directly
            hr = m_device->StretchRect(pCameraSurf, &m_rcSource, m_pDestSurf, &m_rcDest, D3DTEXF_LINEAR);
        }

        if (FAILED(hr) && !m_drawErrorLogged) {
            log_warning("iidx:camhook", "Error in DrawSample {:#x}", (ULONG)hr);
            m_drawErrorLogged = true;
        }
        SafeRelease(&pCameraSurf);
        LeaveCriticalSection(&m_critsec);
        return hr;
    }

    HRESULT IIDXLocalCamera::ReadSample() {
        IMFSample *pSample = nullptr;
        IMFMediaBuffer *pBuffer = nullptr;

        m_pSourceReaderCallback->PrepareRead();
        if (!m_active || m_pSourceReaderCallback->IsShutdown() || HasPendingMediaType()) {
            return S_FALSE;
        }

        HRESULT hr = m_pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        );
        if (SUCCEEDED(hr)) {
            hr = m_pSourceReaderCallback->WaitForRead(&pSample);
        }

        if (SUCCEEDED(hr) && pSample) {
            // Draw to D3D
            hr = pSample->GetBufferByIndex(0, &pBuffer);
            if (SUCCEEDED(hr)) {
                hr = DrawSample(pBuffer);
            }
        }

        SafeRelease(&pBuffer);
        SafeRelease(&pSample);

        return hr;
    }

    LPDIRECT3DTEXTURE9 IIDXLocalCamera::Render() {
        if (!m_active) {
            return nullptr;
        }
        HRESULT hr = ApplyPendingMediaType();
        if (FAILED(hr)) {
            return nullptr;
        }
        hr = ReadSample();
        if (FAILED(hr)) {
            return nullptr;
        }
        return m_texture;
    }

    ULONG IIDXLocalCamera::Release() {
        log_info("iidx:camhook", "[{}] Release camera", m_name);
        m_active = false;

        if (m_pSourceReaderCallback) {
            m_pSourceReaderCallback->BeginShutdown();
        }
        if (m_pSourceReader) {
            m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        }
        if (m_drawThread) {
            m_drawThread->join();
            delete m_drawThread;
            m_drawThread = nullptr;
        }

        ULONG uCount = InterlockedDecrement(&m_nRefCount);

        *m_camera_texture_target = m_camera_texture_original;
        *m_preview_texture_target = m_preview_texture_original;

        for (size_t i = 0; i < m_mediaTypeInfos.size(); i++) {
            SafeRelease(&(m_mediaTypeInfos.at(i).p_mediaType));
        }

        SafeRelease(&m_pDestSurf);
        SafeRelease(&m_pTransformSurf);
        SafeRelease(&m_pConversionSurf);
        SafeRelease(&m_pDecodedSurf);
        SafeRelease(&m_pTransformResultSurf);
        SafeRelease(&m_texture);
        SafeRelease(&m_conversionTexture);
        SafeRelease(&m_transformTexture);
        SafeRelease(&m_transformResultTexture);
        SafeRelease(&m_pSourceReaderEx);
        SafeRelease(&m_pSourceReader);
        SafeRelease(&m_pSourceReaderCallback);

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
