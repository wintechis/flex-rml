#pragma once

#include <filesystem>
#include <string>
#include <vector>

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
