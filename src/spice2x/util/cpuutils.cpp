#include "cpuutils.h"

#include <thread>

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include <winternl.h>
#include <ntstatus.h>

#include "cpuinfo_x86.h"

#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/unique_plain_ptr.h"

// redefinition of PROCESSOR_RELATIONSHIP; only win10 exposes EfficiencyClass field
// instead of setting _WIN32_WINNT to win10 we'll just redefine it to avoid the compat headache
typedef struct _PROCESSOR_RELATIONSHIP_WIN10 {
    BYTE Flags;
    BYTE EfficiencyClass;
    BYTE Reserved[20];
    WORD GroupCount;
    GROUP_AFFINITY GroupMask[ANYSIZE_ARRAY];
} PROCESSOR_RELATIONSHIP_WIN10, *PPROCESSOR_RELATIONSHIP_WIN10;

/*
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
*/

using namespace cpu_features;

namespace cpuutils {

    typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
        LARGE_INTEGER IdleTime;
        LARGE_INTEGER KernelTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER DpcTime;
        LARGE_INTEGER InterruptTime;
        ULONG         InterruptCount;
    } SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, *PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

    typedef NTSTATUS (WINAPI *NtQuerySystemInformation_t)(
            DWORD SystemInformationClass,
            PVOID SystemInformation,
            ULONG SystemInformationLength,
            PULONG ReturnLength
    );
    static NtQuerySystemInformation_t NtQuerySystemInformation = nullptr;

    typedef BOOL (WINAPI *GetLogicalProcessorInformationEx_t)(
            LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer,
            PDWORD ReturnedLength
    );
    static GetLogicalProcessorInformationEx_t GetLogicalProcessorInformationEx = nullptr;

    typedef void (WINAPI *GetCurrentProcessorNumberEx_t)(
            PPROCESSOR_NUMBER ProcNumber
    );
    static GetCurrentProcessorNumberEx_t GetCurrentProcessorNumberEx = nullptr;

    static size_t PROCESSOR_COUNT = 0;
    static std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> PROCESSOR_STATES;
    static USHORT PRIMARY_GROUP = UINT16_MAX;

    static void init() {

        // check if done
        static bool done = false;
        if (done) {
            return;
        }

        // get pointers
        if (NtQuerySystemInformation == nullptr) {
            auto ntdll = libutils::try_module("ntdll.dll");

            if (ntdll != nullptr) {
                NtQuerySystemInformation = libutils::try_proc<NtQuerySystemInformation_t>(
                        ntdll, "NtQuerySystemInformation");

                if (NtQuerySystemInformation == nullptr) {
                    log_warning("cpuutils", "NtQuerySystemInformation not found");
                }
            }
        }

        // get system info
        SYSTEM_INFO info {};
        GetSystemInfo(&info);
        PROCESSOR_COUNT = info.dwNumberOfProcessors;
        log_misc("cpuutils", "detected {} processors", PROCESSOR_COUNT);

        done = true;
        
        // init processor states
        get_load();
    }

    static void init_kernel32_routines() {
        auto kernel32 = libutils::try_module("kernel32.dll");
        if (kernel32 == nullptr) {
            log_warning("cpuutils", "failed to find kernel32");
            return;
        }

        if (GetLogicalProcessorInformationEx == nullptr) {
            GetLogicalProcessorInformationEx = libutils::try_proc<GetLogicalProcessorInformationEx_t>(
                    kernel32, "GetLogicalProcessorInformationEx");
            if (GetLogicalProcessorInformationEx == nullptr) {
                log_warning("cpuutils", "GetLogicalProcessorInformationEx not found");
            }
        }

        if (GetCurrentProcessorNumberEx == nullptr) {
            GetCurrentProcessorNumberEx = libutils::try_proc<GetCurrentProcessorNumberEx_t>(
                    kernel32, "GetCurrentProcessorNumberEx");
            if (GetCurrentProcessorNumberEx == nullptr) {
                log_warning("cpuutils", "GetCurrentProcessorNumberEx not found");
            }
        }

        // figure out the Primary Group for this process
        // if GetCurrentProcessorNumberEx isn't supported, assume OS only allows single-group
        // https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
        if (GetCurrentProcessorNumberEx != nullptr && PRIMARY_GROUP == UINT16_MAX) {
            PROCESSOR_NUMBER ProcNumber;
            GetCurrentProcessorNumberEx(&ProcNumber);
            PRIMARY_GROUP = ProcNumber.Group;
            log_misc("cpuutils", "primary group: {}", PRIMARY_GROUP);
        }
    }

    void print_cpu_features() {
        log_misc("cpuinfo", "dumping processor information...");
        const auto cpu = GetX86Info();

        // dump cpu id
        log_misc("cpuinfo", "{}, {} ({})",
            cpu.vendor,
            cpu.brand_string, 
            GetX86MicroarchitectureName(GetX86Microarchitecture(&cpu)));
        log_misc("cpuinfo", "family 0x{:x}, model 0x{:x}, stepping 0x{:x}", cpu.family, cpu.model, cpu.stepping);

        // dump features        
        std::string features = "";
        for (size_t i = 0; i < X86_LAST_; ++i) {
            if (GetX86FeaturesEnumValue(&cpu.features, static_cast<X86FeaturesEnum>(i))) {
                features += GetX86FeaturesEnumName(static_cast<X86FeaturesEnum>(i));
                features += " ";
            }
        }
        log_misc("cpuinfo", "features : {}", features);
        log_misc("cpuinfo", "  SSE4.2 : {}", cpu.features.sse4_2 ? "supported" : "NOT supported");
        log_misc("cpuinfo", "  AVX2   : {}", cpu.features.avx2 ? "supported" : "NOT supported");
    }

    std::vector<float> get_load() {

        // lazy init
        cpuutils::init();
        std::vector<float> cpu_load_values;

        // query system information
        if (NtQuerySystemInformation) {
            auto ppi = std::make_unique<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION[]>(PROCESSOR_COUNT);
            ULONG ret_len = sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * PROCESSOR_COUNT;
            NTSTATUS ret;
            if (NT_SUCCESS(ret = NtQuerySystemInformation(8, ppi.get(), ret_len, &ret_len))) {

                // check cpu core count
                auto count = ret_len / sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);
                for (size_t i = 0; i < count; i++) {
                    auto &pi = ppi[i];

                    // get old state
                    if (PROCESSOR_STATES.size() <= i) {
                        PROCESSOR_STATES.push_back(pi);
                    }
                    auto &pi_old = PROCESSOR_STATES[i];

                    // get delta state
                    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION delta;
                    delta.DpcTime.QuadPart = pi.DpcTime.QuadPart - pi_old.DpcTime.QuadPart;
                    delta.InterruptTime.QuadPart = pi.InterruptTime.QuadPart - pi_old.InterruptTime.QuadPart;
                    delta.UserTime.QuadPart = pi.UserTime.QuadPart - pi_old.UserTime.QuadPart;
                    delta.KernelTime.QuadPart = pi.KernelTime.QuadPart - pi_old.KernelTime.QuadPart;
                    delta.IdleTime.QuadPart = pi.IdleTime.QuadPart - pi_old.IdleTime.QuadPart;

                    // calculate total time run
                    LARGE_INTEGER time_run {
                        .QuadPart = delta.DpcTime.QuadPart
                                + delta.InterruptTime.QuadPart
                                + delta.UserTime.QuadPart
                                + delta.KernelTime.QuadPart
                    };
                    if (time_run.QuadPart == 0) {
                        time_run.QuadPart = 1;
                    }

                    // calculate CPU load
                    cpu_load_values.emplace_back(MIN(MAX(1.f - (
                            (float) delta.IdleTime.QuadPart / (float) time_run.QuadPart), 0.f), 1.f) * 100.f);

                    // save state
                    PROCESSOR_STATES[i] = pi;
                }
            } else {
                log_warning("cpuutils", "NtQuerySystemInformation failed: {}", ret);
            }
        }

        // return data
        return cpu_load_values;
    }

    void set_processor_priority(std::string priority) {
        DWORD process_priority = HIGH_PRIORITY_CLASS;
        if (priority == "belownormal") {
            process_priority = BELOW_NORMAL_PRIORITY_CLASS;
        } else if (priority == "normal") {
            process_priority = NORMAL_PRIORITY_CLASS;
        } else if (priority == "abovenormal") {
            process_priority = ABOVE_NORMAL_PRIORITY_CLASS;
        // high is the default so it's skipped!
        } else if (priority == "realtime") {
            process_priority = REALTIME_PRIORITY_CLASS;
        }
        // while testing, realtime only worked when being set to high before
        if (process_priority == REALTIME_PRIORITY_CLASS) {
            if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
                log_warning("cpuutils", "could not set process priority to high, GLE:{}", GetLastError());
            }
        }
        if (!SetPriorityClass(GetCurrentProcess(), process_priority)) {
            log_warning("cpuutils", "could not set process priority to {}, GLE:{}", priority, GetLastError());
        } else {
            log_info("cpuutils", "SetPriorityClass succeeded, set priority to {}", priority);
        }
    }

    void set_processor_affinity(CpuEfficiencyClass eff_class) {
        DWORD returned_length;
        BOOL result;

        init_kernel32_routines();
        if (GetLogicalProcessorInformationEx == nullptr) {
            return;
        }
        
        // determine buffer size
        returned_length = 0;
        result = GetLogicalProcessorInformationEx(
                    RelationProcessorCore,
                    nullptr,
                    &returned_length);
        if (result || GetLastError() != ERROR_INSUFFICIENT_BUFFER || returned_length == 0) {
            log_warning("cpuutils", "unexpected return from GetLogicalProcessorInformationEx");
            return;
        }

        const auto buffer =
            util::make_unique_plain<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(returned_length);

        result = GetLogicalProcessorInformationEx(
                    RelationProcessorCore,
                    buffer.get(),
                    &returned_length);
        if (!result) {
            log_warning(
                "cpuutils",
                "unexpected return from GetLogicalProcessorInformationEx, GLE:{}",
                GetLastError());
            return;
        }

        KAFFINITY affinity_eff_0 = 0;
        KAFFINITY affinity_eff_non_0 = 0;
        DWORD_PTR byte_offset = 0;
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX procs = buffer.get();
        while (byte_offset < returned_length) {
            // ignore processors outside of primary group for this processor
            // (GroupCount is always 1 for RelationProcessorCore)
            if (procs->Processor.GroupMask[0].Group != PRIMARY_GROUP) {
                continue;
            }


            // check efficiency class and add up affinities
            PPROCESSOR_RELATIONSHIP_WIN10 relationship =
                (PPROCESSOR_RELATIONSHIP_WIN10)&procs->Processor;
            if (relationship->EfficiencyClass == 0) {
                affinity_eff_0 |= procs->Processor.GroupMask[0].Mask;
            } else {
                affinity_eff_non_0 |= procs->Processor.GroupMask[0].Mask;
            }

            // debug info
            // log_info("cpuutils", "eff = {}, 0x{:x}", relationship->EfficiencyClass, procs->Processor.GroupMask[0].Mask);

            // move onto next entry
            byte_offset += procs->Size;
            procs = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(((PBYTE)procs) + procs->Size);
        }

        if (affinity_eff_non_0 == 0) {
            log_warning("cpuutils", "not a heterogeneous system, or OS doesn't understand it; ignoring -processefficiency");
        } else if (eff_class == CpuEfficiencyClass::PreferECores) {
            log_info("cpuutils", "force efficient cores: 0x{:x}", affinity_eff_0);
            set_processor_affinity(affinity_eff_0, true);
        } else if (eff_class == CpuEfficiencyClass::PreferPCores) {
            log_info("cpuutils", "force performant cores: 0x{:x}", affinity_eff_non_0);
            set_processor_affinity(affinity_eff_non_0, true);
        }
    }

    void set_processor_affinity(uint64_t affinity, bool is_user_override) {
        // two possible sources: user sets a parameter, or game needs errata
        static bool is_user_override_set = false;
        if (is_user_override) {
            is_user_override_set = true;
        } else if (is_user_override_set) {
            log_misc(
                "cpuutils",
                "ignoring call to set_processor_affinity for 0x{:x}, user already set affinity override",
                affinity);
            return;
        }

        // get system affinity
        DWORD_PTR sys_affinity;
        DWORD_PTR proc_affinity;
        if (GetProcessAffinityMask(GetCurrentProcess(), &proc_affinity, &sys_affinity) != 0) {
            log_misc(
                "cpuutils",
                "GetProcessAffinityMask: process=0x{:x}, system=0x{:x}",
                proc_affinity, sys_affinity);
        } else {
            const auto gle = GetLastError();
            if (gle == ERROR_INVALID_PARAMETER) {
                log_fatal("cpuutils", "GetProcessAffinityMask failed, GLE: ERROR_INVALID_PARAMETER.");
            } else {
                log_fatal("cpuutils", "GetProcessAffinityMask failed, GLE: {}", gle);
            }
        }

        DWORD_PTR affinity_to_apply = sys_affinity & (DWORD_PTR)affinity;
        log_info(
            "cpuutils",
            "affinity mask: 0x{:x} & 0x{:x} = 0x{:x}",
            sys_affinity, (DWORD_PTR)affinity, affinity_to_apply);

        if (affinity_to_apply == proc_affinity) {
            log_misc(
                "cpuutils",
                "no need to call GetProcessAffinityMask, process affinity is already the desired value");
            return;
        }

        // call SetProcessAffinityMask; failures are fatal
        if (SetProcessAffinityMask(GetCurrentProcess(), affinity_to_apply) != 0) {
            log_info(
                "cpuutils",
                "SetProcessAffinityMask succeeded, affinity set to 0x{:x}",
                affinity_to_apply);
        } else {
            const auto gle = GetLastError();
            if (gle == ERROR_INVALID_PARAMETER) {
                log_fatal(
                    "cpuutils",
                    "SetProcessAffinityMask failed, provided 0x{:x}, GLE: ERROR_INVALID_PARAMETER.",
                    affinity_to_apply);
            } else {
                log_fatal(
                    "cpuutils",
                    "SetProcessAffinityMask failed, provided 0x{:x}, GLE: {}",
                    affinity_to_apply,
                    gle);
            }
        }
    }
}
