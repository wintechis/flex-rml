#pragma once

#include <string>
#include <vector>
#include <unordered_set>

size_t standalone_simple_mapping(const std::string& information);
std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple);