#pragma once

#include <cstdint>

void init_performance_counter();
double get_performance_seconds();
double get_performance_milliseconds();
uint64_t get_system_seconds();
uint64_t get_system_milliseconds();
