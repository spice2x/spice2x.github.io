#include "audio.h"

#include <mutex>

#include <external/robin_hood.h>

#include <windows.h>
#include <mmsystem.h>
#include <msacm.h>

#include "avs/game.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/memutils.h"
#include "util/time.h"
#include "util/unique_plain_ptr.h"

#define ACM_CACHE_ENABLED 1
#define ACM_DEBUG_PERF 0
#define ACM_DEBUG_VERBOSE 0

#if ACM_DEBUG_VERBOSE
#define log_debug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_debug(module, format_str, ...)
#endif

namespace hooks::audio::acm {
    struct WAVE_FORMAT {
        uint32_t suggest;
        // for WAVEFORMATEX
        std::vector<uint8_t> wave_format_ex;

        bool operator==(const WAVE_FORMAT& other) const {
            return (this->suggest == other.suggest &&
                this->wave_format_ex == other.wave_format_ex);
        }
    };
}

namespace robin_hood {
    template <>
    struct hash<hooks::audio::acm::WAVE_FORMAT> {
        std::size_t operator()(const hooks::audio::acm::WAVE_FORMAT& k) const noexcept {
            std::size_t h = robin_hood::hash_bytes(
                k.wave_format_ex.data(), k.wave_format_ex.size());

            h = h ^ robin_hood::hash<uint32_t>{}(k.suggest) << 1;
            return h;
        }
    };
}

namespace hooks::audio::acm {

    static decltype(acmFormatSuggest) *acmFormatSuggest_orig = nullptr;
    static std::mutex acm_formats_mutex;
    static robin_hood::unordered_map<WAVE_FORMAT, std::vector<uint8_t>> acm_formats;

    // hooks calls to acmFormatSuggest and returns cached results from previous calls
    //
    // iidx calls this many times during song load and reload (function of # of keysounds)
    // - with near-identical arguments over and over again
    // (when hovering over song in song select, selecting a song, and exiting result screen)
    //
    // on Linux this results in ACM module load/unload which is very expensive,
    // resulting in significant perf improvement (seconds per song load);
    // on Windows this saves milliseconds per song, at best
    //
    // https://codeberg.org/nixac/spicetools/issues/2
    // https://codeberg.org/nixac/spicetools/issues/4
    MMRESULT
    acmFormatSuggest_cached (
        HACMDRIVER had,
        LPWAVEFORMATEX pwfxSrc,
        LPWAVEFORMATEX pwfxDst,
        DWORD cbwfxDst,
        DWORD fdwSuggest) {

        if (had != 0 || cbwfxDst == 0) {
            return acmFormatSuggest_orig(had, pwfxSrc, pwfxDst, cbwfxDst, fdwSuggest);
        }

        WAVE_FORMAT key;
        key.suggest = fdwSuggest;
        const size_t src_size = sizeof(*pwfxSrc) + pwfxSrc->cbSize;
        key.wave_format_ex.insert(
            key.wave_format_ex.end(),
            reinterpret_cast<uint8_t *>(pwfxSrc),
            reinterpret_cast<uint8_t *>(pwfxSrc) + src_size);

        std::lock_guard<std::mutex> lock(acm_formats_mutex);

        MMRESULT mmresult = 0;

        if (acm_formats.contains(key)) {
            // found in cache
            const auto &result = acm_formats.at(key);

            if (cbwfxDst >= result.size()) {
                // dest buffer big enough, return cached result
                log_debug("audio::acm", "acmFormatSuggest cache hit, copying {} bytes", result.size());
                std::memcpy(reinterpret_cast<uint8_t *>(pwfxDst), result.data(), result.size());
                mmresult = 0;
            } else {
                // dest buffer not big enough, call original
                log_debug("audio::acm", "acmFormatSuggest cache fail; cbwfxDst too small ({})", cbwfxDst);
                mmresult = acmFormatSuggest_orig(had, pwfxSrc, pwfxDst, cbwfxDst, fdwSuggest);
            }

        } else {
            log_debug("audio::acm", "acmFormatSuggest cache miss; calling original");
            mmresult = acmFormatSuggest_orig(had, pwfxSrc, pwfxDst, cbwfxDst, fdwSuggest);

            // cache the result, but don't allow unconstrained growth
            if (mmresult == 0 && acm_formats.size() < 128) {
                const size_t dest_size = sizeof(*pwfxDst) + pwfxDst->cbSize;
                log_debug(
                    "audio::acm",
                    "acmFormatSuggest cache add; current cache size {}, new data {} bytes",
                    acm_formats.size(),
                    dest_size);

                acm_formats[key] = std::vector<uint8_t>();
                acm_formats[key].reserve(dest_size);
                acm_formats[key].insert(
                    acm_formats[key].begin(),
                    reinterpret_cast<uint8_t *>(pwfxDst),
                    reinterpret_cast<uint8_t *>(pwfxDst) + dest_size);
            }
        }

        return mmresult;
    }

