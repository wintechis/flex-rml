#pragma once

#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "simple_plan.h"
#include "term_cache.h"
#include "term_program.h"

struct CompiledSimplePlan {
  CompiledRuntimeTerm subject;
  CompiledRuntimeTerm predicate;
  CompiledRuntimeTerm object;
  CompiledRuntimeTerm graph;
  bool has_graph = false;
  bool needs_row_map = false;
  bool function_called = false;
  bool has_object_condition = false;
};

struct PreparedSimplePlan {
  SimplePlan source;
  std::vector<int> projected_indices;
  std::vector<std::string> projected_header;
  CompiledSimplePlan compiled;
};

struct FusedSimpleRuntime {
  PreparedSimplePlan prepared;
  std::vector<std::string> projected_row_storage;
  std::vector<std::string_view> projected_row;
  std::unordered_map<std::string, std::string> row;
  int subject_cache_id = -1;
  int predicate_cache_id = -1;
  int object_cache_id = -1;
  int graph_cache_id = -1;
  std::string subject;
  std::string predicate;
  std::string object;
  std::string graph;
  std::string subject_scratch;
  std::string predicate_scratch;
  std::string object_scratch;
  std::string graph_scratch;
};

struct FusedSimpleGroup {
  std::vector<std::size_t> runtime_indices;
  bool reuse_subject_predicate_object = false;
};

struct FusedSimpleProgram {
  std::vector<FusedSimpleRuntime> runtimes;
  std::vector<FusedSimpleGroup> groups;
  std::vector<TermCacheEntry> term_cache_entries;
};

std::vector<int> get_attribute_index(std::istream& file,
                                     const std::vector<std::string>& header,
                                     const std::vector<std::string>& projected_attributes);

CompiledSimplePlan compile_simple_plan(const std::vector<std::string>& projected_header,
                                       const std::string& base_uri,
                                       const std::vector<std::string>& s_content,
                                       const std::vector<std::string>& p_content,
                                       const std::vector<std::string>& o_content,
                                       const std::vector<std::string>* g_content = nullptr);

bool prepare_simple_plan(const SimplePlan& source,
                         std::istream& file,
                         std::string& header_line,
                         PreparedSimplePlan& prepared);

void prepare_simple_plan_from_header(const SimplePlan& source,
                                     const std::vector<std::string>& header,
                                     PreparedSimplePlan& prepared);

FusedSimpleProgram compile_fused_simple_program(const std::vector<SimplePlan>& plans,
                                                const std::vector<std::string>& header);

void reset_fused_term_cache(FusedSimpleProgram& program);

bool has_cached_fused_term(const FusedSimpleRuntime& runtime);
