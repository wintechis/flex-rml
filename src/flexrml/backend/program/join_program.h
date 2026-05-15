#pragma once

#include <string>
#include <vector>

#include "term_cache.h"
#include "term_program.h"

struct CompiledComplexRenderPlan {
  CompiledRuntimeTerm subject;
  CompiledRuntimeTerm predicate;
  CompiledRuntimeTerm object;
  CompiledRuntimeTerm graph;
  int subject_cache_id = -1;
  int predicate_cache_id = -1;
  int object_cache_id = -1;
  int graph_cache_id = -1;
  std::vector<TermCacheEntry> term_cache_entries;
  bool has_graph = false;
  bool needs_row_map = false;
  bool has_object_condition = false;
};

CompiledComplexRenderPlan compile_complex_render_plan(const std::vector<std::string>& joined_headers,
                                                      const std::string& base_uri,
                                                      const std::vector<std::string>& s_content,
                                                      const std::vector<std::string>& p_content,
                                                      const std::vector<std::string>& o_content,
                                                      const std::vector<std::string>* g_content = nullptr);
