#include "join_program.h"

#include <unordered_map>

static bool can_cache_complex_term(const CompiledRuntimeTerm& term) {
  if (term.render_op == CompiledRuntimeTerm::RenderOp::Preformatted) {
    return true;
  }
  return term.render_op == CompiledRuntimeTerm::RenderOp::Compiled &&
         term.compiled.usable &&
         term.compiled.term_type != "blanknode";
}

static void count_cacheable_complex_term(const std::string& base_uri,
                                         const CompiledRuntimeTerm& term,
                                         std::unordered_map<std::string, std::size_t>& counts) {
  if (can_cache_complex_term(term)) {
    counts[term_cache_key(base_uri, term)]++;
  }
}

static int assign_cacheable_complex_term(const std::string& base_uri,
                                         const CompiledRuntimeTerm& term,
                                         const std::unordered_map<std::string, std::size_t>& counts,
                                         std::unordered_map<std::string, int>& ids,
                                         std::vector<TermCacheEntry>& entries) {
  if (!can_cache_complex_term(term)) {
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

static void compile_complex_term_cache(CompiledComplexRenderPlan& plan,
                                       const std::string& base_uri) {
  std::unordered_map<std::string, std::size_t> counts;
  counts.reserve(plan.has_graph ? 4 : 3);
  count_cacheable_complex_term(base_uri, plan.subject, counts);
  count_cacheable_complex_term(base_uri, plan.predicate, counts);
  count_cacheable_complex_term(base_uri, plan.object, counts);
  if (plan.has_graph) {
    count_cacheable_complex_term(base_uri, plan.graph, counts);
  }

  std::unordered_map<std::string, int> ids;
  ids.reserve(counts.size());
  plan.term_cache_entries.reserve(counts.size());
  plan.subject_cache_id = assign_cacheable_complex_term(base_uri, plan.subject, counts, ids, plan.term_cache_entries);
  plan.predicate_cache_id = assign_cacheable_complex_term(base_uri, plan.predicate, counts, ids, plan.term_cache_entries);
  plan.object_cache_id = assign_cacheable_complex_term(base_uri, plan.object, counts, ids, plan.term_cache_entries);
  if (plan.has_graph) {
    plan.graph_cache_id = assign_cacheable_complex_term(base_uri, plan.graph, counts, ids, plan.term_cache_entries);
  }
}

CompiledComplexRenderPlan compile_complex_render_plan(const std::vector<std::string>& joined_headers,
                                                      const std::string& base_uri,
                                                      const std::vector<std::string>& s_content,
                                                      const std::vector<std::string>& p_content,
                                                      const std::vector<std::string>& o_content,
                                                      const std::vector<std::string>* g_content) {
  CompiledComplexRenderPlan plan;
  plan.subject = compile_runtime_term(s_content, joined_headers, base_uri);
  plan.predicate = compile_runtime_term(p_content, joined_headers, base_uri);
  plan.object = compile_runtime_term(o_content, joined_headers, base_uri);
  if (g_content != nullptr) {
    plan.graph = compile_runtime_term(*g_content, joined_headers, base_uri);
    plan.has_graph = true;
  }

  plan.has_object_condition = plan.object.content.size() > 5 && plan.object.content[5] != "None";
  plan.needs_row_map = term_needs_row_map(plan.subject.content, plan.subject.compiled) ||
                       term_needs_row_map(plan.predicate.content, plan.predicate.compiled) ||
                       term_needs_row_map(plan.object.content, plan.object.compiled) ||
                       plan.has_object_condition;
  if (plan.has_graph) {
    plan.needs_row_map = plan.needs_row_map || term_needs_row_map(plan.graph.content, plan.graph.compiled);
  }
  compile_complex_term_cache(plan, base_uri);
  return plan;
}
