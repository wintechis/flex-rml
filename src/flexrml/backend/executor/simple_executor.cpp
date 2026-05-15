#include "simple_executor.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "csv_row.h"
#include "definitions.h"
#include "source_reader_factory.h"
#include "simple_program.h"
#include "term_cache.h"
#include "term_program.h"
#include "utils.h"
#include "xxhash.h"

namespace fs = std::filesystem;

constexpr std::size_t kOutputBufferReserve = 64 * 1024;
constexpr std::size_t kSharedDedupBatchSize = 16 * 1024;

struct TripleHash128 {
  XXH64_hash_t low = 0;
  XXH64_hash_t high = 0;

  bool operator==(const TripleHash128& other) const {
    return low == other.low && high == other.high;
  }
};

struct TripleHash128Hasher {
  using is_avalanching = void;

  auto operator()(const TripleHash128& key) const noexcept -> std::uint64_t {
    return key.low ^ (key.high + 0x9e3779b97f4a7c15ULL + (key.low << 6) + (key.low >> 2));
  }
};

using TripleHashSet = ankerl::unordered_dense::set<TripleHash128, TripleHash128Hasher>;

static TripleHash128 hash_triple(std::string_view triple) {
  const XXH128_hash_t hash = XXH3_128bits(triple.data(), triple.size());
  return TripleHash128{hash.low64, hash.high64};
}

class ConcurrentTripleDeduper final : public TripleDeduper {
 public:
  bool insert(std::string_view triple) override {
    const TripleHash128 hash = hash_triple(triple);
    Shard& shard = shards_[hash.low % shards_.size()];
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.hashes.insert(hash).second;
  }

  std::size_t append_unique(std::vector<std::string>& triples, std::string& output) override {
    std::array<std::vector<std::pair<TripleHash128, std::size_t>>, 64> batches;
    for (std::size_t index = 0; index < triples.size(); ++index) {
      const TripleHash128 hash = hash_triple(triples[index]);
      batches[hash.low % batches.size()].push_back({hash, index});
    }

    std::vector<std::size_t> unique_indices;
    unique_indices.reserve(triples.size());
    for (std::size_t shard_index = 0; shard_index < batches.size(); ++shard_index) {
      auto& batch = batches[shard_index];
      if (batch.empty()) {
        continue;
      }

      Shard& shard = shards_[shard_index];
      std::lock_guard<std::mutex> lock(shard.mutex);
      for (const auto& [hash, triple_index] : batch) {
        if (shard.hashes.insert(hash).second) {
          unique_indices.push_back(triple_index);
        }
      }
    }

    for (const std::size_t triple_index : unique_indices) {
      output += triples[triple_index];
    }

    triples.clear();
    return unique_indices.size();
  }

 private:
  struct Shard {
    std::mutex mutex;
    TripleHashSet hashes;
  };

  std::array<Shard, 64> shards_;
};

std::shared_ptr<TripleDeduper> create_concurrent_triple_deduper() {
  return std::make_shared<ConcurrentTripleDeduper>();
}

static void create_parent_directories_if_needed(const fs::path& path) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

static const std::string kConstantTermMapType = "constant";

static void initialize_row_map(std::unordered_map<std::string, std::string>& row,
                               const std::vector<std::string>& projected_header) {
  row.clear();
  row.reserve(projected_header.size());
  for (const auto& name : projected_header) {
    row.try_emplace(name);
  }
}

static void update_row_map(std::unordered_map<std::string, std::string>& row,
                           const std::vector<std::string>& projected_header,
                           const std::vector<std::string_view>& projected_row) {
  for (std::size_t i = 0; i < projected_row.size(); i++) {
    auto it = row.find(projected_header[i]);
    if (it != row.end()) {
      it->second = projected_row[i];
    }
  }
}

static bool render_runtime_term(const CompiledRuntimeTerm& term,
                                int line_count,
                                const std::string& input_file_name,
                                const std::vector<std::string_view>& projected_row,
                                const std::string& base_uri,
                                std::unordered_map<std::string, std::string>& row,
                                std::string& out,
                                std::string& scratch,
                                std::string& function_value) {
  const std::vector<std::string>& content = term.content;
  switch (term.render_op) {
    case CompiledRuntimeTerm::RenderOp::Null:
      return false;
    case CompiledRuntimeTerm::RenderOp::Function:
      function_value = handle_function_call(content[0], line_count, input_file_name, row);
      if (function_value == "NULL") {
        return false;
      }
      create_operator_into(function_value, kConstantTermMapType, content[2],
                           content.size() > 3 ? content[3] : "",
                           content.size() > 4 ? content[4] : "",
                           base_uri, row, out, scratch);
      return true;
    case CompiledRuntimeTerm::RenderOp::Preformatted:
      out = content[0];
      return true;
    case CompiledRuntimeTerm::RenderOp::Compiled:
      render_compiled_term(term.compiled, projected_row, base_uri, out, scratch);
      return true;
    case CompiledRuntimeTerm::RenderOp::Fallback:
      create_operator_into(content[0], content[1], content[2],
                           content.size() > 3 ? content[3] : "",
                           content.size() > 4 ? content[4] : "",
                           base_uri, row, out, scratch);
      return true;
  }
  return false;
}

