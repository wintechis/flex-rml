#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

struct SimplePlan {
  std::filesystem::path output_file_name;
  std::string base_uri;
  std::string input_file_name;
  std::vector<std::string> projected_attributes;
  std::vector<std::string> s_content;
  std::vector<std::string> p_content;
  std::vector<std::string> o_content;
  std::vector<std::string> g_content;
  bool generate_graph = false;
};

SimplePlan parse_simple_plan(const std::string& information);
size_t execute_standalone_simple_plan(const SimplePlan& info, const std::unordered_map<std::string, std::string>& data_map);
std::unordered_set<std::string> execute_dependent_simple_plan(const SimplePlan& info, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map);

size_t standalone_simple_mapping(const std::string& information, const std::unordered_map<std::string, std::string>& data_map);
std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map);
