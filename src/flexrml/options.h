#pragma once

#include <string>
#include <unordered_map>

struct Options {
  std::string mapping_source;
  std::string output_file_path;
  std::string base_uri = "http://example.com/base/";
  std::string threading_enabled = "true";
  std::string materialize_constants = "true";
  std::string heuristic_ordering = "true";
  std::unordered_map<std::string, std::string> in_memory_sources;
  bool mapping_source_is_string = false;
  bool generate_plan = false;
};