static void append_literal_term(std::string_view rdf_term,
                                const std::string& lang_tag,
                                const std::string& data_type,
                                bool infer_datatype,
                                std::string& out) {
  const std::string_view effective_data_type =
      infer_datatype ? infer_literal_datatype(rdf_term, lang_tag, data_type) : data_type;
  const std::size_t required_size = out.size() + rdf_term.size() + effective_data_type.size() + lang_tag.size() + 6;
  if (out.capacity() < required_size) {
    out.reserve(required_size);
  }
  out.push_back('"');
  out.append(rdf_term);
  out.push_back('"');
  if (effective_data_type != "None") {
    out.append("^^<");
    out.append(effective_data_type);
    out.push_back('>');
  } else if (lang_tag != "None") {
    out.push_back('@');
    out.append(lang_tag);
  }
}

static bool append_fast_compiled_term(const CompiledRuntimeTerm& runtime_term,
                                      const std::vector<std::string_view>& projected_row,
                                      const std::string& base_uri,
                                      std::string& out,
                                      std::string& scratch) {
  if (runtime_term.render_op == CompiledRuntimeTerm::RenderOp::Preformatted) {
    out.append(runtime_term.content[0]);
    return true;
  }
  if (runtime_term.render_op != CompiledRuntimeTerm::RenderOp::Compiled) {
    return false;
  }

  const CompiledTerm& term = runtime_term.compiled;
  if (!term.usable || term.term_kind == CompiledTermType::BlankNode) {
    return false;
  }

  scratch.clear();
  std::string_view rdf_term = term.term_map;
  if (term.map_type == CompiledTermMapType::Reference) {
    rdf_term = projected_row[term.reference_index];
  } else if (term.map_type == CompiledTermMapType::Template) {
    if (scratch.capacity() < term.term_map.size()) {
      scratch.reserve(term.term_map.size());
    }
    for (const CompiledTemplatePart& part : term.parts) {
      if (part.reference_index >= 0) {
        const std::string_view value = projected_row[part.reference_index];
        switch (term.term_kind) {
          case CompiledTermType::Uri:
            append_safe_iri(value, true, scratch);
            break;
          case CompiledTermType::Iri:
            append_safe_iri(value, false, scratch);
            break;
          default:
            scratch += value;
            break;
        }
      } else {
        scratch += part.literal;
      }
    }
    rdf_term = scratch;
  }

  if ((term.map_type == CompiledTermMapType::Reference || term.map_type == CompiledTermMapType::Template) &&
      term.add_base_iri &&
      !(rdf_term.starts_with("http://") || rdf_term.starts_with("https://"))) {
    if (rdf_term.data() != scratch.data()) {
      scratch.assign(rdf_term);
    }
    scratch.insert(0, base_uri);
    rdf_term = scratch;
  }

  switch (term.term_kind) {
    case CompiledTermType::Literal:
      append_literal_term(rdf_term, term.lang_tag, term.data_type, term.infer_datatype, out);
      return true;
    case CompiledTermType::Uri:
    case CompiledTermType::Iri:
    case CompiledTermType::UnsafeIri:
      if (validates_iri_chars(term.term_kind) && has_invalid_iri_char(rdf_term)) {
        std::string error_msg;
        error_msg.reserve(rdf_term.size() + 58);
        error_msg.append("Error: invalid IRI detected for node: '");
        error_msg.append(rdf_term);
        error_msg.append("'. ");
        if (continue_on_error == true) {
          std::cout << error_msg << "Skipping!\n";
        }
        error_msg += "Stop!";
        throw std::runtime_error(error_msg);
      }
      out.push_back('<');
      out.append(rdf_term);
      out.push_back('>');
      return true;
    case CompiledTermType::BlankNode:
    case CompiledTermType::Unsupported:
      return false;
  }

  return false;
}

static bool can_fast_emit_simple_plan(const CompiledSimplePlan& plan) {
  const auto can_fast_term = [](const CompiledRuntimeTerm& term) {
    if (term.render_op == CompiledRuntimeTerm::RenderOp::Preformatted) {
      return true;
    }
    return term.render_op == CompiledRuntimeTerm::RenderOp::Compiled &&
           term.compiled.usable &&
           term.compiled.term_kind != CompiledTermType::BlankNode;
  };
  return !plan.needs_row_map &&
         !plan.function_called &&
         !plan.has_object_condition &&
         can_fast_term(plan.subject) &&
         can_fast_term(plan.predicate) &&
         can_fast_term(plan.object) &&
         (!plan.has_graph || can_fast_term(plan.graph));
}

static bool emit_fast_statement(const CompiledSimplePlan& plan,
                                const std::vector<std::string_view>& projected_row,
                                const std::string& base_uri,
                                std::string& out,
                                std::string& subject_scratch,
                                std::string& predicate_scratch,
                                std::string& object_scratch,
                                std::string& graph,
                                std::string& graph_scratch) {
  out.clear();
  if (!append_fast_compiled_term(plan.subject, projected_row, base_uri, out, subject_scratch)) {
    return false;
  }
  out.push_back(' ');
  if (!append_fast_compiled_term(plan.predicate, projected_row, base_uri, out, predicate_scratch)) {
    return false;
  }
  out.push_back(' ');
  if (!append_fast_compiled_term(plan.object, projected_row, base_uri, out, object_scratch)) {
    return false;
  }
  if (plan.has_graph) {
    graph.clear();
    if (!append_fast_compiled_term(plan.graph, projected_row, base_uri, graph, graph_scratch)) {
      return false;
    }
    if (!graph.empty() && !is_default_graph_marker(graph)) {
      out.push_back(' ');
      out.append(graph);
    }
  }
  out.append(" .\n");
  return true;
}

///////////////////////////////////////////////////////////////
/// Data setup
///////////////////////////////////////////////////////////////
struct SetupData {
  TripleHashSet unique_triple_hashes;

