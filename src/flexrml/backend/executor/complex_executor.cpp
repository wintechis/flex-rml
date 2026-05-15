#include "complex_executor.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "csv_row.h"
#include "definitions.h"
#include "join_program.h"
#include "term_cache.h"
#include "term_program.h"
#include "utils.h"
#include "xxhash.h"

namespace fs = std::filesystem;

constexpr std::size_t kOutputBufferReserve = 64 * 1024;
static const std::string kConstantTermMapType = "constant";

static void create_parent_directories_if_needed(const fs::path& path) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

static void initialize_row_map(std::unordered_map<std::string, std::string>& row,
                               const std::vector<std::string>& joined_headers) {
  row.clear();
  row.reserve(joined_headers.size());
  for (const auto& name : joined_headers) {
    row.try_emplace(name);
  }
}

static void update_row_map(std::unordered_map<std::string, std::string>& row,
                           const std::vector<std::string>& joined_headers,
                           const std::vector<std::string_view>& joined_row) {
  for (std::size_t i = 0; i < joined_row.size(); ++i) {
    auto it = row.find(joined_headers[i]);
    if (it != row.end()) {
      it->second = joined_row[i];
    }
  }
}

static bool render_runtime_term(const CompiledRuntimeTerm& term,
                                int line_count,
                                const std::string& input_file_name,
                                const std::vector<std::string_view>& joined_row,
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
      render_compiled_term(term.compiled, joined_row, base_uri, out, scratch);
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

static void reset_complex_term_cache(CompiledComplexRenderPlan& plan) {
  reset_term_cache(plan.term_cache_entries);
}

static bool render_cached_runtime_term(const CompiledRuntimeTerm& term,
                                       int line_count,
                                       const std::string& input_file_name,
                                       const std::vector<std::string_view>& joined_row,
                                       const std::string& base_uri,
                                       std::unordered_map<std::string, std::string>& row,
                                       int cache_id,
                                       std::vector<TermCacheEntry>& cache_entries,
                                       std::string& out,
                                       std::string& scratch,
                                       std::string& function_value) {
  if (cache_id < 0) {
    return render_runtime_term(term, line_count, input_file_name, joined_row, base_uri, row, out, scratch, function_value);
  }

  TermCacheEntry& entry = cache_entries[static_cast<std::size_t>(cache_id)];
  if (!entry.computed) {
    entry.value.clear();
    if (!render_runtime_term(term, line_count, input_file_name, joined_row, base_uri, row, entry.value, entry.scratch, function_value)) {
      return false;
    }
    entry.computed = true;
  }
  out = entry.value;
  return true;
}

static uint64_t combined_hash_views(const std::vector<std::string_view>& fields) {
  uint64_t hash = 0;
  for (const auto& field : fields) {
    uint64_t field_hash = XXH3_64bits(field.data(), field.size());
    hash ^= field_hash + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
  }
  return hash;
}

// Build header mapping and return header vector and index map.
std::pair<std::vector<std::string>, std::unordered_map<std::string, int>> build_header(const std::string& header_line, const std::string& prefix) {
  auto original = split_csv_line(header_line, ',');

  std::vector<std::string> headers;
  std::unordered_map<std::string, int> header_idx;

  for (int i = 0; i < original.size(); ++i) {
    std::string full = prefix + "_" + original[i];
    headers.push_back(full);
    header_idx[full] = i;
  }

  return {headers, header_idx};
}

// Get projected column indices from a header mapping
std::vector<int> get_projected_indices(const std::vector<std::string>& proj_attrs, const std::string& prefix, const std::unordered_map<std::string, int>& header_idx) {
  std::vector<int> indices;

  for (const auto& attr : proj_attrs) {
    std::string full = prefix + "_" + attr;
    auto it = header_idx.find(full);  // Efficient lookup

    if (it == header_idx.end()) {
      throw std::runtime_error("Attribute not found: '" + full + "'");
    }

    indices.push_back(it->second);
  }

  return indices;
}

struct JoinBinding {
  int index = -1;
  std::string constant;
};

struct JoinKey128 {
  XXH64_hash_t low = 0;
  XXH64_hash_t high = 0;

  bool operator==(const JoinKey128& other) const {
    return low == other.low && high == other.high;
  }
};

struct JoinKey128Hasher {
  auto operator()(const JoinKey128& key) const noexcept -> std::uint64_t {
    return key.low ^ (key.high + 0x9e3779b97f4a7c15ULL + (key.low << 6) + (key.low >> 2));
  }
};

using JoinHashTable = ankerl::unordered_dense::map<JoinKey128,
                                                   std::vector<std::vector<std::string>>,
                                                   JoinKey128Hasher>;
using RowHashSet = ankerl::unordered_dense::set<uint64_t>;

JoinKey128 build_join_key(const std::vector<std::string>& row,
                          const std::vector<JoinBinding>& join_bindings) {
  JoinKey128 key;
  for (const auto& binding : join_bindings) {
    const std::string& value = binding.index >= 0 ? row[binding.index] : binding.constant;
    key.low = XXH64(value.data(), value.size(), key.low ^ value.size());
    key.high = XXH64(value.data(), value.size(), key.high ^ 0x9e3779b97f4a7c15ULL ^ value.size());
  }
  return key;
}

JoinKey128 build_join_key(const std::vector<std::string_view>& row,
                          const std::vector<JoinBinding>& join_bindings) {
  JoinKey128 key;
  for (const auto& binding : join_bindings) {
    const std::string_view value = binding.index >= 0 ? row[binding.index] : std::string_view(binding.constant);
    key.low = XXH64(value.data(), value.size(), key.low ^ value.size());
    key.high = XXH64(value.data(), value.size(), key.high ^ 0x9e3779b97f4a7c15ULL ^ value.size());
  }
  return key;
}

JoinBinding resolve_join_binding(const std::vector<std::string>& proj_attrs,
                                 const std::string& prefix,
                                 const std::string& join_attr) {
  for (size_t i = 0; i < proj_attrs.size(); ++i) {
    if (prefix + "_" + proj_attrs[i] == join_attr) {
      return {static_cast<int>(i), ""};
    }
  }

  const std::string join_prefix = prefix + "_";
  if (join_attr.starts_with(join_prefix)) {
    return {-1, join_attr.substr(join_prefix.size())};
  }

  std::cerr << "Error: Join attribute " << join_attr << " not found in projected attributes." << std::endl;
  std::exit(1);
}

std::vector<JoinBinding> resolve_join_bindings(const std::vector<std::string>& proj_attrs,
                                               const std::string& prefix,
                                               const std::vector<std::string>& join_attrs) {
  std::vector<JoinBinding> bindings;
  bindings.reserve(join_attrs.size());
  for (const auto& join_attr : join_attrs) {
    bindings.push_back(resolve_join_binding(proj_attrs, prefix, join_attr));
  }
  return bindings;
}

static void append_joined_views(const std::vector<std::string>& left_row,
                                const std::vector<std::string_view>& right_row,
                                std::vector<std::string_view>& joined_row) {
  joined_row.clear();
  joined_row.reserve(left_row.size() + right_row.size());
  for (const auto& value : left_row) {
    joined_row.emplace_back(value);
  }
  joined_row.insert(joined_row.end(), right_row.begin(), right_row.end());
}

JoinHashTable build_hash_table(std::istream& input_file,
                               const std::vector<int>& projected_indeces,
                               const std::vector<JoinBinding>& join_bindings) {
  JoinHashTable hash_table;
  RowHashSet unique_hashes;

  std::string line;
  std::vector<std::string_view> split_line_views;
  std::vector<std::string> split_line;
  std::vector<std::string_view> projected_row_views;
  std::vector<std::string> projected_row;
  split_line_views.reserve(64);
  split_line.reserve(64);
  projected_row_views.reserve(projected_indeces.size());
  projected_row.reserve(projected_indeces.size());

  while (std::getline(input_file, line)) {
    if (split_csv_line_views_into(line, ',', split_line_views)) {
      project_row_into(split_line_views, projected_indeces, projected_row_views);
    } else {
      split_csv_line_into(line, ',', split_line);
      project_row_into(split_line, projected_indeces, projected_row);
      project_row_views_from_strings(projected_row, projected_row_views);
    }

    if (row_has_skip_value(projected_row_views)) {
      continue;
    }

    uint64_t hash = combined_hash_views(projected_row_views);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    JoinKey128 key = build_join_key(projected_row_views, join_bindings);
    materialize_row_views(projected_row_views, projected_row);
    auto [it, inserted] = hash_table.try_emplace(key);
    if (inserted) {
      it->second.reserve(1);
    }
    it->second.push_back(projected_row);
  }

  return hash_table;
}


// Open File or from map
static std::unique_ptr<std::istream> open_from_map_or_file(
    const std::unordered_map<std::string, std::string>& mem,
    const std::string& path){
    if (auto it = mem.find(path); it != mem.end()) {
        return std::make_unique<std::istringstream>(it->second);
    }

    auto f = std::make_unique<std::ifstream>(path);
    if (!f->is_open()) {
        throw std::runtime_error("Could not open logical source: " + path);
    }
    return f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int execute_complex(const fs::path& output_file_name,
                     const std::string& left_path,
                     const std::string& right_path,
                     const std::string& left_name,
                     const std::string& right_name,
                     const std::vector<std::string>& left_join_attrs,
                     const std::vector<std::string>& right_join_attrs,
                     const std::string& base_uri,
                     const std::vector<std::string>& projected_attributes_left,
                     const std::vector<std::string>& projected_attributes_right,
                     const std::vector<std::string>& s_content,
                     const std::vector<std::string>& p_content,
                     const std::vector<std::string>& o_content,
                     const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  std::string line;
  RowHashSet unique_hashes;
  size_t triple_counter = 0;
  size_t write_cnt = 0;
  size_t buffer_limit = 20000;
  std::string buffered_res;
  std::string subject;
  std::string predicate;
  std::string object;
  std::string res;
  std::string subject_scratch;
  std::string predicate_scratch;
  std::string object_scratch;
  buffered_res.reserve(kOutputBufferReserve);
  subject.reserve(512);
  predicate.reserve(512);
  object.reserve(512);
  res.reserve(2048);
  subject_scratch.reserve(512);
  predicate_scratch.reserve(512);
  object_scratch.reserve(512);

  // Open output file
  create_parent_directories_if_needed(output_file_name);
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cerr << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }

  //////////////////////////////////////////////////////////////////////

  // Open CSV files
  auto left_file = open_from_map_or_file(data_map, left_path);
  auto right_file = open_from_map_or_file(data_map, right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  if (!std::getline(*left_file, left_header_line) || !std::getline(*right_file, right_header_line)) {
    return 0;
  }

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  std::vector<JoinBinding> left_join_bindings = resolve_join_bindings(projected_attributes_left, left_name, left_join_attrs);
  std::vector<JoinBinding> right_join_bindings = resolve_join_bindings(projected_attributes_right, right_name, right_join_attrs);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file (store only projected columns).
  auto hash_table = build_hash_table(*left_file, left_proj_indices, left_join_bindings);

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }
  CompiledComplexRenderPlan render_plan =
      compile_complex_render_plan(joined_headers, base_uri, s_content, p_content, o_content);
  std::unordered_map<std::string, std::string> row_map;
  if (render_plan.needs_row_map) {
    initialize_row_map(row_map, joined_headers);
  }

  // Process right file
  std::vector<std::string_view> split_line_views;
  std::vector<std::string> split_line;
  std::vector<std::string_view> projected_row;
  std::vector<std::string> projected_row_storage;
  split_line_views.reserve(64);
  split_line.reserve(64);
  projected_row.reserve(right_proj_indices.size());
  projected_row_storage.reserve(right_proj_indices.size());
  std::vector<std::string_view> joined_row;
  joined_row.reserve(joined_headers.size());
  int line_count = 0;
  while (getline(*right_file, line)) {
    line_count++;
    if (split_csv_line_views_into(line, ',', split_line_views)) {
      project_row_into(split_line_views, right_proj_indices, projected_row);
    } else {
      split_csv_line_into(line, ',', split_line);
      project_row_into(split_line, right_proj_indices, projected_row_storage);
      project_row_views_from_strings(projected_row_storage, projected_row);
    }

    // Check for unwanted values
    if (row_has_skip_value(projected_row)) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combined_hash_views(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    JoinKey128 key = build_join_key(projected_row, right_join_bindings);
    auto matching_rows = hash_table.find(key);
    if (matching_rows == hash_table.end()) {
      continue;
    }

    for (const auto& left_row : matching_rows->second) {
      // Combine left and right filtered rows
      append_joined_views(left_row, projected_row, joined_row);

      // Generate triple
      if (render_plan.needs_row_map) {
        update_row_map(row_map, joined_headers, joined_row);
      }

      std::string s_function_value;
      std::string p_function_value;
      std::string o_function_value;
      if (render_plan.has_object_condition &&
          handle_function_call(render_plan.object.content[5], line_count, right_path, row_map) != "true") {
        continue;
      }
      reset_complex_term_cache(render_plan);

      ////// CREATE //////
      try {
        if (!render_cached_runtime_term(render_plan.subject, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.subject_cache_id, render_plan.term_cache_entries,
                                        subject, subject_scratch, s_function_value) ||
            !render_cached_runtime_term(render_plan.predicate, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.predicate_cache_id, render_plan.term_cache_entries,
                                        predicate, predicate_scratch, p_function_value) ||
            !render_cached_runtime_term(render_plan.object, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.object_cache_id, render_plan.term_cache_entries,
                                        object, object_scratch, o_function_value)) {
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

      format_statement_into(subject, predicate, object, res);
      triple_counter++;

      buffered_res += res;
      write_cnt++;

      if (write_cnt == buffer_limit) {
        write_cnt = 0;
        ////// SERIALIZE //////
        outputFile << buffered_res;
        buffered_res = "";
      }
    }
  }

  ////// SERIALIZE //////
  outputFile << buffered_res;

  return triple_counter;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unordered_set<std::string> execute_complex_dependent(const fs::path& output_file_name,
                                                          const std::string& left_path,
                                                          const std::string& right_path,
                                                          const std::string& left_name,
                                                          const std::string& right_name,
                                                          const std::vector<std::string>& left_join_attrs,
                                                          const std::vector<std::string>& right_join_attrs,
                                                          const std::string& base_uri,
                                                          const std::vector<std::string>& projected_attributes_left,
                                                          const std::vector<std::string>& projected_attributes_right,
                                                          const std::vector<std::string>& s_content,
                                                          const std::vector<std::string>& p_content,
                                                          const std::vector<std::string>& o_content,
                                                          std::unordered_set<std::string>& unique_triple,
                                                          const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  std::string line;
  RowHashSet unique_hashes;
  std::string subject;
  std::string predicate;
  std::string object;
  std::string res;
  std::string subject_scratch;
  std::string predicate_scratch;
  std::string object_scratch;
  subject.reserve(512);
  predicate.reserve(512);
  object.reserve(512);
  res.reserve(2048);
  subject_scratch.reserve(512);
  predicate_scratch.reserve(512);
  object_scratch.reserve(512);

  //////////////////////////////////////////////////////////////////////
  // Open CSV files
  auto left_file = open_from_map_or_file(data_map, left_path);
  auto right_file = open_from_map_or_file(data_map, right_path);

  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(*left_file, left_header_line);
  std::getline(*right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  std::vector<JoinBinding> left_join_bindings = resolve_join_bindings(projected_attributes_left, left_name, left_join_attrs);
  std::vector<JoinBinding> right_join_bindings = resolve_join_bindings(projected_attributes_right, right_name, right_join_attrs);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file using the left join index
  auto hash_table = build_hash_table(*left_file, left_proj_indices, left_join_bindings);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }
  CompiledComplexRenderPlan render_plan =
      compile_complex_render_plan(joined_headers, base_uri, s_content, p_content, o_content);
  std::unordered_map<std::string, std::string> row_map;
  if (render_plan.needs_row_map) {
    initialize_row_map(row_map, joined_headers);
  }

  // Process right file
  std::vector<std::string_view> split_line_views;
  std::vector<std::string> split_line;
  std::vector<std::string_view> projected_row;
  std::vector<std::string> projected_row_storage;
  split_line_views.reserve(64);
  split_line.reserve(64);
  projected_row.reserve(right_proj_indices.size());
  projected_row_storage.reserve(right_proj_indices.size());
  std::vector<std::string_view> joined_row;
  joined_row.reserve(joined_headers.size());
  int line_count = 0;
  while (getline(*right_file, line)) {
    line_count++;
    if (split_csv_line_views_into(line, ',', split_line_views)) {
      project_row_into(split_line_views, right_proj_indices, projected_row);
    } else {
      split_csv_line_into(line, ',', split_line);
      project_row_into(split_line, right_proj_indices, projected_row_storage);
      project_row_views_from_strings(projected_row_storage, projected_row);
    }

    // Check for unwanted values
    if (row_has_skip_value(projected_row)) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combined_hash_views(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    JoinKey128 key = build_join_key(projected_row, right_join_bindings);
    auto matching_rows = hash_table.find(key);
    if (matching_rows == hash_table.end()) {
      continue;
    }

    for (const auto& left_row : matching_rows->second) {
      // Combine left and right filtered rows
      append_joined_views(left_row, projected_row, joined_row);

      if (render_plan.needs_row_map) {
        update_row_map(row_map, joined_headers, joined_row);
      }

      std::string s_function_value;
      std::string p_function_value;
      std::string o_function_value;
      if (render_plan.has_object_condition &&
          handle_function_call(render_plan.object.content[5], line_count, right_path, row_map) != "true") {
        continue;
      }
      reset_complex_term_cache(render_plan);

      try {
        if (!render_cached_runtime_term(render_plan.subject, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.subject_cache_id, render_plan.term_cache_entries,
                                        subject, subject_scratch, s_function_value) ||
            !render_cached_runtime_term(render_plan.predicate, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.predicate_cache_id, render_plan.term_cache_entries,
                                        predicate, predicate_scratch, p_function_value) ||
            !render_cached_runtime_term(render_plan.object, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.object_cache_id, render_plan.term_cache_entries,
                                        object, object_scratch, o_function_value)) {
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

      format_statement_into(subject, predicate, object, res);

      unique_triple.insert(res);
    }
  }
  return unique_triple;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int execute_complex_with_graph(const fs::path& output_file_name,
                               const std::string& left_path, const std::string& right_path,
                               const std::string& left_name,
                               const std::string& right_name,
                               const std::vector<std::string>& left_join_attrs,
                               const std::vector<std::string>& right_join_attrs,
                               const std::string& base_uri,
                               const std::vector<std::string>& projected_attributes_left,
                               const std::vector<std::string>& projected_attributes_right,
                               const std::vector<std::string>& s_content,
                               const std::vector<std::string>& p_content,
                               const std::vector<std::string>& o_content,
                               const std::vector<std::string>& g_content,
                               const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  std::string line;
  RowHashSet unique_hashes;
  size_t triple_counter = 0;

  size_t write_cnt = 0;
  size_t buffer_limit = 20000;
  std::string buffered_res;
  std::string subject;
  std::string predicate;
  std::string object;
  std::string graph;
  std::string res;
  std::string subject_scratch;
  std::string predicate_scratch;
  std::string object_scratch;
  std::string graph_scratch;
  buffered_res.reserve(kOutputBufferReserve);
  subject.reserve(512);
  predicate.reserve(512);
  object.reserve(512);
  graph.reserve(512);
  res.reserve(2048);
  subject_scratch.reserve(512);
  predicate_scratch.reserve(512);
  object_scratch.reserve(512);
  graph_scratch.reserve(512);

  // Open output file
  create_parent_directories_if_needed(output_file_name);
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cerr << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }

  //////////////////////////////////////////////////////////////////////
  // Open CSV files
  auto left_file = open_from_map_or_file(data_map, left_path);
  auto right_file = open_from_map_or_file(data_map, right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(*left_file, left_header_line);
  std::getline(*right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  std::vector<JoinBinding> left_join_bindings = resolve_join_bindings(projected_attributes_left, left_name, left_join_attrs);
  std::vector<JoinBinding> right_join_bindings = resolve_join_bindings(projected_attributes_right, right_name, right_join_attrs);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file (store only projected columns).
  auto hash_table = build_hash_table(*left_file, left_proj_indices, left_join_bindings);

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }
  CompiledComplexRenderPlan render_plan =
      compile_complex_render_plan(joined_headers, base_uri, s_content, p_content, o_content, &g_content);
  std::unordered_map<std::string, std::string> row_map;
  if (render_plan.needs_row_map) {
    initialize_row_map(row_map, joined_headers);
  }

  // Process right file
  std::vector<std::string_view> split_line_views;
  std::vector<std::string> split_line;
  std::vector<std::string_view> projected_row;
  std::vector<std::string> projected_row_storage;
  split_line_views.reserve(64);
  split_line.reserve(64);
  projected_row.reserve(right_proj_indices.size());
  projected_row_storage.reserve(right_proj_indices.size());
  std::vector<std::string_view> joined_row;
  joined_row.reserve(joined_headers.size());
  int line_count = 0;
  while (getline(*right_file, line)) {
    line_count++;
    if (split_csv_line_views_into(line, ',', split_line_views)) {
      project_row_into(split_line_views, right_proj_indices, projected_row);
    } else {
      split_csv_line_into(line, ',', split_line);
      project_row_into(split_line, right_proj_indices, projected_row_storage);
      project_row_views_from_strings(projected_row_storage, projected_row);
    }

    // Check for unwanted values
    if (row_has_skip_value(projected_row)) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combined_hash_views(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    JoinKey128 key = build_join_key(projected_row, right_join_bindings);
    auto matching_rows = hash_table.find(key);
    if (matching_rows == hash_table.end()) {
      continue;
    }

    for (const auto& left_row : matching_rows->second) {
      // Combine left and right filtered rows
      append_joined_views(left_row, projected_row, joined_row);

      ////// CREATE //////
      if (render_plan.needs_row_map) {
        update_row_map(row_map, joined_headers, joined_row);
      }

      std::string s_function_value;
      std::string p_function_value;
      std::string o_function_value;
      std::string g_function_value;
      if (render_plan.has_object_condition &&
          handle_function_call(render_plan.object.content[5], line_count, right_path, row_map) != "true") {
        continue;
      }
      reset_complex_term_cache(render_plan);

      try {
        if (!render_cached_runtime_term(render_plan.subject, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.subject_cache_id, render_plan.term_cache_entries,
                                        subject, subject_scratch, s_function_value) ||
            !render_cached_runtime_term(render_plan.predicate, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.predicate_cache_id, render_plan.term_cache_entries,
                                        predicate, predicate_scratch, p_function_value) ||
            !render_cached_runtime_term(render_plan.object, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.object_cache_id, render_plan.term_cache_entries,
                                        object, object_scratch, o_function_value) ||
            !render_cached_runtime_term(render_plan.graph, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.graph_cache_id, render_plan.term_cache_entries,
                                        graph, graph_scratch, g_function_value)) {
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

      format_statement_into(subject, predicate, object, graph, res);
      triple_counter++;

      buffered_res += res;
      write_cnt++;

      if (write_cnt == buffer_limit) {
        write_cnt = 0;
        ////// SERIALIZE //////
        outputFile << buffered_res;
        buffered_res = "";
      }
    }
  }
  ////// SERIALIZE //////
  outputFile << buffered_res;

  return triple_counter;
}

//////////////////////////////////////////////////////////////

std::unordered_set<std::string> execute_complex_with_graph_dependent(const fs::path& output_file_name,
                                                                     const std::string& left_path,
                                                                     const std::string& right_path,
                                                                     const std::string& left_name,
                                                                     const std::string& right_name,
                                                                     const std::vector<std::string>& left_join_attrs,
                                                                     const std::vector<std::string>& right_join_attrs,
                                                                     const std::string& base_uri,
                                                                     const std::vector<std::string>& projected_attributes_left,
                                                                     const std::vector<std::string>& projected_attributes_right,
                                                                     const std::vector<std::string>& s_content,
                                                                     const std::vector<std::string>& p_content,
                                                                     const std::vector<std::string>& o_content,
                                                                     const std::vector<std::string>& g_content,
                                                                     std::unordered_set<std::string>& unique_triple,
                                                                     const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  std::string line;
  RowHashSet unique_hashes;
  std::string subject;
  std::string predicate;
  std::string object;
  std::string graph;
  std::string res;
  std::string subject_scratch;
  std::string predicate_scratch;
  std::string object_scratch;
  std::string graph_scratch;
  subject.reserve(512);
  predicate.reserve(512);
  object.reserve(512);
  graph.reserve(512);
  res.reserve(2048);
  subject_scratch.reserve(512);
  predicate_scratch.reserve(512);
  object_scratch.reserve(512);
  graph_scratch.reserve(512);

  //////////////////////////////////////////////////////////////////////
  // Open CSV files
  auto left_file = open_from_map_or_file(data_map, left_path);
  auto right_file = open_from_map_or_file(data_map, right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(*left_file, left_header_line);
  std::getline(*right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  std::vector<JoinBinding> left_join_bindings = resolve_join_bindings(projected_attributes_left, left_name, left_join_attrs);
  std::vector<JoinBinding> right_join_bindings = resolve_join_bindings(projected_attributes_right, right_name, right_join_attrs);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file using the left join index
  auto hash_table = build_hash_table(*left_file, left_proj_indices, left_join_bindings);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }
  CompiledComplexRenderPlan render_plan =
      compile_complex_render_plan(joined_headers, base_uri, s_content, p_content, o_content, &g_content);
  std::unordered_map<std::string, std::string> row_map;
  if (render_plan.needs_row_map) {
    initialize_row_map(row_map, joined_headers);
  }

  // Process right file
  std::vector<std::string_view> split_line_views;
  std::vector<std::string> split_line;
  std::vector<std::string_view> projected_row;
  std::vector<std::string> projected_row_storage;
  split_line_views.reserve(64);
  split_line.reserve(64);
  projected_row.reserve(right_proj_indices.size());
  projected_row_storage.reserve(right_proj_indices.size());
  std::vector<std::string_view> joined_row;
  joined_row.reserve(joined_headers.size());
  int line_count = 0;
  while (getline(*right_file, line)) {
    line_count++;
    if (split_csv_line_views_into(line, ',', split_line_views)) {
      project_row_into(split_line_views, right_proj_indices, projected_row);
    } else {
      split_csv_line_into(line, ',', split_line);
      project_row_into(split_line, right_proj_indices, projected_row_storage);
      project_row_views_from_strings(projected_row_storage, projected_row);
    }

    // Check for unwanted values
    if (row_has_skip_value(projected_row)) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combined_hash_views(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    JoinKey128 key = build_join_key(projected_row, right_join_bindings);
    auto matching_rows = hash_table.find(key);
    if (matching_rows == hash_table.end()) {
      continue;
    }

    for (const auto& left_row : matching_rows->second) {
      // Combine left and right filtered rows
      append_joined_views(left_row, projected_row, joined_row);

      ////// CREATE //////
      if (render_plan.needs_row_map) {
        update_row_map(row_map, joined_headers, joined_row);
      }

      std::string s_function_value;
      std::string p_function_value;
      std::string o_function_value;
      std::string g_function_value;
      if (render_plan.has_object_condition &&
          handle_function_call(render_plan.object.content[5], line_count, right_path, row_map) != "true") {
        continue;
      }
      reset_complex_term_cache(render_plan);

      try {
        if (!render_cached_runtime_term(render_plan.subject, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.subject_cache_id, render_plan.term_cache_entries,
                                        subject, subject_scratch, s_function_value) ||
            !render_cached_runtime_term(render_plan.predicate, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.predicate_cache_id, render_plan.term_cache_entries,
                                        predicate, predicate_scratch, p_function_value) ||
            !render_cached_runtime_term(render_plan.object, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.object_cache_id, render_plan.term_cache_entries,
                                        object, object_scratch, o_function_value) ||
            !render_cached_runtime_term(render_plan.graph, line_count, right_path, joined_row, base_uri, row_map,
                                        render_plan.graph_cache_id, render_plan.term_cache_entries,
                                        graph, graph_scratch, g_function_value)) {
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

      format_statement_into(subject, predicate, object, graph, res);

      unique_triple.insert(res);
    }
  }
  return unique_triple;
}

//////////////////////////////////////////////////////////////
size_t execute_standalone_complex_plan(const ComplexPlan& info, const std::unordered_map<std::string, std::string>& data_map) {
  size_t generated_triple = 0;

  try {
    if (!info.generate_graph) {
      // handle without graph //
      // Check if all entrries are constant
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant") {
        handle_constant(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        generated_triple = 1;

      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        handle_constant_preformatted(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        generated_triple = 1;
      } else {
        generated_triple = execute_complex(info.output_file_name, info.left_path, info.right_path, info.left_name, info.right_name, info.left_join_attrs, info.right_join_attrs, info.base_uri,
                                           info.projected_attributes_left, info.projected_attributes_right, info.s_content, info.p_content, info.o_content, data_map);
      }
    } else {
      // Handle with graph
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant" && info.g_content[1] == "constant") {
        handle_constant(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted" && info.g_content[1] == "preformatted") {
        handle_constant_preformatted(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        generated_triple = 1;
      } else {
        // If not constant handle normal
        generated_triple = execute_complex_with_graph(info.output_file_name, info.left_path, info.right_path, info.left_name, info.right_name, info.left_join_attrs, info.right_join_attrs, info.base_uri,
                                                      info.projected_attributes_left, info.projected_attributes_right, info.s_content, info.p_content, info.o_content, info.g_content, data_map);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      throw;
    }
  } catch (...) {
    throw std::runtime_error("Unknown exception caught while executing complex mapping.");
  }

  return generated_triple;
}

std::unordered_set<std::string> execute_dependent_complex_plan(const ComplexPlan& info, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map) {
  try {
    if (!info.generate_graph) {
      // handle without graph //
      // Check if all entrries are constant
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant") {
        handle_constant(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        handle_constant_preformatted_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
      } else {
        unique_triple = execute_complex_dependent(info.output_file_name, info.left_path, info.right_path, info.left_name, info.right_name, info.left_join_attrs, info.right_join_attrs, info.base_uri,
                                                  info.projected_attributes_left, info.projected_attributes_right, info.s_content, info.p_content, info.o_content, unique_triple, data_map);
      }
    } else {
      // Handle with graph //
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant" && info.g_content[1] == "constant") {
        handle_constant(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        handle_constant_preformatted_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
      } else {
        unique_triple = execute_complex_with_graph_dependent(info.output_file_name, info.left_path, info.right_path, info.left_name, info.right_name, info.left_join_attrs, info.right_join_attrs, info.base_uri,
                                                             info.projected_attributes_left, info.projected_attributes_right, info.s_content, info.p_content, info.o_content, info.g_content, unique_triple, data_map);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      throw;
    }
  } catch (...) {
    throw std::runtime_error("Unknown exception caught while executing dependent complex mapping.");
  }

  return unique_triple;
}
