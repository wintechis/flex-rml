#include "simple_program.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "csv_row.h"

static void initialize_row_map(std::unordered_map<std::string, std::string>& row,
                               const std::vector<std::string>& projected_header) {
  row.clear();
  row.reserve(projected_header.size());
  for (const auto& name : projected_header) {
    row.try_emplace(name);
  }
}

CompiledSimplePlan compile_simple_plan(const std::vector<std::string>& projected_header,
                                       const std::string& base_uri,
                                       const std::vector<std::string>& s_content,
                                       const std::vector<std::string>& p_content,
                                       const std::vector<std::string>& o_content,
                                       const std::vector<std::string>* g_content) {
  CompiledSimplePlan plan;
  plan.subject = compile_runtime_term(s_content, projected_header, base_uri);
  plan.predicate = compile_runtime_term(p_content, projected_header, base_uri);
  plan.object = compile_runtime_term(o_content, projected_header, base_uri);
  if (g_content != nullptr) {
    plan.graph = compile_runtime_term(*g_content, projected_header, base_uri);
    plan.has_graph = true;
  }

  plan.has_object_condition = plan.object.content.size() > 5 && plan.object.content[5] != "None";
  plan.needs_row_map = term_needs_row_map(plan.subject.content, plan.subject.compiled) ||
                       term_needs_row_map(plan.predicate.content, plan.predicate.compiled) ||
                       term_needs_row_map(plan.object.content, plan.object.compiled) ||
                       plan.has_object_condition;
  plan.function_called = plan.subject.render_op == CompiledRuntimeTerm::RenderOp::Function ||
                         plan.predicate.render_op == CompiledRuntimeTerm::RenderOp::Function ||
                         plan.object.render_op == CompiledRuntimeTerm::RenderOp::Function;
  if (plan.has_graph) {
    plan.needs_row_map = plan.needs_row_map || term_needs_row_map(plan.graph.content, plan.graph.compiled);
    plan.function_called = plan.function_called || plan.graph.render_op == CompiledRuntimeTerm::RenderOp::Function;
  }
  return plan;
}

std::vector<int> get_attribute_index(std::istream&,
                                     const std::vector<std::string>& header,
                                     const std::vector<std::string>& projected_attributes) {
  std::vector<int> projected_indices;

  for (const auto& attr : projected_attributes) {
    if (attr == "") {
      continue;
    }

    auto it = std::find_if(header.begin(), header.end(), [&](std::string_view field) {
      return field == attr;
    });
    if (it != header.end()) {
      projected_indices.push_back(std::distance(header.begin(), it));
    } else {
      throw std::runtime_error("Attribute not found: '" + attr + "'");
    }
  }

  return projected_indices;
}

bool prepare_simple_plan(const SimplePlan& source,
                         std::istream& file,
                         std::string& header_line,
                         PreparedSimplePlan& prepared) {
  if (!std::getline(file, header_line)) {
    return false;
  }

  std::vector<std::string> header = split_csv_line(header_line, ',');
  prepared.source = source;
  if (!(source.projected_attributes.size() == 1 && source.projected_attributes[0] == "")) {
    prepared.projected_indices = get_attribute_index(file, header, source.projected_attributes);
  }

  prepared.projected_header.clear();
  prepared.projected_header.reserve(prepared.projected_indices.size());
  for (int index : prepared.projected_indices) {
    prepared.projected_header.push_back(header[index]);
  }

  prepared.compiled = compile_simple_plan(
      prepared.projected_header,
      source.base_uri,
      source.s_content,
      source.p_content,
      source.o_content,
      source.generate_graph ? &source.g_content : nullptr);
  return true;
}

void prepare_simple_plan_from_header(const SimplePlan& source,
                                     const std::vector<std::string>& header,
                                     PreparedSimplePlan& prepared) {
  prepared.source = source;
  if (!(source.projected_attributes.size() == 1 && source.projected_attributes[0] == "")) {
    std::istringstream unused;
    prepared.projected_indices = get_attribute_index(unused, header, source.projected_attributes);
  }

  prepared.projected_header.clear();
  prepared.projected_header.reserve(prepared.projected_indices.size());
  for (int index : prepared.projected_indices) {
    prepared.projected_header.push_back(header[index]);
  }

  prepared.compiled = compile_simple_plan(
      prepared.projected_header,
      source.base_uri,
      source.s_content,
      source.p_content,
      source.o_content,
      source.generate_graph ? &source.g_content : nullptr);
}