  std::vector<std::string_view> projected_row_views;
  std::unordered_map<std::string, std::string> row;

  size_t triple_counter = 0;
  size_t write_cnt = 0;
  bool skip = false;
  size_t buffer_limit = 20000;

  std::string subject;
  std::string predicate;
  std::string object;
  std::string graph;
  std::string res;
  std::string buffered_res;
  std::string subject_scratch;
  std::string predicate_scratch;
  std::string object_scratch;
  std::string graph_scratch;

  std::ofstream outputFile;
  OutputChunkWriter* writer = nullptr;
};

SetupData initialize_setup(const fs::path& output_file_name, OutputChunkWriter* writer = nullptr) {
  SetupData data;
  data.writer = writer;

  // Reserve memory for strings and vectors
  data.projected_row_views.reserve(32);
  data.row.reserve(32);

  data.subject.reserve(512);
  data.predicate.reserve(512);
  data.object.reserve(512);
  data.graph.reserve(512);
  data.res.reserve(2048);
  data.buffered_res.reserve(kOutputBufferReserve);
  data.subject_scratch.reserve(512);
  data.predicate_scratch.reserve(512);
  data.object_scratch.reserve(512);
  data.graph_scratch.reserve(512);

  if (data.writer == nullptr) {
    // Open output file
    create_parent_directories_if_needed(output_file_name);
    data.outputFile.open(output_file_name, std::ios::app);

    if (!data.outputFile) {
      std::cerr << "Error: Unable to open file for writing." << std::endl;
      std::exit(1);
    }
  }

  return data;
}
///////////////

static void flush_setup_buffer(SetupData& setup_data) {
  if (setup_data.buffered_res.empty()) {
    return;
  }

  if (setup_data.writer != nullptr) {
    setup_data.writer->write(std::move(setup_data.buffered_res));
    setup_data.buffered_res.clear();
    setup_data.buffered_res.reserve(kOutputBufferReserve);
  } else {
    setup_data.outputFile << setup_data.buffered_res;
    setup_data.buffered_res.clear();
  }
}

static bool insert_unique_triple(SetupData& setup_data, TripleDeduper* shared_deduper) {
  if (shared_deduper != nullptr) {
    return shared_deduper->insert(setup_data.res);
  }
  return setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second;
}

static void flush_shared_dedup_batch(SetupData& setup_data,
                                     TripleDeduper* shared_deduper,
                                     std::vector<std::string>& pending_triples) {
  if (shared_deduper == nullptr || pending_triples.empty()) {
    return;
  }

  setup_data.triple_counter += shared_deduper->append_unique(pending_triples, setup_data.buffered_res);
  if (setup_data.buffered_res.size() >= kOutputBufferReserve) {
    flush_setup_buffer(setup_data);
  }
}

static void emit_unique_result(SetupData& setup_data,
                               TripleDeduper* shared_deduper,
                               std::vector<std::string>& pending_shared_triples) {
  if (shared_deduper != nullptr) {
    pending_shared_triples.push_back(setup_data.res);
    if (pending_shared_triples.size() >= kSharedDedupBatchSize) {
      flush_shared_dedup_batch(setup_data, shared_deduper, pending_shared_triples);
    }
    return;
  }

  if (!insert_unique_triple(setup_data, nullptr)) {
    return;
  }

  setup_data.triple_counter++;
  setup_data.buffered_res += setup_data.res;
  setup_data.write_cnt++;
  if (setup_data.write_cnt == setup_data.buffer_limit) {
    setup_data.write_cnt = 0;
    flush_setup_buffer(setup_data);
  }
}

SetupData initialize_setup_dependent(const fs::path& output_file_name) {
  SetupData data;

  // Reserve memory for strings and vectors
  data.projected_row_views.reserve(32);
  data.row.reserve(32);

  data.subject.reserve(512);
  data.predicate.reserve(512);
  data.object.reserve(512);
  data.graph.reserve(512);
  data.res.reserve(2048);
  data.buffered_res.reserve(kOutputBufferReserve);
  data.subject_scratch.reserve(512);
  data.predicate_scratch.reserve(512);
  data.object_scratch.reserve(512);
  data.graph_scratch.reserve(512);

  return data;
}

static bool append_cached_fast_term(const CompiledRuntimeTerm& term,
                                    const std::vector<std::string_view>& projected_row,
                                    const std::string& base_uri,
                                    int cache_id,
                                    std::vector<TermCacheEntry>& cache_entries,
                                    std::string& out,
                                    std::string& scratch) {
  if (cache_id < 0) {
    return append_fast_compiled_term(term, projected_row, base_uri, out, scratch);
  }

  TermCacheEntry& entry = cache_entries[static_cast<std::size_t>(cache_id)];
  if (!entry.computed) {
    entry.value.clear();
    if (!append_fast_compiled_term(term, projected_row, base_uri, entry.value, entry.scratch)) {
      return false;
    }
    entry.computed = true;
  }
  out.append(entry.value);
  return true;
}

