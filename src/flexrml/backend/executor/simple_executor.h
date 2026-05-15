#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include "simple_plan.h"

class OutputChunkWriter {
 public:
  virtual ~OutputChunkWriter() = default;
  virtual void write(std::string chunk) = 0;
};

SimplePlan parse_simple_plan(const std::string& information);
size_t execute_standalone_simple_plan(const SimplePlan& info, const std::unordered_map<std::string, std::string>& data_map);
size_t execute_standalone_simple_plan(const SimplePlan& info, const std::unordered_map<std::string, std::string>& data_map, OutputChunkWriter* writer);
size_t execute_fused_simple_plans(const std::vector<SimplePlan>& plans, const std::unordered_map<std::string, std::string>& data_map, OutputChunkWriter* writer);
std::unordered_set<std::string> execute_dependent_simple_plan(const SimplePlan& info, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map);

size_t standalone_simple_mapping(const std::string& information, const std::unordered_map<std::string, std::string>& data_map);
std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map);
