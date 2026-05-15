#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "complex_plan.h"

size_t execute_standalone_complex_plan(const ComplexPlan& info, const std::unordered_map<std::string, std::string>& data_map);
std::unordered_set<std::string> execute_dependent_complex_plan(const ComplexPlan& info, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map);
