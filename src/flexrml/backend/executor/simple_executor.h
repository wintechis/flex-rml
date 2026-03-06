#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

size_t standalone_simple_mapping(const std::string& information, const std::unordered_map<std::string, std::string>& data_map);
std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map);