static bool emit_cached_fast_statement(const CompiledSimplePlan& plan,
                                       const std::vector<std::string_view>& projected_row,
                                       const std::string& base_uri,
                                       int subject_cache_id,
                                       int predicate_cache_id,
                                       int object_cache_id,
                                       int graph_cache_id,
                                       std::vector<TermCacheEntry>& cache_entries,
                                       std::string& out,
                                       std::string& subject_scratch,
                                       std::string& predicate_scratch,
                                       std::string& object_scratch,
                                       std::string& graph,
                                       std::string& graph_scratch) {
  out.clear();
  if (!append_cached_fast_term(plan.subject, projected_row, base_uri, subject_cache_id, cache_entries, out, subject_scratch)) {
    return false;
  }
  out.push_back(' ');
  if (!append_cached_fast_term(plan.predicate, projected_row, base_uri, predicate_cache_id, cache_entries, out, predicate_scratch)) {
    return false;
  }
  out.push_back(' ');
  if (!append_cached_fast_term(plan.object, projected_row, base_uri, object_cache_id, cache_entries, out, object_scratch)) {
    return false;
  }
  if (plan.has_graph) {
    graph.clear();
    if (!append_cached_fast_term(plan.graph, projected_row, base_uri, graph_cache_id, cache_entries, graph, graph_scratch)) {
      return false;
    }
    if (!graph.empty() && !is_default_graph_marker(graph)) {
      out.push_back(' ');
      out.append(graph);
    }
  }
  out.append(" .\n");
  return true;
}

