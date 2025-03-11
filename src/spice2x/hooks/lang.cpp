#include "lang.h"

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include <winternl.h>
#include <ntstatus.h>

#include "avs/game.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/utils.h"

// ANSI/OEM Japanese; Japanese (Shift-JIS)
constexpr UINT CODEPAGE_SHIFT_JIS = 932;

static decltype(GetACP) *GetACP_orig = nullptr;
static decltype(GetOEMCP) *GetOEMCP_orig = nullptr;
static decltype(MultiByteToWideChar) *MultiByteToWideChar_orig = nullptr;

#ifdef SPICE64
static decltype(GetSystemDefaultLCID) *GetSystemDefaultLCID_orig = nullptr;
#endif

static NTSTATUS NTAPI RtlMultiByteToUnicodeN_hook(
        PWCH UnicodeString,
        ULONG MaxBytesInUnicodeString,
        PULONG BytesInUnicodeString,
        const CHAR *MultiByteString,
        ULONG BytesInMultiByteString)
{
    // try to convert
    auto wc_num = MultiByteToWideChar(
            CODEPAGE_SHIFT_JIS,
            0,
            MultiByteString,
            static_cast<int>(BytesInMultiByteString),
            UnicodeString,
            static_cast<int>(MaxBytesInUnicodeString)
    );

    // error handling
    if (!wc_num) {
        auto error = GetLastError();

        switch (error) {
            case ERROR_INSUFFICIENT_BUFFER:
                return STATUS_BUFFER_TOO_SMALL;

            case ERROR_INVALID_PARAMETER:
            case ERROR_INVALID_FLAGS:
                return STATUS_INVALID_PARAMETER;

            case ERROR_NO_UNICODE_TRANSLATION:
                return STATUS_UNMAPPABLE_CHARACTER;

            default:
                return STATUS_UNSUCCESSFUL;
        }
    }

    // set byte count
    if (BytesInUnicodeString) {
        *BytesInUnicodeString = 2 * static_cast<UINT>(wc_num);
    }

    // return success
    return STATUS_SUCCESS;
}

static UINT WINAPI GetACP_hook() {
    return CODEPAGE_SHIFT_JIS;
}

static UINT WINAPI GetOEMCP_hook() {
    return CODEPAGE_SHIFT_JIS;
}

#ifdef SPICE64

static LCID WINAPI GetSystemDefaultLCID_hook() {
    // ja-JP per https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-lcid/a9eac961-e77d-41a6-90a5-ce1a8b0cdb9c
    // this is passed to LCMapStringA for kana conversions (e.g., for subscreen song search)
    return 0x411;
}

#endif

static int WINAPI MultiByteToWideChar_hook(
        UINT CodePage,
        DWORD dwFlags,
        LPCCH lpMultiByteStr,
        int cbMultiByte,
        LPWSTR lpWideCharStr,
        int cchWideChar)
{
    switch (CodePage) {
        case CP_ACP:
        case CP_THREAD_ACP:

            // this fixes pop'n music's mojibake issue with the system locale not set to Japanese
            SetThreadLocale(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN));

            CodePage = CODEPAGE_SHIFT_JIS;

            break;

        default:
            break;
    }

    return MultiByteToWideChar_orig(
            CodePage,
            dwFlags,
            lpMultiByteStr,
            cbMultiByte,
            lpWideCharStr,
            cchWideChar);
}

void hooks::lang::early_init() {
    log_info("hooks::lang", "early initialization");

    // hooking these two functions fixes the jubeat mojibake
    detour::trampoline_try("kernel32.dll", "GetACP", GetACP_hook, &GetACP_orig);
    detour::trampoline_try("kernel32.dll", "GetOEMCP", GetOEMCP_hook, &GetOEMCP_orig);

#ifdef SPICE64 // SDVX5+ specific code
    bool isValkyrieCabinetMode = avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H';
    if (avs::game::is_model("KFC") && isValkyrieCabinetMode) {
        log_info("hooks::lang", "hooking GetSystemDefaultLCID");
        detour::trampoline_try(
            "kernel32.dll",
            "GetSystemDefaultLCID",
            GetSystemDefaultLCID_hook,
            &GetSystemDefaultLCID_orig);
    }
#endif

}

void hooks::lang::init() {
    log_info("hooks::lang", "initializing");

    detour::iat_try("RtlMultiByteToUnicodeN", RtlMultiByteToUnicodeN_hook, nullptr, "ntdll.dll");

    MultiByteToWideChar_orig = detour::iat_try(
            "MultiByteToWideChar",
            MultiByteToWideChar_hook,
            nullptr,
            "kernel32.dll");
}

bool hooks::lang::is_native_shiftjis() {
    return GetACP() == CODEPAGE_SHIFT_JIS;
}
