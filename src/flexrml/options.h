#pragma once

#include <string>

struct Options {
  std::string mapping_source;
  std::string output_file_path;
  std::string base_uri = "http://example.com/base/";
  std::string threading_enabled = "true";
  std::string materialize_constants = "true";
  std::string heuristic_ordering = "true";
  bool generate_plan = false;
};
