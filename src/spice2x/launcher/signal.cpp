#include "signal.h"

#include <algorithm>
#include <future>
#include <exception>

#include <windows.h>
#include <dbghelp.h>

#include "acio/acio.h"
#include "external/stackwalker/stackwalker.h"
#include "hooks/libraryhook.h"
#include "launcher/shutdown.h"
#include "util/deferlog.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/memutils.h"
#include "cfg/configurator.h"

#include "logger.h"

// MSVC compatibility
#ifdef exception_code
#undef exception_code
#endif

static decltype(MiniDumpWriteDump) *MiniDumpWriteDump_local = nullptr;
static LONG EXCEPTION_HANDLER_ACTIVE = 0;

namespace launcher::signal {

    // settings
    bool DISABLE = false;
    bool USE_VEH_WORKAROUND = false;
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

static const char *access_operation(ULONG_PTR operation) {
    switch (operation) {
        case 0:
            return "read";
        case 1:
            return "write";
        case 8:
            return "execute";
        default:
            return "unknown";
    }
}

static const char *memory_state(DWORD state) {
    switch (state) {
        case MEM_COMMIT:
            return "committed";
        case MEM_FREE:
            return "free";
        case MEM_RESERVE:
            return "reserved";
        default:
            return "unknown";
    }
}

static const char *memory_type(DWORD type) {
    switch (type) {
        case MEM_IMAGE:
            return "image";
        case MEM_MAPPED:
            return "mapped";
        case MEM_PRIVATE:
            return "private";
        default:
            return "none";
    }
}

static const char *memory_protection(DWORD protection) {
    switch (protection & 0xff) {
        case PAGE_EXECUTE:
            return "execute";
        case PAGE_EXECUTE_READ:
            return "execute-read";
        case PAGE_EXECUTE_READWRITE:
            return "execute-read-write";
        case PAGE_EXECUTE_WRITECOPY:
            return "execute-write-copy";
        case PAGE_NOACCESS:
            return "no-access";
        case PAGE_READONLY:
            return "read-only";
        case PAGE_READWRITE:
            return "read-write";
        case PAGE_WRITECOPY:
            return "write-copy";
        default:
            return "none";
    }
}

static void log_exception_parameters(const struct _EXCEPTION_RECORD *record) {
    const auto parameter_count = record->NumberParameters <= EXCEPTION_MAXIMUM_PARAMETERS ?
            record->NumberParameters : EXCEPTION_MAXIMUM_PARAMETERS;

    log_warning("signal", "exception flags: 0x{:08x}, parameters: {}",
            record->ExceptionFlags, parameter_count);

    for (DWORD parameter = 0; parameter < parameter_count; parameter++) {
#ifdef _WIN64
        log_warning("signal", "exception parameter[{}]: {:016x}",
                parameter, record->ExceptionInformation[parameter]);
#else
        log_warning("signal", "exception parameter[{}]: {:08x}",
                parameter, record->ExceptionInformation[parameter]);
#endif
    }
}

static bool query_memory(const void *address, MEMORY_BASIC_INFORMATION *memory) {
    return VirtualQuery(address, memory, sizeof(*memory)) == sizeof(*memory);
}

static void log_memory_region(const char *label, const void *address) {
    MEMORY_BASIC_INFORMATION memory {};
    if (!query_memory(address, &memory)) {
        log_warning("signal", "{}: VirtualQuery failed for {}: 0x{:08x}",
                label, fmt::ptr(address), GetLastError());
        return;
    }

    log_warning("signal",
            "{}: base={}, size=0x{:x}, allocation_base={}, state={} (0x{:x}), "
            "protect={} (0x{:x}), type={} (0x{:x})",
            label,
            fmt::ptr(memory.BaseAddress),
            memory.RegionSize,
            fmt::ptr(memory.AllocationBase),
            memory_state(memory.State),
            memory.State,
            memory_protection(memory.Protect),
            memory.Protect,
            memory_type(memory.Type),
            memory.Type);
}

static void log_exception_module(const void *address) {
    MEMORY_BASIC_INFORMATION memory {};
    if (!query_memory(address, &memory) || memory.Type != MEM_IMAGE || memory.AllocationBase == nullptr) {
        log_warning("signal", "exception module: unavailable");
        return;
    }

    char module_path[MAX_PATH] {};
    const auto module = static_cast<HMODULE>(memory.AllocationBase);
    const auto module_base = reinterpret_cast<uintptr_t>(memory.AllocationBase);
    const auto exception_address = reinterpret_cast<uintptr_t>(address);
    const auto module_length = GetModuleFileNameA(module, module_path, sizeof(module_path));

    if (module_length > 0) {
        const std::string module_name(module_path, std::min<size_t>(module_length, sizeof(module_path)));
        log_warning("signal", "exception module: '{}' base={}, rva=0x{:x}",
            module_name, fmt::ptr(memory.AllocationBase), exception_address - module_base);
    } else {
        log_warning("signal", "exception module: base={}, rva=0x{:x}",
                fmt::ptr(memory.AllocationBase), exception_address - module_base);
    }
}

static void log_instruction_bytes(const void *address) {
    MEMORY_BASIC_INFORMATION memory {};
    if (!query_memory(address, &memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        log_warning("signal", "instruction bytes: unavailable");
        return;
    }

    constexpr size_t max_instruction_bytes = 16;
    const auto region_end = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    const auto instruction_address = reinterpret_cast<uintptr_t>(address);
    const auto available = region_end > instruction_address ? region_end - instruction_address : 0;
    const auto byte_count = std::min(max_instruction_bytes, static_cast<size_t>(available));
    uint8_t bytes[max_instruction_bytes] {};
    SIZE_T bytes_read = 0;
    if (byte_count == 0 ||
        !ReadProcessMemory(GetCurrentProcess(), address, bytes, byte_count, &bytes_read)) {
        log_warning("signal", "instruction bytes: read failed at {}: 0x{:08x}",
                fmt::ptr(address), GetLastError());
        return;
    }

    std::string byte_string;
    byte_string.reserve(bytes_read * 3);

    for (size_t index = 0; index < bytes_read; index++) {
        fmt::format_to(std::back_inserter(byte_string), "{:02x}{}",
                bytes[index], index + 1 < bytes_read ? " " : "");
    }

    log_warning("signal", "instruction bytes: {}", byte_string);
}

static void log_exception_context(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    const auto *record = ExceptionInfo->ExceptionRecord;
    const auto *context = ExceptionInfo->ContextRecord;

    log_warning("signal", "thread id: {}", GetCurrentThreadId());
    log_warning("signal", "exception address: {}", fmt::ptr(record->ExceptionAddress));
    log_exception_module(record->ExceptionAddress);
    log_instruction_bytes(record->ExceptionAddress);
    log_exception_parameters(record);

    if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters >= 2) {
        const auto operation = record->ExceptionInformation[0];
        const auto address = reinterpret_cast<const void *>(record->ExceptionInformation[1]);
        log_warning("signal", "invalid memory access: {} at {}",
                access_operation(operation), fmt::ptr(address));
        log_memory_region("fault memory", address);
    }

    if (context == nullptr) {
        return;
    }

#ifdef _WIN64
    log_warning("signal", "registers: rax={:016x} rbx={:016x} rcx={:016x} rdx={:016x}",
            context->Rax, context->Rbx, context->Rcx, context->Rdx);
    log_warning("signal", "registers: rsi={:016x} rdi={:016x} rbp={:016x} rsp={:016x}",
            context->Rsi, context->Rdi, context->Rbp, context->Rsp);
    log_warning("signal", "registers: r8={:016x} r9={:016x} r10={:016x} r11={:016x}",
            context->R8, context->R9, context->R10, context->R11);
    log_warning("signal", "registers: r12={:016x} r13={:016x} r14={:016x} r15={:016x}",
            context->R12, context->R13, context->R14, context->R15);
    log_warning("signal", "registers: rip={:016x} eflags={:08x}",
            context->Rip, context->EFlags);
#else
    log_warning("signal", "registers: eax={:08x} ebx={:08x} ecx={:08x} edx={:08x}",
            context->Eax, context->Ebx, context->Ecx, context->Edx);
    log_warning("signal", "registers: esi={:08x} edi={:08x} ebp={:08x} esp={:08x}",
            context->Esi, context->Edi, context->Ebp, context->Esp);
    log_warning("signal", "registers: eip={:08x} eflags={:08x}",
            context->Eip, context->EFlags);
#endif
}

static void write_minidump(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    if (MiniDumpWriteDump_local == nullptr) {
        log_warning("signal", "minidump creation function not available, skipping");
        return;
    }

    HANDLE minidump_file = CreateFileA(
        "minidump.dmp",
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (minidump_file == INVALID_HANDLE_VALUE) {
        log_warning("signal", "failed to create 'minidump.dmp' for minidump: 0x{:08x}",
                GetLastError());
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION ExceptionParam {};
    ExceptionParam.ThreadId = GetCurrentThreadId();
    ExceptionParam.ExceptionPointers = ExceptionInfo;
    ExceptionParam.ClientPointers = FALSE;

    constexpr auto extended_type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithUnloadedModules |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithProcessThreadData |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithThreadInfo |
            MiniDumpIgnoreInaccessibleMemory);

    auto written_type = extended_type;
    auto minidump_written = MiniDumpWriteDump_local(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        minidump_file,
        extended_type,
        &ExceptionParam,
        nullptr,
        nullptr);
    auto minidump_error = minidump_written ? ERROR_SUCCESS : GetLastError();

    if (!minidump_written) {
        log_warning("signal", "failed to write extended minidump: 0x{:08x}; retrying MiniDumpNormal",
                minidump_error);

        LARGE_INTEGER file_start {};
        if (SetFilePointerEx(minidump_file, file_start, nullptr, FILE_BEGIN) &&
            SetEndOfFile(minidump_file)) {
            written_type = MiniDumpNormal;
            minidump_written = MiniDumpWriteDump_local(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                minidump_file,
                MiniDumpNormal,
                &ExceptionParam,
                nullptr,
                nullptr);
            minidump_error = minidump_written ? ERROR_SUCCESS : GetLastError();
        } else {
            minidump_error = GetLastError();
        }
    }

    CloseHandle(minidump_file);

    if (minidump_written) {
        log_info("signal", "wrote minidump to 'minidump.dmp' (type=0x{:08x})",
                static_cast<unsigned>(written_type));
    } else {
        log_warning("signal", "failed to write 'minidump.dmp': 0x{:08x}", minidump_error);
    }
}

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
    if (!launcher::signal::DISABLE &&
        ExceptionInfo != nullptr &&
        ExceptionInfo->ExceptionRecord != nullptr &&
        InterlockedCompareExchange(&EXCEPTION_HANDLER_ACTIVE, 1, 0) == 0) {

        // get exception record
        struct _EXCEPTION_RECORD *ExceptionRecord = ExceptionInfo->ExceptionRecord;

        // print signal
        log_warning("signal", "exception raised: {}", exception_code(ExceptionRecord));
        log_exception_context(ExceptionInfo);

        switch (ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                deferredlogs::defer_error_messages({
                    "illegal instruction exception:",
                    "    either your CPU is too old (e.g., does not support SSE4.2 or AVX2 ",
                    "    but perhaps the game requires it); or, a bad patch was applied."
                    });
                break;
            default:
                break;
        }

        // check ACIO init failures
        if (acio::IO_INIT_IN_PROGRESS) {
            deferredlogs::defer_error_messages({
                "exception raised during ACIO init, this usually happens when ",
                "    a third party application interferes with hooks",
                "    please check for the following, disable them, and try launching the game again:",
                "      * RivaTuner Statistics Server (RTSS)",
                "      * MSI Afterburner",
                "      * kernel mode anti-cheat"
            });
        }

        // dump deferred logs BEFORE stack trace since some errors cause stack trace logic to hang
        // (e.g., ACIO init hang due to RTSS)
        deferredlogs::dump_to_logger(true);

        // walk the exception chain
        struct _EXCEPTION_RECORD *record_cause = ExceptionRecord->ExceptionRecord;
        while (record_cause != nullptr) {
            log_warning("signal", "caused by: {} at {}",
                    exception_code(record_cause), fmt::ptr(record_cause->ExceptionAddress));
            log_exception_parameters(record_cause);
            record_cause = record_cause->ExceptionRecord;
        }

        // write the minidump before stack walking, which may hang on a damaged stack
        write_minidump(ExceptionInfo);

        // print stacktrace
        StackWalker sw;
        log_info("signal", "printing callstack");
        if (!sw.ShowCallstack(GetCurrentThread(), ExceptionInfo->ContextRecord)) {
            log_warning("signal", "failed to print callstack");
        }

        // dump memory information
        memutils::show_available_memory();

        // this will stall all UI threads for this process
        show_popup_for_crash();

        log_fatal("signal", "end");

        InterlockedExchange(&EXCEPTION_HANDLER_ACTIVE, 0);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL WINAPI SetConsoleCtrlHandler_hook(PHANDLER_ROUTINE pHandlerRoutine, BOOL Add) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_misc("signal", "SetConsoleCtrlHandler hook hit (printing once)");
    });

    return TRUE;
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter_hook(
    LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {

    static std::once_flag printed;
    std::call_once(printed, []() {
        log_info("signal", "SetUnhandledExceptionFilter hook hit (printing once)");
    });

    return nullptr;
}

static PVOID WINAPI AddVectoredExceptionHandler_hook(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler) {
    static std::once_flag printed;
    std::call_once(printed, []() {
        log_info("signal", "AddVectoredExceptionHandler hook hit (printing once)");
    });

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
