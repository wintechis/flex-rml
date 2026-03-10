#pragma once

#include <string>
#include <vector>
#include <chrono>

inline std::vector<std::string> values_to_skip = {"NULL", ""};
inline bool continue_on_error = false;

// Generate random seed based on clock
inline const std::uint64_t g_seed64 = [] {
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}();