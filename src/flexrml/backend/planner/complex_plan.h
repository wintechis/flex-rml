#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ComplexPlan {
  std::filesystem::path output_file_name;
  std::string base_uri;
  std::string left_path;
  std::string right_path;
  std::string left_name;
  std::string right_name;
  std::vector<std::string> projected_attributes_left;
  std::vector<std::string> projected_attributes_right;
  std::vector<std::string> left_join_attrs;
  std::vector<std::string> right_join_attrs;
  std::vector<std::string> s_content;
  std::vector<std::string> p_content;
  std::vector<std::string> o_content;
  std::vector<std::string> g_content;
  bool generate_graph = false;
};
