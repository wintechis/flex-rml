#pragma once

#include <string>

size_t standalone_complex_mapping(const std::string& information);
std::unordered_set<std::string> dependent_complex_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple);