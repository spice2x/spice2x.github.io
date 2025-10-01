#include "signal.h"

#include <exception>

#include <windows.h>
#include <dbghelp.h>

#include "avs/core.h"
#include "avs/game.h"
#include "acio/acio.h"
#include "external/stackwalker/stackwalker.h"
#include "hooks/libraryhook.h"
#include "launcher/shutdown.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "cfg/configurator.h"

#include "logger.h"

// MSVC compatibility
#ifdef exception_code
#undef exception_code
#endif

static decltype(MiniDumpWriteDump) *MiniDumpWriteDump_local = nullptr;

namespace launcher::signal {

    // settings
    bool DISABLE = false;
    bool USE_VEH_WORKAROUND = false;

    // states
    bool SUPERSTEP_SOUND_ERROR = false;
    bool AVS_DIR_CREATION_FAILURE = false;
    std::string AVS_SRC_PATH;
    bool D3D9_CREATE_DEVICE_FAILED = false;
    uint32_t D3D9_CREATE_DEVICE_FAILED_HRESULT;
}

#define V(variant) case variant: return #variant

static std::string control_code(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        V(CTRL_C_EVENT);
        V(CTRL_BREAK_EVENT);
        V(CTRL_CLOSE_EVENT);
        V(CTRL_LOGOFF_EVENT);
        V(CTRL_SHUTDOWN_EVENT);
        default:
            return "Unknown(0x" + to_hex(dwCtrlType) + ")";
    }
}

