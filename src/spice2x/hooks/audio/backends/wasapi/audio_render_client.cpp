#include "audio_render_client.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "audio_client.h"
#include "hooks/audio/audio.h"
#include "util.h"
#include "wasapi_private.h"

const char CLASS_NAME[] = "WrappedIAudioRenderClient";

HRESULT STDMETHODCALLTYPE WrappedIAudioRenderClient::QueryInterface(REFIID riid, void **ppvObj) {
    if (ppvObj == nullptr) {
        return E_POINTER;
    }

    if (riid == IID_WrappedIAudioRenderClient ||
        riid == IID_IAudioRenderClient)
    {
        this->AddRef();
        *ppvObj = this;

        return S_OK;
    }

    return pReal->QueryInterface(riid, ppvObj);
}
ULONG STDMETHODCALLTYPE WrappedIAudioRenderClient::AddRef() {
    return pReal->AddRef();
}
ULONG STDMETHODCALLTYPE WrappedIAudioRenderClient::Release() {

    // get reference count of underlying interface
    ULONG refs = pReal != nullptr ? pReal->Release() : 0;

    if (refs == 0) {
        delete this;
    }

    return refs;
}

// IAudioRenderClient
HRESULT STDMETHODCALLTYPE WrappedIAudioRenderClient::GetBuffer(UINT32 NumFramesRequested, BYTE **ppData) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioRenderClient::GetBuffer");
    });

    if (this->client->backend) {
        SAFE_CALL("AudioBackend", "on_get_buffer", this->client->backend->on_get_buffer(
                NumFramesRequested,
                ppData));

        return S_OK;
    }

    // downmix + resample chained: the game writes its multi-channel native-rate audio into the
    // downmix scratch, which is downmixed to stereo and then resampled on release. size the
    // resampler's (stereo) input scratch now and hand the game the multi-channel downmix scratch.
    if (this->client->downmix.enabled && this->client->resample.enabled) {
        BYTE *resample_scratch = nullptr;
        this->client->resample.get_buffer(NumFramesRequested, &resample_scratch);
        CHECK_RESULT(this->client->downmix.get_scratch(NumFramesRequested, ppData));

    // surround downmix: reserve the real (stereo) device buffer, but hand the game a
    // multi-channel scratch buffer that we downmix on release
    } else if (this->client->downmix.enabled) {
        CHECK_RESULT(this->client->downmix.get_buffer(pReal, NumFramesRequested, ppData));

    // resample: hand the game a native-rate scratch buffer that we convert on release. the real
    // device buffer is acquired in ReleaseBuffer once the converted frame count is known.
    } else if (this->client->resample.enabled) {
        CHECK_RESULT(this->client->resample.get_buffer(NumFramesRequested, ppData));

    // shared-mode redirect bridge: point the game at the FIFO tail it can always fill, decoupling
    // its per-event writes from the shared engine's clock. the real device buffer is acquired in
    // ReleaseBuffer and filled only as fast as the device frees space (see SharedRedirect::drain).
    } else if (this->client->shared.bridge_enabled()) {
        *ppData = this->client->shared.begin_write(NumFramesRequested);

        return S_OK;
    }

    // call original
    HRESULT ret = pReal->GetBuffer(NumFramesRequested, ppData);

    // store buffer reference
    if (SUCCEEDED(ret)) {
        this->audio_buffer = *ppData;
    }

    CHECK_RESULT(ret);
}
HRESULT STDMETHODCALLTYPE WrappedIAudioRenderClient::ReleaseBuffer(UINT32 NumFramesWritten, DWORD dwFlags) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("audio::wasapi", "WrappedIAudioRenderClient::ReleaseBuffer");
    });

    if (this->client->backend) {
        SAFE_CALL("AudioBackend", "on_release_buffer", this->client->backend->on_release_buffer(
                NumFramesWritten,
                dwFlags));

        return S_OK;
    }

    // downmix + resample chained: downmix the game's multi-channel scratch into the resampler's
    // stereo input scratch, then let the resampler convert and push it to the device. a silent
    // buffer skips the downmix and feeds silence straight through.
    if (this->client->downmix.enabled && this->client->resample.enabled) {
        if ((dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
            this->client->downmix.downmix_into(
                    this->client->resample.input_data(), NumFramesWritten);
        }
        return this->client->resample.flush(
                pReal,
                this->client->pReal,
                NumFramesWritten,
                dwFlags,
                hooks::audio::VOLUME_BOOST);
    }

    // resample: convert the game's native-rate scratch and push as many output frames as the
    // device has room for, applying the volume boost to the converted output. handles acquiring
    // and releasing the real device buffer itself.
    if (this->client->resample.enabled) {
        return this->client->resample.flush(
                pReal,
                this->client->pReal,
                NumFramesWritten,
                dwFlags,
                hooks::audio::VOLUME_BOOST);
    }

    // shared-mode redirect bridge: queue the game's write and drain it to the device at the
    // device's own pace, so a full-buffer write never overflows the shared buffer.
    if (this->client->shared.bridge_enabled()) {
        this->client->shared.commit_write(
                NumFramesWritten, (dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);

        return this->client->shared.drain(
                pReal,
                this->client->pReal,
                this->client->device_format,
                hooks::audio::VOLUME_BOOST);
    }

    // resolve the real device buffer for whichever path produced the audio
    BYTE *device_buffer;
    if (this->client->downmix.enabled) {

        // downmix the game's multi-channel scratch into the real stereo buffer held since GetBuffer
        this->client->downmix.write_device_buffer(NumFramesWritten, dwFlags);
        device_buffer = this->client->downmix.current_buffer();
    } else {
        device_buffer = this->audio_buffer;

        // mute the first few buffers to avoid a startup pop
        if (this->buffers_to_mute > 0 && this->client->frame_size > 0) {
            memset(this->audio_buffer, 0, NumFramesWritten * this->client->frame_size);
            this->buffers_to_mute--;
        }
    }

    // boost the final output volume just before it reaches the device, layout-agnostic
    if (hooks::audio::VOLUME_BOOST != 1.0f
            && device_buffer != nullptr
            && (dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
        static std::once_flag boost_printed;
        std::call_once(boost_printed, []() {
            log_info("audio::wasapi", "volume boost active: gain={}", hooks::audio::VOLUME_BOOST);
        });
        apply_gain(device_buffer, NumFramesWritten, this->client->device_format,
                hooks::audio::VOLUME_BOOST);
    }

    HRESULT ret = pReal->ReleaseBuffer(NumFramesWritten, dwFlags);

    if (this->client->downmix.enabled) {
        this->client->downmix.buffer_released();
    }

    if (FAILED(ret)) {
        PRINT_FAILED_RESULT(CLASS_NAME, __func__, ret);
    }

    return ret;
}