static bool is_single_constant_statement(const SimplePlan& plan) {
  if (!plan.generate_graph) {
    return (plan.s_content[1] == "constant" && plan.p_content[1] == "constant" && plan.o_content[1] == "constant") ||
           (plan.s_content[1] == "preformatted" && plan.p_content[1] == "preformatted" && plan.o_content[1] == "preformatted");
  }
  return (plan.s_content[1] == "constant" && plan.p_content[1] == "constant" && plan.o_content[1] == "constant" && plan.g_content[1] == "constant") ||
         (plan.s_content[1] == "preformatted" && plan.p_content[1] == "preformatted" && plan.o_content[1] == "preformatted" && plan.g_content[1] == "preformatted");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
int execute_simple_with_graph(const std::string& input_file_name,
                              const fs::path& output_file_name,
                              const std::string& base_uri,
                              const std::vector<std::string>& projected_attributes,
                              const std::vector<std::string>& s_content,
                              const std::vector<std::string>& p_content,
                              const std::vector<std::string>& o_content,
                              const std::vector<std::string>& g_content,
                              const std::unordered_map<std::string, std::string>& data_map,
                              OutputChunkWriter* writer = nullptr) {
  // Setup
  SetupData setup_data = initialize_setup(output_file_name, writer);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  auto reader = create_source_reader(data_map, input_file_name);

  SimplePlan source;
  source.output_file_name = output_file_name;
  source.base_uri = base_uri;
  source.input_file_name = input_file_name;
  source.projected_attributes = projected_attributes;
  source.s_content = s_content;
  source.p_content = p_content;
  source.o_content = o_content;
  source.g_content = g_content;
  source.generate_graph = true;

  PreparedSimplePlan prepared;
  if (reader->header().empty()) {
    return 0;
  }
  prepare_simple_plan_from_header(source, reader->header(), prepared);
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  int line_count = 0;
  // Iterate over file line by line
  RowView source_row;
  while (reader->next(source_row)) {
    line_count++;

    project_row_into(source_row.fields, prepared.projected_indices, setup_data.projected_row_views);

    // Check for NULL values
    setup_data.skip = row_has_skip_value(setup_data.projected_row_views);
    if (setup_data.skip) {
      continue;
    }

    auto& row = setup_data.row;
    if (plan.needs_row_map) {
      update_row_map(row, prepared.projected_header, setup_data.projected_row_views);
    }

    if (can_fast_emit_simple_plan(plan)) {
      try {
        if (!emit_fast_statement(plan, setup_data.projected_row_views, base_uri, setup_data.res,
                                 setup_data.subject_scratch, setup_data.predicate_scratch,
                                 setup_data.object_scratch, setup_data.graph, setup_data.graph_scratch)) {
          continue;
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        }
        continue;
      }
      if (!setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second) {
        continue;
      }
      setup_data.triple_counter++;
      setup_data.buffered_res += setup_data.res;
      setup_data.write_cnt++;
      if (setup_data.write_cnt == setup_data.buffer_limit) {
        setup_data.write_cnt = 0;
        flush_setup_buffer(setup_data);
      }
      continue;
    }

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    std::string g_function_value;
    if (plan.has_object_condition &&
        handle_function_call(plan.object.content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      if (!render_runtime_term(plan.subject, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.subject, setup_data.subject_scratch, s_function_value) ||
          !render_runtime_term(plan.predicate, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.predicate, setup_data.predicate_scratch, p_function_value) ||
          !render_runtime_term(plan.object, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.object, setup_data.object_scratch, o_function_value) ||
          !render_runtime_term(plan.graph, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.graph, setup_data.graph_scratch, g_function_value)) {
        continue;
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    format_statement_into(setup_data.subject, setup_data.predicate, setup_data.object, setup_data.graph, setup_data.res);
    if (!setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second) {
      continue;
    }
    setup_data.triple_counter++;

    setup_data.buffered_res += setup_data.res;
    setup_data.write_cnt++;

    if (setup_data.write_cnt == setup_data.buffer_limit) {
      setup_data.write_cnt = 0;
      ////// SERIALIZE //////
      flush_setup_buffer(setup_data);
    }
  }
  ////// SERIALIZE //////
  flush_setup_buffer(setup_data);

  return setup_data.triple_counter;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::unordered_set<std::string> execute_simple_with_graph_dependent(const std::string& input_file_name,
                                                                    const fs::path& output_file_name,
                                                                    const std::string& base_uri,
                                                                    const std::vector<std::string>& projected_attributes,
                                                                    const std::vector<std::string>& s_content,
                                                                    const std::vector<std::string>& p_content,
                                                                    const std::vector<std::string>& o_content,
                                                                    const std::vector<std::string>& g_content,
                                                                    std::unordered_set<std::string>& unique_triple,
                                                                    const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  SetupData setup_data = initialize_setup_dependent(output_file_name);

  //////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////
  // Open input
  auto reader = create_source_reader(data_map, input_file_name);

  //////////////////////////////////////////////////////////////////////

  SimplePlan source;
  source.output_file_name = output_file_name;
  source.base_uri = base_uri;
  source.input_file_name = input_file_name;
  source.projected_attributes = projected_attributes;
  source.s_content = s_content;
  source.p_content = p_content;
  source.o_content = o_content;
  source.g_content = g_content;
  source.generate_graph = true;

  PreparedSimplePlan prepared;
  if (reader->header().empty()) {
    return unique_triple;
  }
  prepare_simple_plan_from_header(source, reader->header(), prepared);
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  // Iterate over file line by line
  int line_count = 0;
  RowView source_row;
  while (reader->next(source_row)) {
    line_count++;

    project_row_into(source_row.fields, prepared.projected_indices, setup_data.projected_row_views);

    // Check for NULL values
    setup_data.skip = row_has_skip_value(setup_data.projected_row_views);
    if (setup_data.skip) {
      continue;
    }

    auto& row = setup_data.row;
    if (plan.needs_row_map) {
      update_row_map(row, prepared.projected_header, setup_data.projected_row_views);
    }

    if (can_fast_emit_simple_plan(plan)) {
      try {
        if (!emit_fast_statement(plan, setup_data.projected_row_views, base_uri, setup_data.res,
                                 setup_data.subject_scratch, setup_data.predicate_scratch,
                                 setup_data.object_scratch, setup_data.graph, setup_data.graph_scratch)) {
          continue;
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        }
        continue;
      }
      if (!unique_triple.insert(setup_data.res).second) {
        continue;
      }
      setup_data.triple_counter++;
      continue;
    }

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    std::string g_function_value;
    if (plan.has_object_condition &&
        handle_function_call(plan.object.content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      if (!render_runtime_term(plan.subject, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.subject, setup_data.subject_scratch, s_function_value) ||
          !render_runtime_term(plan.predicate, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.predicate, setup_data.predicate_scratch, p_function_value) ||
          !render_runtime_term(plan.object, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.object, setup_data.object_scratch, o_function_value) ||
          !render_runtime_term(plan.graph, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.graph, setup_data.graph_scratch, g_function_value)) {
        continue;
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    format_statement_into(setup_data.subject, setup_data.predicate, setup_data.object, setup_data.graph, setup_data.res);
    if (!unique_triple.insert(setup_data.res).second) {
      continue;
    }
    setup_data.triple_counter++;
  }

  return unique_triple;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int execute_simple(const std::string& input_file_name,
                   const fs::path& output_file_name,
                   const std::string& base_uri,
                   const std::vector<std::string>& projected_attributes,
                   const std::vector<std::string>& s_content,
                   const std::vector<std::string>& p_content,
                   const std::vector<std::string>& o_content,
                   const std::unordered_map<std::string, std::string>& data_map,
                   OutputChunkWriter* writer = nullptr) {
  ///// Setup /////
  SetupData setup_data = initialize_setup(output_file_name, writer);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  auto reader = create_source_reader(data_map, input_file_name);

  SimplePlan source;
  source.output_file_name = output_file_name;
  source.base_uri = base_uri;
  source.input_file_name = input_file_name;
  source.projected_attributes = projected_attributes;
  source.s_content = s_content;
  source.p_content = p_content;
  source.o_content = o_content;
  source.generate_graph = false;

  PreparedSimplePlan prepared;
  if (reader->header().empty()) {
    return 0;
  }
  prepare_simple_plan_from_header(source, reader->header(), prepared);
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  int line_count = 0;
  // Iterate over file line by line
  RowView source_row;
  while (reader->next(source_row)) {
    line_count++;

    project_row_into(source_row.fields, prepared.projected_indices, setup_data.projected_row_views);

    // Check for NULL values
    setup_data.skip = row_has_skip_value(setup_data.projected_row_views);
    if (setup_data.skip) {
      continue;
    }

    auto& row = setup_data.row;
    if (plan.needs_row_map) {
      update_row_map(row, prepared.projected_header, setup_data.projected_row_views);
    }

    if (can_fast_emit_simple_plan(plan)) {
      try {
        if (!emit_fast_statement(plan, setup_data.projected_row_views, base_uri, setup_data.res,
                                 setup_data.subject_scratch, setup_data.predicate_scratch,
                                 setup_data.object_scratch, setup_data.graph, setup_data.graph_scratch)) {
          continue;
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        }
        continue;
      }
      if (!setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second) {
        continue;
      }
      setup_data.triple_counter++;
      setup_data.buffered_res += setup_data.res;
      setup_data.write_cnt++;
      if (setup_data.write_cnt == setup_data.buffer_limit) {
        setup_data.write_cnt = 0;
        flush_setup_buffer(setup_data);
      }
      continue;
    }

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    if (plan.has_object_condition &&
        handle_function_call(plan.object.content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      if (!render_runtime_term(plan.subject, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.subject, setup_data.subject_scratch, s_function_value) ||
          !render_runtime_term(plan.predicate, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.predicate, setup_data.predicate_scratch, p_function_value) ||
          !render_runtime_term(plan.object, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.object, setup_data.object_scratch, o_function_value)) {
        continue;
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    format_statement_into(setup_data.subject, setup_data.predicate, setup_data.object, setup_data.res);
    if (!setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second) {
      continue;
    }
    setup_data.triple_counter++;

    setup_data.buffered_res += setup_data.res;
    setup_data.write_cnt++;

    if (setup_data.write_cnt == setup_data.buffer_limit) {
      setup_data.write_cnt = 0;
      ////// SERIALIZE //////
      flush_setup_buffer(setup_data);
    }
  }
  ////// SERIALIZE //////
  flush_setup_buffer(setup_data);

  return setup_data.triple_counter;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::unordered_set<std::string> execute_simple_dependent(const std::string& input_file_name,
                                                         const fs::path& output_file_name,
                                                         const std::string& base_uri,
                                                         const std::vector<std::string>& projected_attributes,
                                                         const std::vector<std::string>& s_content,
                                                         const std::vector<std::string>& p_content,
                                                         const std::vector<std::string>& o_content,
                                                         std::unordered_set<std::string>& unique_triple,
                                                         const std::unordered_map<std::string, std::string>& data_map) {
  ///// Setup /////
  SetupData setup_data = initialize_setup_dependent(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input
  auto reader = create_source_reader(data_map, input_file_name);

  //////////////////////////////////////////////////////////////////////
  SimplePlan source;
  source.output_file_name = output_file_name;
  source.base_uri = base_uri;
  source.input_file_name = input_file_name;
  source.projected_attributes = projected_attributes;
  source.s_content = s_content;
  source.p_content = p_content;
  source.o_content = o_content;
  source.generate_graph = false;

  PreparedSimplePlan prepared;
  if (reader->header().empty()) {
    return unique_triple;
  }
  prepare_simple_plan_from_header(source, reader->header(), prepared);
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  // Iterate over file line by line
  int line_count = 0;

  // Check if funciton is needed
  bool function_called = plan.function_called;

  RowView source_row;
  while (reader->next(source_row)) {
    line_count++;

    project_row_into(source_row.fields, prepared.projected_indices, setup_data.projected_row_views);

    // Check for NULL values
    setup_data.skip = row_has_skip_value(setup_data.projected_row_views);

    if (setup_data.skip && !function_called) {
      continue;
    }

    auto& row = setup_data.row;
    if (plan.needs_row_map) {
      update_row_map(row, prepared.projected_header, setup_data.projected_row_views);
    }

    if (can_fast_emit_simple_plan(plan)) {
      try {
        if (!emit_fast_statement(plan, setup_data.projected_row_views, base_uri, setup_data.res,
                                 setup_data.subject_scratch, setup_data.predicate_scratch,
                                 setup_data.object_scratch, setup_data.graph, setup_data.graph_scratch)) {
          continue;
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        }
        continue;
      }
      if (!unique_triple.insert(setup_data.res).second) {
        continue;
      }
      setup_data.triple_counter++;
      continue;
    }

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    if (plan.has_object_condition &&
        handle_function_call(plan.object.content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      if (!render_runtime_term(plan.subject, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.subject, setup_data.subject_scratch, s_function_value) ||
          !render_runtime_term(plan.predicate, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.predicate, setup_data.predicate_scratch, p_function_value) ||
          !render_runtime_term(plan.object, line_count, input_file_name, setup_data.projected_row_views, base_uri, row, setup_data.object, setup_data.object_scratch, o_function_value)) {
        continue;
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    format_statement_into(setup_data.subject, setup_data.predicate, setup_data.object, setup_data.res);
    if (!unique_triple.insert(setup_data.res).second) {
      continue;
    }
    setup_data.triple_counter++;
  }

  return unique_triple;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t execute_standalone_simple_plan(const SimplePlan& info, const std::unordered_map<std::string, std::string>& data_map) {
  return execute_standalone_simple_plan(info, data_map, nullptr);
}

static void write_constant_statement(const std::vector<std::string>& s_content,
                                     const std::vector<std::string>& p_content,
                                     const std::vector<std::string>& o_content,
                                     const std::vector<std::string>& g_content,
                                     OutputChunkWriter* writer,
                                     const fs::path& output_file_name) {
  if (writer == nullptr) {
    handle_constant(s_content, p_content, o_content, g_content, output_file_name);
    return;
  }

  std::string subject;
  std::string predicate;
  std::string object;
  handle_term_type_into(s_content[2], s_content[0], "", "", subject);
  handle_term_type_into(p_content[2], p_content[0], "", "", predicate);
  handle_term_type_into(o_content[2], o_content[0],
                        o_content.size() > 3 ? o_content[3] : "",
                        o_content.size() > 4 ? o_content[4] : "",
                        object);
  if (g_content.empty()) {
    writer->write(format_statement(subject, predicate, object));
    return;
  }

  std::string graph;
  handle_term_type_into(g_content[2], g_content[0], "", "", graph);
  writer->write(format_statement(subject, predicate, object, graph));
}

static void write_preformatted_statement(const std::vector<std::string>& s_content,
                                         const std::vector<std::string>& p_content,
                                         const std::vector<std::string>& o_content,
                                         const std::vector<std::string>& g_content,
                                         OutputChunkWriter* writer,
                                         const fs::path& output_file_name) {
  if (writer == nullptr) {
    handle_constant_preformatted(s_content, p_content, o_content, g_content, output_file_name);
    return;
  }

  if (g_content.empty()) {
    writer->write(format_statement(s_content[0], p_content[0], o_content[0]));
    return;
  }
  writer->write(format_statement(s_content[0], p_content[0], o_content[0], g_content[0]));
}

size_t execute_standalone_simple_plan(const SimplePlan& info, const std::unordered_map<std::string, std::string>& data_map, OutputChunkWriter* writer) {
  size_t generated_triple = 0;
  ////////////////////////////////////////////////////////////
  // Execute
  try {
    if (info.generate_graph == false) {
      // handle without graph //
      // Check if all entrries are constant
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant") {
        write_constant_statement(info.s_content, info.p_content, info.o_content, info.g_content, writer, info.output_file_name);
        generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        write_preformatted_statement(info.s_content, info.p_content, info.o_content, info.g_content, writer, info.output_file_name);
        generated_triple = 1;
      } else {
        generated_triple = execute_simple(info.input_file_name, info.output_file_name, info.base_uri,
                                          info.projected_attributes, info.s_content, info.p_content, info.o_content, data_map, writer);
      }
    } else {
      // Handle with graph //
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant" && info.g_content[1] == "constant") {
        write_constant_statement(info.s_content, info.p_content, info.o_content, info.g_content, writer, info.output_file_name);
        generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted" && info.g_content[1] == "preformatted") {
        write_preformatted_statement(info.s_content, info.p_content, info.o_content, info.g_content, writer, info.output_file_name);
        generated_triple = 1;
      } else {
        // If not constant handle normal
        generated_triple = execute_simple_with_graph(info.input_file_name, info.output_file_name, info.base_uri,
                                                     info.projected_attributes, info.s_content, info.p_content, info.o_content, info.g_content, data_map, writer);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      throw;
    }
  } catch (...) {
    throw std::runtime_error("Unknown exception caught while executing simple mapping.");
  }

  return generated_triple;
}

size_t execute_fused_simple_plans(const std::vector<SimplePlan>& plans,
                                  const std::unordered_map<std::string, std::string>& data_map,
                                  OutputChunkWriter* writer,
                                  TripleDeduper* shared_deduper) {
  if (plans.empty()) {
    return 0;
  }

  const std::string& input_file_name = plans.front().input_file_name;
  const fs::path& output_file_name = plans.front().output_file_name;
  for (const auto& plan : plans) {
    if (plan.input_file_name != input_file_name || plan.output_file_name != output_file_name || is_single_constant_statement(plan)) {
      throw std::runtime_error("Simple plans are not compatible with fused execution.");
    }
  }

  SetupData setup_data = initialize_setup(output_file_name, writer);
  auto reader = create_source_reader(data_map, input_file_name);
  if (reader->header().empty()) {
    return 0;
  }

  FusedSimpleProgram program = compile_fused_simple_program(plans, reader->header());
  std::vector<FusedSimpleRuntime>& runtimes = program.runtimes;
  const std::vector<FusedSimpleGroup>& groups = program.groups;
  std::vector<TermCacheEntry>& term_cache_entries = program.term_cache_entries;
  std::vector<std::string> pending_shared_triples;
  if (shared_deduper != nullptr) {
    pending_shared_triples.reserve(kSharedDedupBatchSize);
  }

  int line_count = 0;
  RowView source_row;
  while (reader->next(source_row)) {
    line_count++;
    reset_fused_term_cache(program);

    for (const auto& group : groups) {
      if (group.reuse_subject_predicate_object && group.runtime_indices.size() > 1) {
        FusedSimpleRuntime& representative = runtimes[group.runtime_indices.front()];
        const CompiledSimplePlan& representative_plan = representative.prepared.compiled;
        project_row_into(source_row.fields, representative.prepared.projected_indices, representative.projected_row);

        if (row_has_skip_value(representative.projected_row)) {
          continue;
        }

        try {
          if (can_fast_emit_simple_plan(representative_plan)) {
            representative.subject.clear();
            representative.predicate.clear();
            representative.object.clear();
            if (!append_fast_compiled_term(representative_plan.subject, representative.projected_row, representative.prepared.source.base_uri, representative.subject, representative.subject_scratch) ||
                !append_fast_compiled_term(representative_plan.predicate, representative.projected_row, representative.prepared.source.base_uri, representative.predicate, representative.predicate_scratch) ||
                !append_fast_compiled_term(representative_plan.object, representative.projected_row, representative.prepared.source.base_uri, representative.object, representative.object_scratch)) {
              continue;
            }
          } else {
            std::string s_function_value;
            std::string p_function_value;
            std::string o_function_value;
            if (!render_runtime_term(representative_plan.subject, line_count, input_file_name, representative.projected_row, representative.prepared.source.base_uri, representative.row, representative.subject, representative.subject_scratch, s_function_value) ||
                !render_runtime_term(representative_plan.predicate, line_count, input_file_name, representative.projected_row, representative.prepared.source.base_uri, representative.row, representative.predicate, representative.predicate_scratch, p_function_value) ||
                !render_runtime_term(representative_plan.object, line_count, input_file_name, representative.projected_row, representative.prepared.source.base_uri, representative.row, representative.object, representative.object_scratch, o_function_value)) {
              continue;
            }
          }
        } catch (const std::runtime_error& e) {
          if (continue_on_error == false) {
            std::cout << e.what() << std::endl;
            std::exit(1);
          }
          continue;
        }

        for (std::size_t runtime_index : group.runtime_indices) {
          FusedSimpleRuntime& runtime = runtimes[runtime_index];
          const CompiledSimplePlan& plan = runtime.prepared.compiled;
          try {
            if (can_fast_emit_simple_plan(plan)) {
              runtime.graph.clear();
              if (!append_fast_compiled_term(plan.graph, representative.projected_row, runtime.prepared.source.base_uri, runtime.graph, runtime.graph_scratch)) {
                continue;
              }
            } else {
              std::string g_function_value;
              if (!render_runtime_term(plan.graph, line_count, input_file_name, representative.projected_row, runtime.prepared.source.base_uri, runtime.row, runtime.graph, runtime.graph_scratch, g_function_value)) {
                continue;
              }
            }
          } catch (const std::runtime_error& e) {
            if (continue_on_error == false) {
              std::cout << e.what() << std::endl;
              std::exit(1);
            }
            continue;
          }

          format_statement_into(representative.subject, representative.predicate, representative.object, runtime.graph, setup_data.res);
          emit_unique_result(setup_data, shared_deduper, pending_shared_triples);
        }
        continue;
      }

      FusedSimpleRuntime& runtime = runtimes[group.runtime_indices.front()];
      const CompiledSimplePlan& plan = runtime.prepared.compiled;
      project_row_into(source_row.fields, runtime.prepared.projected_indices, runtime.projected_row);

      const bool skip = row_has_skip_value(runtime.projected_row);
      if (skip && !plan.function_called) {
        continue;
      }

      if (plan.needs_row_map) {
        update_row_map(runtime.row, runtime.prepared.projected_header, runtime.projected_row);
      }

      if (can_fast_emit_simple_plan(plan)) {
        try {
          if (has_cached_fused_term(runtime)) {
            if (!emit_cached_fast_statement(plan, runtime.projected_row, runtime.prepared.source.base_uri,
                                            runtime.subject_cache_id, runtime.predicate_cache_id,
                                            runtime.object_cache_id, runtime.graph_cache_id,
                                            term_cache_entries, setup_data.res,
                                            runtime.subject_scratch, runtime.predicate_scratch,
                                            runtime.object_scratch, runtime.graph, runtime.graph_scratch)) {
              continue;
            }
          } else if (!emit_fast_statement(plan, runtime.projected_row, runtime.prepared.source.base_uri, setup_data.res,
                                          runtime.subject_scratch, runtime.predicate_scratch,
                                          runtime.object_scratch, runtime.graph, runtime.graph_scratch)) {
            continue;
          }
        } catch (const std::runtime_error& e) {
          if (continue_on_error == false) {
            std::cout << e.what() << std::endl;
            std::exit(1);
          }
          continue;
        }
        emit_unique_result(setup_data, shared_deduper, pending_shared_triples);
        continue;
      }

      std::string s_function_value;
      std::string p_function_value;
      std::string o_function_value;
      std::string g_function_value;
      if (plan.has_object_condition &&
          handle_function_call(plan.object.content[5], line_count, input_file_name, runtime.row) != "true") {
        continue;
      }

      try {
        if (!render_runtime_term(plan.subject, line_count, input_file_name, runtime.projected_row, runtime.prepared.source.base_uri, runtime.row, runtime.subject, runtime.subject_scratch, s_function_value) ||
            !render_runtime_term(plan.predicate, line_count, input_file_name, runtime.projected_row, runtime.prepared.source.base_uri, runtime.row, runtime.predicate, runtime.predicate_scratch, p_function_value) ||
            !render_runtime_term(plan.object, line_count, input_file_name, runtime.projected_row, runtime.prepared.source.base_uri, runtime.row, runtime.object, runtime.object_scratch, o_function_value)) {
          continue;
        }
        if (plan.has_graph &&
            !render_runtime_term(plan.graph, line_count, input_file_name, runtime.projected_row, runtime.prepared.source.base_uri, runtime.row, runtime.graph, runtime.graph_scratch, g_function_value)) {
          continue;
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        }
        continue;
      }

      if (plan.has_graph) {
        format_statement_into(runtime.subject, runtime.predicate, runtime.object, runtime.graph, setup_data.res);
      } else {
        format_statement_into(runtime.subject, runtime.predicate, runtime.object, setup_data.res);
      }
      emit_unique_result(setup_data, shared_deduper, pending_shared_triples);
    }
  }

  flush_shared_dedup_batch(setup_data, shared_deduper, pending_shared_triples);
  flush_setup_buffer(setup_data);
  return setup_data.triple_counter;
}

std::unordered_set<std::string> execute_dependent_simple_plan(const SimplePlan& info, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map) {
  ////////////////////////////////////////////////////////////
  // Execute

  try {
    if (info.generate_graph == false) {
      // handle without graph //

      // Check if all entrries are constant
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant") {
        std::vector<std::string> g_content;
        unique_triple = handle_constant_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        unique_triple = handle_constant_preformatted_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
      } else {
        unique_triple = execute_simple_dependent(info.input_file_name, info.output_file_name, info.base_uri, info.projected_attributes,
                                                 info.s_content, info.p_content, info.o_content, unique_triple, data_map);
      }
    } else {
      // Handle with graph
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant" && info.g_content[1] == "constant") {
        unique_triple = handle_constant_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted" && info.g_content[1] == "preformatted") {
        unique_triple = handle_constant_preformatted_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
      } else {
        // If not constant handle normal
        unique_triple = execute_simple_with_graph_dependent(info.input_file_name, info.output_file_name, info.base_uri, info.projected_attributes,
                                                            info.s_content, info.p_content, info.o_content, info.g_content, unique_triple, data_map);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      throw;
    }
  } catch (...) {
    throw std::runtime_error("Unknown exception caught while executing dependent simple mapping.");
  }

  return unique_triple;
}