static bool can_cache_fused_term(const CompiledRuntimeTerm& term) {
  if (term.render_op == CompiledRuntimeTerm::RenderOp::Preformatted) {
    return true;
  }
  return term.render_op == CompiledRuntimeTerm::RenderOp::Compiled &&
         term.compiled.usable &&
         term.compiled.term_type != "blanknode";
}

static void count_cacheable_fused_term(const std::string& base_uri,
                                       const CompiledRuntimeTerm& term,
                                       std::map<std::string, std::size_t>& counts) {
  if (can_cache_fused_term(term)) {
    counts[term_cache_key(base_uri, term)]++;
  }
}

static int assign_cacheable_fused_term(const std::string& base_uri,
                                       const CompiledRuntimeTerm& term,
                                       const std::map<std::string, std::size_t>& counts,
                                       std::unordered_map<std::string, int>& ids,
                                       std::vector<TermCacheEntry>& entries) {
  if (!can_cache_fused_term(term)) {
    return -1;
  }

  std::string key = term_cache_key(base_uri, term);
  auto count_it = counts.find(key);
  if (count_it == counts.end() || count_it->second < 2) {
    return -1;
  }

  auto id_it = ids.find(key);
  if (id_it != ids.end()) {
    return id_it->second;
  }

  const int id = static_cast<int>(entries.size());
  TermCacheEntry entry;
  entry.key = key;
  entry.value.reserve(512);
  entry.scratch.reserve(512);
  entries.push_back(std::move(entry));
  ids.emplace(std::move(key), id);
  return id;
}

static std::vector<TermCacheEntry> compile_fused_term_cache(std::vector<FusedSimpleRuntime>& runtimes,
                                                            const std::vector<FusedSimpleGroup>& groups) {
  std::vector<bool> skip_runtime(runtimes.size(), false);
  for (std::size_t runtime_index = 0; runtime_index < runtimes.size(); ++runtime_index) {
    if (runtimes[runtime_index].prepared.compiled.has_graph) {
      skip_runtime[runtime_index] = true;
    }
  }
  for (const FusedSimpleGroup& group : groups) {
    if (!group.reuse_subject_predicate_object || group.runtime_indices.size() < 2) {
      continue;
    }
    for (std::size_t runtime_index : group.runtime_indices) {
      skip_runtime[runtime_index] = true;
    }
  }

  std::map<std::string, std::size_t> counts;
  for (std::size_t runtime_index = 0; runtime_index < runtimes.size(); ++runtime_index) {
    if (skip_runtime[runtime_index]) {
      continue;
    }
    const FusedSimpleRuntime& runtime = runtimes[runtime_index];
    const CompiledSimplePlan& plan = runtime.prepared.compiled;
    const std::string& base_uri = runtime.prepared.source.base_uri;
    count_cacheable_fused_term(base_uri, plan.subject, counts);
    count_cacheable_fused_term(base_uri, plan.predicate, counts);
    count_cacheable_fused_term(base_uri, plan.object, counts);
    if (plan.has_graph) {
      count_cacheable_fused_term(base_uri, plan.graph, counts);
    }
  }

  std::unordered_map<std::string, int> ids;
  std::vector<TermCacheEntry> entries;
  ids.reserve(counts.size());
  entries.reserve(counts.size());
  for (std::size_t runtime_index = 0; runtime_index < runtimes.size(); ++runtime_index) {
    if (skip_runtime[runtime_index]) {
      continue;
    }
    FusedSimpleRuntime& runtime = runtimes[runtime_index];
    const CompiledSimplePlan& plan = runtime.prepared.compiled;
    const std::string& base_uri = runtime.prepared.source.base_uri;
    runtime.subject_cache_id = assign_cacheable_fused_term(base_uri, plan.subject, counts, ids, entries);
    runtime.predicate_cache_id = assign_cacheable_fused_term(base_uri, plan.predicate, counts, ids, entries);
    runtime.object_cache_id = assign_cacheable_fused_term(base_uri, plan.object, counts, ids, entries);
    if (plan.has_graph) {
      runtime.graph_cache_id = assign_cacheable_fused_term(base_uri, plan.graph, counts, ids, entries);
    }
  }
  return entries;
}

