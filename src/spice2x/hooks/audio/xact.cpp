#include "xact.h"

#include <atomic>
#include <string>

#include <windows.h>
#include <initguid.h>
#include <mmreg.h>
#include <objbase.h>

#include "util/deferlog.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/utils.h"

namespace hooks::audio::xact {

    // XAudio 2.7 is a COM API. Newer Windows SDKs expose a different IXAudio2
    // layout, so keep this proxy pinned to the legacy ABI used by libxact.
    struct XAudio2DeviceDetails {
        WCHAR device_id[256];
        WCHAR display_name[256];
        DWORD role;
        WAVEFORMATEXTENSIBLE output_format;
    };

    struct XAudio2EffectChain {
        UINT32 effect_count;
        const void *effect_descriptors;
    };

    struct IXAudio2_27 {
        virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) = 0;
        virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
        virtual ULONG STDMETHODCALLTYPE Release() = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDeviceCount(UINT32 *device_count) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDeviceDetails(
            UINT32 device_index,
            XAudio2DeviceDetails *device_details) = 0;
        virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32 flags, UINT32 processor) = 0;
        virtual HRESULT STDMETHODCALLTYPE RegisterForCallbacks(void *callback) = 0;
        virtual void STDMETHODCALLTYPE UnregisterForCallbacks(void *callback) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateSourceVoice(
            void **source_voice,
            const WAVEFORMATEX *source_format,
            UINT32 flags,
            float max_frequency_ratio,
            void *callback,
            const void *send_list,
            const XAudio2EffectChain *effect_chain) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateSubmixVoice(
            void **submix_voice,
            UINT32 input_channels,
            UINT32 input_sample_rate,
            UINT32 flags,
            UINT32 processing_stage,
            const void *send_list,
            const XAudio2EffectChain *effect_chain) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateMasteringVoice(
            void **mastering_voice,
            UINT32 input_channels,
            UINT32 input_sample_rate,
            UINT32 flags,
            UINT32 device_index,
            const XAudio2EffectChain *effect_chain) = 0;
        virtual HRESULT STDMETHODCALLTYPE StartEngine() = 0;
        virtual void STDMETHODCALLTYPE StopEngine() = 0;
        virtual HRESULT STDMETHODCALLTYPE CommitChanges(UINT32 operation_set) = 0;
        virtual void STDMETHODCALLTYPE GetPerformanceData(void *performance_data) = 0;
        virtual void STDMETHODCALLTYPE SetDebugConfiguration(
            const void *debug_configuration,
            void *reserved) = 0;
    };

    // XAudio2 2.7 COM class and interface.
    DEFINE_GUID(CLSID_XAudio2_7_LEGACY,
        0x5a508685, 0xa254, 0x4fba,
        0x9b, 0x82, 0x9a, 0x24, 0xb0, 0x03, 0x06, 0xaf);
    DEFINE_GUID(IID_IXAudio2_7_LEGACY,
        0x8bcf1f58, 0x9fe7, 0x4583,
        0x8a, 0xc6, 0xe2, 0xad, 0xc4, 0x65, 0xc8, 0xbb);

    static decltype(CoCreateInstance) *CoCreateInstance_orig = nullptr;

    using CreateFX_t = HRESULT (WINAPI *)(REFCLSID, IUnknown **, const void *, UINT32);
    static CreateFX_t CreateFX_orig = nullptr;

    static std::string describe_wave_format(const WAVEFORMATEX *format) {
        if (format == nullptr) {
            return "null";
        }

        DWORD channel_mask = 0;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            channel_mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format)->dwChannelMask;
        }

        return fmt::format(
            "tag=0x{:04x}, channels={}, rate={} Hz, bits={}, valid_block={} B, avg={} B/s, mask=0x{:08x}",
            format->wFormatTag,
            format->nChannels,
            format->nSamplesPerSec,
            format->wBitsPerSample,
            format->nBlockAlign,
            format->nAvgBytesPerSec,
            channel_mask);
    }

    template <size_t Size>
    static std::string narrow_fixed(const WCHAR (&value)[Size]) {
        size_t length = 0;
        while (length < Size && value[length] != L'\0') {
            length++;
        }
        return ws2s(std::wstring(value, length));
    }

    class WrappedXAudio2 final : public IXAudio2_27 {
    public:
        explicit WrappedXAudio2(IXAudio2_27 *real) : real(real) {
            log_info("audio::xaudio2", "wrapping IXAudio2 2.7 engine {}", static_cast<void *>(real));
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
            if (object == nullptr) {
                return E_POINTER;
            }
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IXAudio2_7_LEGACY)) {
                *object = this;
                AddRef();
                log_info("audio::xaudio2", "IXAudio2::QueryInterface({}) -> proxy", guid2s(riid));
                return S_OK;
            }

            const auto result = real->QueryInterface(riid, object);
            log_info(
                "audio::xaudio2",
                "IXAudio2::QueryInterface({}) -> {}, object={}",
                guid2s(riid),
                FMT_HRESULT(result),
                object != nullptr ? *object : nullptr);
            return result;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++ref_count;
        }

        ULONG STDMETHODCALLTYPE Release() override {
            const auto remaining = --ref_count;
            if (remaining == 0) {
                log_info("audio::xaudio2", "destroying IXAudio2 2.7 proxy");
                real->Release();
                delete this;
            }
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE GetDeviceCount(UINT32 *device_count) override {
            const auto result = real->GetDeviceCount(device_count);
            log_info(
                "audio::xaudio2",
                "IXAudio2::GetDeviceCount -> {}, count={}",
                FMT_HRESULT(result),
                SUCCEEDED(result) && device_count != nullptr ? *device_count : 0);
            return result;
        }

        HRESULT STDMETHODCALLTYPE GetDeviceDetails(
            UINT32 device_index,
            XAudio2DeviceDetails *device_details) override {
            const auto result = real->GetDeviceDetails(device_index, device_details);
            if (SUCCEEDED(result) && device_details != nullptr) {
                const auto device_name = narrow_fixed(device_details->display_name);
                if (!device_details_logged.exchange(true, std::memory_order_relaxed)) {
                    log_info(
                        "audio::xaudio2",
                        "IXAudio2::GetDeviceDetails({}) -> {}, id='{}', name='{}', role=0x{:08x}, {}",
                        device_index,
                        FMT_HRESULT(result),
                        narrow_fixed(device_details->device_id),
                        device_name,
                        device_details->role,
                        describe_wave_format(&device_details->output_format.Format));
                }
                const auto channels = device_details->output_format.Format.nChannels;
                if (channels != 2 && channels != 6 &&
                    !channel_warning_logged.exchange(true, std::memory_order_relaxed)) {
                    log_warning(
                        "audio::xaudio2",
                        "output device '{}' has {} channels; Nostalgia requires stereo or 5.1 output",
                        device_name,
                        channels);
                    deferredlogs::defer_error_messages({
                        "unsupported audio output channel count detected!",
                        fmt::format("    device: {}", device_name),
                        fmt::format("    detected {} channels; Nostalgia requires 2 (stereo) or 6 (5.1)", channels),
                        "      * configure the default Windows playback device for stereo or 5.1 output",
                        "      * disable 7.1 surround sound or spatial audio for this device",
                    });
                }
            } else {
                log_warning(
                    "audio::xaudio2",
                    "IXAudio2::GetDeviceDetails({}) -> {}",
                    device_index,
                    FMT_HRESULT(result));
            }
            return result;
        }

        HRESULT STDMETHODCALLTYPE Initialize(UINT32 flags, UINT32 processor) override {
            const auto result = real->Initialize(flags, processor);
            log_info(
                "audio::xaudio2",
                "IXAudio2::Initialize(flags=0x{:08x}, processor=0x{:08x}) -> {}",
                flags,
                processor,
                FMT_HRESULT(result));
            return result;
        }

        HRESULT STDMETHODCALLTYPE RegisterForCallbacks(void *callback) override {
            const auto result = real->RegisterForCallbacks(callback);
            log_info(
                "audio::xaudio2",
                "IXAudio2::RegisterForCallbacks({}) -> {}",
                callback,
                FMT_HRESULT(result));
            return result;
        }

        void STDMETHODCALLTYPE UnregisterForCallbacks(void *callback) override {
            log_info("audio::xaudio2", "IXAudio2::UnregisterForCallbacks({})", callback);
            real->UnregisterForCallbacks(callback);
        }

        HRESULT STDMETHODCALLTYPE CreateSourceVoice(
            void **source_voice,
            const WAVEFORMATEX *source_format,
            UINT32 flags,
            float max_frequency_ratio,
            void *callback,
            const void *send_list,
            const XAudio2EffectChain *effect_chain) override {
            return real->CreateSourceVoice(
                source_voice,
                source_format,
                flags,
                max_frequency_ratio,
                callback,
                send_list,
                effect_chain);
        }

        HRESULT STDMETHODCALLTYPE CreateSubmixVoice(
            void **submix_voice,
            UINT32 input_channels,
            UINT32 input_sample_rate,
            UINT32 flags,
            UINT32 processing_stage,
            const void *send_list,
            const XAudio2EffectChain *effect_chain) override {
            return real->CreateSubmixVoice(
                submix_voice,
                input_channels,
                input_sample_rate,
                flags,
                processing_stage,
                send_list,
                effect_chain);
        }

        HRESULT STDMETHODCALLTYPE CreateMasteringVoice(
            void **mastering_voice,
            UINT32 input_channels,
            UINT32 input_sample_rate,
            UINT32 flags,
            UINT32 device_index,
            const XAudio2EffectChain *effect_chain) override {
            const auto result = real->CreateMasteringVoice(
                mastering_voice,
                input_channels,
                input_sample_rate,
                flags,
                device_index,
                effect_chain);
            log_info(
                "audio::xaudio2",
                "IXAudio2::CreateMasteringVoice(channels={}, rate={} Hz, flags=0x{:08x}, device={}, effects={}) -> {}, voice={}",
                input_channels,
                input_sample_rate,
                flags,
                device_index,
                effect_chain != nullptr ? effect_chain->effect_count : 0,
                FMT_HRESULT(result),
                mastering_voice != nullptr ? *mastering_voice : nullptr);
            return result;
        }

        HRESULT STDMETHODCALLTYPE StartEngine() override {
            const auto result = real->StartEngine();
            log_info("audio::xaudio2", "IXAudio2::StartEngine -> {}", FMT_HRESULT(result));
            return result;
        }

        void STDMETHODCALLTYPE StopEngine() override {
            log_info("audio::xaudio2", "IXAudio2::StopEngine");
            real->StopEngine();
        }

        HRESULT STDMETHODCALLTYPE CommitChanges(UINT32 operation_set) override {
            return real->CommitChanges(operation_set);
        }

        void STDMETHODCALLTYPE GetPerformanceData(void *performance_data) override {
            real->GetPerformanceData(performance_data);
        }

        void STDMETHODCALLTYPE SetDebugConfiguration(
            const void *debug_configuration,
            void *reserved) override {
            log_info("audio::xaudio2", "IXAudio2::SetDebugConfiguration({})", debug_configuration);
            real->SetDebugConfiguration(debug_configuration, reserved);
        }

    private:
        std::atomic<ULONG> ref_count = 1;
        std::atomic_bool device_details_logged = false;
        std::atomic_bool channel_warning_logged = false;
        IXAudio2_27 *real;
    };

    static HRESULT STDAPICALLTYPE CoCreateInstance_hook(
        REFCLSID clsid,
        LPUNKNOWN outer,
        DWORD class_context,
        REFIID iid,
        LPVOID *object) {
        const auto result = CoCreateInstance_orig(clsid, outer, class_context, iid, object);
        log_info(
            "audio::xact",
            "CoCreateInstance(clsid={}, iid={}, context=0x{:08x}) -> {}, object={}",
            guid2s(clsid),
            guid2s(iid),
            class_context,
            FMT_HRESULT(result),
            object != nullptr ? *object : nullptr);

        if (SUCCEEDED(result) && object != nullptr && *object != nullptr &&
            IsEqualCLSID(clsid, CLSID_XAudio2_7_LEGACY) &&
            IsEqualIID(iid, IID_IXAudio2_7_LEGACY)) {
            *object = static_cast<IXAudio2_27 *>(
                new WrappedXAudio2(static_cast<IXAudio2_27 *>(*object)));
        }
        return result;
    }

    static HRESULT WINAPI CreateFX_hook(
        REFCLSID clsid,
        IUnknown **effect,
        const void *init_data,
        UINT32 init_data_size) {
        const auto result = CreateFX_orig(clsid, effect, init_data, init_data_size);
        log_info(
            "audio::xapofx",
            "CreateFX(clsid={}, init_data={}, size={}) -> {}, effect={}",
            guid2s(clsid),
            init_data,
            init_data_size,
            FMT_HRESULT(result),
            effect != nullptr ? static_cast<void *>(*effect) : nullptr);
        return result;
    }

    void init() {
        const auto libxact = GetModuleHandleW(L"libxact.dll");
        if (libxact == nullptr) {
            return;
        }

        CoCreateInstance_orig = detour::iat_try(
            "CoCreateInstance", CoCreateInstance_hook, libxact);
        CreateFX_orig = detour::iat_try("CreateFX", CreateFX_hook, libxact);

        log_info(
            "audio::xact",
            "libxact hooks installed: CoCreateInstance={}, CreateFX={}",
            CoCreateInstance_orig != nullptr,
            CreateFX_orig != nullptr);
    }
}