#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace cpuutils {
    enum class CpuEfficiencyClass {
        PreferECores,
        PreferPCores
    };

    std::vector<float> get_load();
    void print_cpu_features();
    void set_processor_priority(std::string priority);
    void set_processor_affinity(uint64_t affinity, bool is_user_override);
    void set_processor_affinity(CpuEfficiencyClass eff_class);
}