void reset_fused_term_cache(FusedSimpleProgram& program) {
  reset_term_cache(program.term_cache_entries);
}

bool has_cached_fused_term(const FusedSimpleRuntime& runtime) {
  return runtime.subject_cache_id >= 0 ||
         runtime.predicate_cache_id >= 0 ||
         runtime.object_cache_id >= 0 ||
         runtime.graph_cache_id >= 0;
}

static void initialize_fused_runtime(FusedSimpleRuntime& runtime) {
  runtime.projected_row_storage.reserve(runtime.prepared.projected_indices.size());
  runtime.projected_row.reserve(runtime.prepared.projected_indices.size());
  runtime.subject.reserve(512);
  runtime.predicate.reserve(512);
  runtime.object.reserve(512);
  runtime.graph.reserve(512);
  runtime.subject_scratch.reserve(512);
  runtime.predicate_scratch.reserve(512);
  runtime.object_scratch.reserve(512);
  runtime.graph_scratch.reserve(512);
  if (runtime.prepared.compiled.needs_row_map) {
    initialize_row_map(runtime.row, runtime.prepared.projected_header);
  }
}

static bool can_reuse_subject_predicate_object(const FusedSimpleRuntime& runtime) {
  const CompiledSimplePlan& plan = runtime.prepared.compiled;
  return plan.has_graph &&
         !plan.needs_row_map &&
         !plan.function_called &&
         !plan.has_object_condition;
}

static bool has_same_subject_predicate_object_group(const FusedSimpleRuntime& left,
                                                    const FusedSimpleRuntime& right) {
  return left.prepared.source.base_uri == right.prepared.source.base_uri &&
         left.prepared.source.projected_attributes == right.prepared.source.projected_attributes &&
         left.prepared.source.s_content == right.prepared.source.s_content &&
         left.prepared.source.p_content == right.prepared.source.p_content &&
         left.prepared.source.o_content == right.prepared.source.o_content;
}

static std::vector<FusedSimpleGroup> build_fused_simple_groups(const std::vector<FusedSimpleRuntime>& runtimes) {
  std::vector<FusedSimpleGroup> groups;
  for (std::size_t i = 0; i < runtimes.size(); ++i) {
    if (!can_reuse_subject_predicate_object(runtimes[i])) {
      groups.push_back(FusedSimpleGroup{{i}, false});
      continue;
    }

    bool added = false;
    for (auto& group : groups) {
      if (!group.reuse_subject_predicate_object) {
        continue;
      }
      const FusedSimpleRuntime& representative = runtimes[group.runtime_indices.front()];
      if (has_same_subject_predicate_object_group(representative, runtimes[i])) {
        group.runtime_indices.push_back(i);
        added = true;
        break;
      }
    }

    if (!added) {
      groups.push_back(FusedSimpleGroup{{i}, true});
    }
  }
  return groups;
}

FusedSimpleProgram compile_fused_simple_program(const std::vector<SimplePlan>& plans,
                                                const std::vector<std::string>& header) {
  FusedSimpleProgram program;
  program.runtimes.reserve(plans.size());
  for (const auto& plan : plans) {
    FusedSimpleRuntime runtime;
    prepare_simple_plan_from_header(plan, header, runtime.prepared);
    initialize_fused_runtime(runtime);
    program.runtimes.push_back(std::move(runtime));
  }

  program.groups = build_fused_simple_groups(program.runtimes);
  program.term_cache_entries = compile_fused_term_cache(program.runtimes, program.groups);
  return program;
}