    MMRESULT
    ACMAPI
    acmFormatSuggest_hook (
        HACMDRIVER had,
        LPWAVEFORMATEX pwfxSrc,
        LPWAVEFORMATEX pwfxDst,
        DWORD cbwfxDst,
        DWORD fdwSuggest) {

        log_debug(
            "audio::acm",
            "acmFormatSuggest called: had={}, "
            "formattag={}, ch={}, samplespersec={}, bytespersec={}, blockalign={}, bits={}, extrasize={}",
            fmt::ptr(had),
            pwfxSrc->wFormatTag,
            pwfxSrc->nChannels,
            pwfxSrc->nSamplesPerSec,
            pwfxSrc->nAvgBytesPerSec,
            pwfxSrc->nBlockAlign,
            pwfxSrc->wBitsPerSample,
            pwfxSrc->cbSize);

#if ACM_DEBUG_PERF
        const auto start = get_performance_milliseconds();
#endif

        // make a call to acmFormatSuggest

#if ACM_CACHE_ENABLED
        const auto result = acmFormatSuggest_cached(had, pwfxSrc, pwfxDst, cbwfxDst, fdwSuggest);
#else 
        const auto result = acmFormatSuggest_orig(had, pwfxSrc, pwfxDst, cbwfxDst, fdwSuggest);
#endif

        // log result

#if ACM_DEBUG_PERF
        const auto delta = get_performance_milliseconds() - start;
        static double delta_total = 0;
        delta_total += delta;

        log_info(
            "audio::acm",
            "acmFormatSuggest_hook returned {}, took {} us (running total {} ms)",
            result,
            delta * 1000,
            delta_total);
#else
        log_debug("audio::acm", "acmFormatSuggest_hook returned {}", result);
#endif

        if (result == 0 && cbwfxDst > sizeof(WAVEFORMATEX)) {
            log_debug(
                "audio::acm",
                "....formattag={}, ch={}, samplespersec={}, bytespersec={}, blockalign={}, bits={}, extrasize={}",
                pwfxDst->wFormatTag,
                pwfxDst->nChannels,
                pwfxDst->nSamplesPerSec,
                pwfxDst->nAvgBytesPerSec,
                pwfxDst->nBlockAlign,
                pwfxDst->wBitsPerSample,
                pwfxDst->cbSize);
        }

        return result;
    }

    void init() {

        // only enabled on Linux as the performance gains are negligible on Windows
#if SPICE_LINUX

        HMODULE msacm = libutils::try_library("msacm32.dll");
        if (msacm == nullptr) {
            log_info("audio::acm", "msacm32.dll failed to hook");
            return;
        }

        acmFormatSuggest_orig = detour::iat_try(
            "acmFormatSuggest", acmFormatSuggest_hook, avs::game::DLL_INSTANCE);
        if (acmFormatSuggest_orig != nullptr) {
            log_misc("audio::acm", "acmFormatSuggest hooked");

#if !ACM_CACHE_ENABLED
            log_warning("audio::acm", "acmFormatSuggest cache DISABLED");
#endif

        }

#endif

    }
}