static std::string exception_code(struct _EXCEPTION_RECORD *ExceptionRecord) {
    switch (ExceptionRecord->ExceptionCode) {
        V(EXCEPTION_ACCESS_VIOLATION);
        V(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
        V(EXCEPTION_BREAKPOINT);
        V(EXCEPTION_DATATYPE_MISALIGNMENT);
        V(EXCEPTION_FLT_DENORMAL_OPERAND);
        V(EXCEPTION_FLT_DIVIDE_BY_ZERO);
        V(EXCEPTION_FLT_INEXACT_RESULT);
        V(EXCEPTION_FLT_INVALID_OPERATION);
        V(EXCEPTION_FLT_OVERFLOW);
        V(EXCEPTION_FLT_STACK_CHECK);
        V(EXCEPTION_FLT_UNDERFLOW);
        V(EXCEPTION_ILLEGAL_INSTRUCTION);
        V(EXCEPTION_IN_PAGE_ERROR);
        V(EXCEPTION_INT_DIVIDE_BY_ZERO);
        V(EXCEPTION_INT_OVERFLOW);
        V(EXCEPTION_INVALID_DISPOSITION);
        V(EXCEPTION_NONCONTINUABLE_EXCEPTION);
        V(EXCEPTION_PRIV_INSTRUCTION);
        V(EXCEPTION_SINGLE_STEP);
        V(EXCEPTION_STACK_OVERFLOW);
        V(DBG_CONTROL_C);
        default:
            return "Unknown(0x" + to_hex(ExceptionRecord->ExceptionCode) + ")";
    }
}

#undef V

static BOOL WINAPI HandlerRoutine(DWORD dwCtrlType) {
    log_info("signal", "console ctrl handler called: {}", control_code(dwCtrlType));

    if (dwCtrlType == CTRL_C_EVENT) {
        launcher::shutdown();
    } else if (dwCtrlType == CTRL_CLOSE_EVENT) {
        launcher::shutdown();
    }

    return FALSE;
}

static LONG WINAPI TopLevelExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo) {

    // ignore signal if disabled or no exception info provided
    if (!launcher::signal::DISABLE && ExceptionInfo != nullptr) {

        // get exception record
        struct _EXCEPTION_RECORD *ExceptionRecord = ExceptionInfo->ExceptionRecord;

        // print signal
        log_warning("signal", "exception raised: {}", exception_code(ExceptionRecord));

        std::string err;
        switch (ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                err = "Illegal instruction: either your CPU is too old (e.g., does not support "
                    "SSE4.2 or AVX2 but perhaps the game requires it); or, a bad patch was applied.";
                break;
            default:
                break;
        }
        if (!err.empty()) {
            log_warning(
                "signal",
                "likely cause for your error based on the exception code:\n    {}",
                err.c_str());
        }

        // check ACIO init failures
        if (acio::IO_INIT_IN_PROGRESS) {
            log_warning(
                "signal",
                "exception raised during ACIO init, this usually happens when a third party application interferes with hooks");
            log_warning(
                "signal",
                "    please check for the following, disable them, and try launching the game again:");
            log_warning(
                "signal",
                "    RivaTuner Statistics Server (RTSS), MSI Afterburner, kernel mode anti-cheat");
        }

        if (launcher::signal::SUPERSTEP_SOUND_ERROR) {
            log_warning("signal", "audio initialization error was previously detected during boot!");
            log_warning("signal", "    (W:SuperstepSound: Audio device is not available!!!)");
            log_warning("signal", "    this crash is most likely related to audio init failure");
            log_warning("signal", "    fix your audio device, double check your audio options/patches, and try again");
        }

        if (launcher::signal::AVS_DIR_CREATION_FAILURE) {
            log_warning("signal",
                "AVS filesystem initialization failure was previously detected during boot!");
            log_warning("signal",
                "    ERROR: directory could not be created: {}",
                launcher::signal::AVS_SRC_PATH.c_str());
            log_warning("signal",
                "    this crash may have been caused by bad <mounttable> contents in {}",
                avs::core::CFG_PATH.c_str());
            log_warning("signal", "    fix the XML file and try again");
        }

        if (launcher::signal::D3D9_CREATE_DEVICE_FAILED) {
            log_warning("signal",
                "D3D9 CreateDevice/CreateDeviceEx failed with {:#x}!",
                launcher::signal::D3D9_CREATE_DEVICE_FAILED_HRESULT);

            log_warning("signal",
                "    this is a common graphics / monitor issue");
            log_warning("signal",
                "    double check any graphics options you configured in spicecfg");
            log_warning("signal",
                "    double check that your monitor supports the resolution + refresh rate that the game needs");
            log_warning("signal",
                "    enable GPU-side resolution scaling in your GPU options as needed");
            log_warning("signal",
                "    if you have three or more monitors, try unplugging them down to one or two, or enable -graphics-force-single-adapter option");
            log_warning("signal",
                "    failing all that, see if enabling windowed mode helps");
        }

        // walk the exception chain
        struct _EXCEPTION_RECORD *record_cause = ExceptionRecord->ExceptionRecord;
        while (record_cause != nullptr) {
            log_warning("signal", "caused by: {}", exception_code(record_cause));
            record_cause = record_cause->ExceptionRecord;
        }

        // print stacktrace
        StackWalker sw;
        log_info("signal", "printing callstack");
        if (!sw.ShowCallstack(GetCurrentThread(), ExceptionInfo->ContextRecord)) {
            log_warning("signal", "failed to print callstack");
        }

        if (MiniDumpWriteDump_local != nullptr) {
            HANDLE minidump_file = CreateFileA(
                "minidump.dmp",
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (minidump_file != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION ExceptionParam {};
                ExceptionParam.ThreadId = GetCurrentThreadId();
                ExceptionParam.ExceptionPointers = ExceptionInfo;
                ExceptionParam.ClientPointers = FALSE;

                MiniDumpWriteDump_local(
                    GetCurrentProcess(),
                    GetCurrentProcessId(),
                    minidump_file,
                    MiniDumpNormal,
                    &ExceptionParam,
                    nullptr,
                    nullptr);

                CloseHandle(minidump_file);
            } else {
                log_warning("signal", "failed to create 'minidump.dmp' for minidump: 0x{:08x}",
                    GetLastError());
            }
        } else {
            log_warning("signal", "minidump creation function not available, skipping");
        }

        log_fatal("signal", "end");
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL WINAPI SetConsoleCtrlHandler_hook(PHANDLER_ROUTINE pHandlerRoutine, BOOL Add) {
    log_misc("signal", "SetConsoleCtrlHandler hook hit");

    return TRUE;
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter_hook(
    LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    log_info("signal", "SetUnhandledExceptionFilter hook hit");

    return nullptr;
}

static PVOID WINAPI AddVectoredExceptionHandler_hook(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
    log_info("signal", "AddVectoredExceptionHandler hook hit");

    return launcher::signal::USE_VEH_WORKAROUND ? INVALID_HANDLE_VALUE : nullptr;
}

void launcher::signal::attach() {

    if (launcher::signal::DISABLE) {
        return;
    }

    log_info("signal", "attaching...");

    // set a `std::terminate` handler so `std::abort()` is not called by default
    std::set_terminate([]() {
        log_warning("signal", "std::terminate called");

        launcher::kill();
    });

    // NOTE: inline hooks are not used here as they have caused EXCEPTION_ACCESS_VIOLATION in the past
    // when hooking these methods

    // hook relevant functions
    libraryhook_hook_proc("SetConsoleCtrlHandler", SetConsoleCtrlHandler_hook);
    libraryhook_hook_proc("SetUnhandledExceptionFilter", SetUnhandledExceptionFilter_hook);
    libraryhook_hook_proc("AddVectoredExceptionHandler", AddVectoredExceptionHandler_hook);
    libraryhook_enable();

    // hook in all loaded modules
    detour::iat_try("SetConsoleCtrlHandler", SetConsoleCtrlHandler_hook);
    detour::iat_try("SetUnhandledExceptionFilter", SetUnhandledExceptionFilter_hook);
    detour::iat_try("AddVectoredExceptionHandler", AddVectoredExceptionHandler_hook);

    log_info("signal", "attached");
}

void launcher::signal::init() {

    // load debug help library
    if (!cfg::CONFIGURATOR_STANDALONE) {
        auto dbghelp = libutils::try_library("dbghelp.dll");

        if (dbghelp != nullptr) {
            MiniDumpWriteDump_local = libutils::try_proc<decltype(MiniDumpWriteDump) *>(
                    dbghelp, "MiniDumpWriteDump");
        }
    }

    // register our console ctrl handler
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    // register our exception handler
    SetUnhandledExceptionFilter(TopLevelExceptionFilter);
}
