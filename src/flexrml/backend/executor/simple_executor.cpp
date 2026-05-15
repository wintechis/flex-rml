#include "simple_executor.h"

#include <algorithm>
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
#include "utils.h"
#include "xxhash.h"

namespace fs = std::filesystem;

constexpr std::size_t kOutputBufferReserve = 64 * 1024;

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

enum class CompiledTermMapType {
  Preformatted,
  Constant,
  Reference,
  Template
};

struct CompiledTemplatePart {
  std::string literal;
  int reference_index = -1;
};

struct CompiledTerm {
  bool usable = false;
  CompiledTermMapType map_type = CompiledTermMapType::Constant;
  std::string term_map;
  std::string term_type;
  std::string lang_tag = "None";
  std::string data_type = "None";
  std::vector<CompiledTemplatePart> parts;
  int reference_index = -1;
  bool infer_datatype = false;
  bool add_base_iri = false;
};

static int projected_column_index(const std::vector<std::string>& projected_header,
                                  std::string_view name) {
  for (std::size_t i = 0; i < projected_header.size(); ++i) {
    if (projected_header[i] == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static bool is_dynamic_annotation(const std::string& annotation,
                                  const std::vector<std::string>& projected_header) {
  return annotation.starts_with("==FUNC==") ||
         annotation.find('{') != std::string::npos ||
         projected_column_index(projected_header, annotation) >= 0;
}

static bool compile_template_parts(const std::string& term_map,
                                   const std::vector<std::string>& projected_header,
                                   std::vector<CompiledTemplatePart>& parts) {
  std::string literal;
  literal.reserve(term_map.size());

  for (std::size_t i = 0; i < term_map.size();) {
    if (term_map[i] == '\\' && i + 1 < term_map.size() &&
        (term_map[i + 1] == '{' || term_map[i + 1] == '}')) {
      literal.push_back(term_map[i + 1]);
      i += 2;
      continue;
    }

    if (term_map[i] != '{') {
      literal.push_back(term_map[i]);
      ++i;
      continue;
    }

    const std::size_t placeholder_start = i;
    ++i;
    std::string reference_name;
    bool closed = false;
    for (; i < term_map.size(); ++i) {
      if (term_map[i] == '\\' && i + 1 < term_map.size() &&
          (term_map[i + 1] == '{' || term_map[i + 1] == '}')) {
        reference_name.push_back(term_map[i + 1]);
        ++i;
      } else if (term_map[i] == '}') {
        closed = true;
        ++i;
        break;
      } else {
        reference_name.push_back(term_map[i]);
      }
    }

    if (!closed) {
      literal.append(term_map, placeholder_start, std::string::npos);
      break;
    }

    if (!literal.empty()) {
      parts.push_back(CompiledTemplatePart{std::move(literal), -1});
      literal.clear();
    }

    const int index = projected_column_index(projected_header, reference_name);
    if (index < 0) {
      return false;
    }
    parts.push_back(CompiledTemplatePart{"", index});
  }

  if (!literal.empty()) {
    parts.push_back(CompiledTemplatePart{std::move(literal), -1});
  }
  return true;
}

static CompiledTerm compile_term(const std::vector<std::string>& content,
                                 const std::vector<std::string>& projected_header,
                                 const std::string& base_uri) {
  CompiledTerm term;
  if (content.size() < 3 || content[1] == "function") {
    return term;
  }

  term.term_map = content[0];
  term.term_type = content[2];
  const bool is_literal = term.term_type == "literal";
  if (is_literal) {
    if (content.size() > 3) {
      term.lang_tag = content[3];
    }
    if (content.size() > 4) {
      term.data_type = content[4];
    }
    if (is_dynamic_annotation(term.lang_tag, projected_header) ||
        is_dynamic_annotation(term.data_type, projected_header)) {
      return term;
    }
    if (term.data_type != "None" &&
        !(term.data_type.starts_with("http://") || term.data_type.starts_with("https://"))) {
      term.data_type = base_uri + term.data_type;
    }
  }

  term.add_base_iri = term.term_type == "uri" || term.term_type == "iri" || term.term_type == "unsafeiri";

  if (content[1] == "preformatted") {
    term.map_type = CompiledTermMapType::Preformatted;
  } else if (content[1] == "constant") {
    term.map_type = CompiledTermMapType::Constant;
    term.infer_datatype = is_literal && term.lang_tag == "None" && term.data_type == "None";
  } else if (content[1] == "reference") {
    term.map_type = CompiledTermMapType::Reference;
    term.reference_index = projected_column_index(projected_header, term.term_map);
    if (term.reference_index < 0) {
      return CompiledTerm{};
    }
    term.infer_datatype = is_literal && term.lang_tag == "None" && term.data_type == "None";
  } else if (content[1] == "template") {
    term.map_type = CompiledTermMapType::Template;
    if (!compile_template_parts(term.term_map, projected_header, term.parts)) {
      return CompiledTerm{};
    }
  } else {
    return term;
  }

  term.usable = true;
  return term;
}

static bool term_needs_row_map(const std::vector<std::string>& content, const CompiledTerm& compiled) {
  return content.size() > 1 && (content[1] == "function" || !compiled.usable);
}

struct CompiledRuntimeTerm {
  std::vector<std::string> content;
  CompiledTerm compiled;
  enum class RenderOp {
    Null,
    Function,
    Preformatted,
    Compiled,
    Fallback
  };
  RenderOp render_op = RenderOp::Fallback;
};

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

std::vector<int> get_attribute_index(std::istream& file,
                                     const std::vector<std::string>& header,
                                     const std::vector<std::string>& projected_attributes);

static CompiledRuntimeTerm compile_runtime_term(const std::vector<std::string>& content,
                                                const std::vector<std::string>& projected_header,
                                                const std::string& base_uri) {
  CompiledRuntimeTerm term;
  term.content = content;
  term.compiled = compile_term(term.content, projected_header, base_uri);
  if (term.content.empty() || term.content[0] == "NULL") {
    term.render_op = CompiledRuntimeTerm::RenderOp::Null;
  } else if (term.content.size() > 1 && term.content[1] == "function") {
    term.render_op = CompiledRuntimeTerm::RenderOp::Function;
  } else if (term.content.size() > 1 && term.content[1] == "preformatted") {
    term.render_op = CompiledRuntimeTerm::RenderOp::Preformatted;
  } else if (term.compiled.usable) {
    term.render_op = CompiledRuntimeTerm::RenderOp::Compiled;
  } else {
    term.render_op = CompiledRuntimeTerm::RenderOp::Fallback;
  }
  return term;
}

static CompiledSimplePlan compile_simple_plan(const std::vector<std::string>& projected_header,
                                              const std::string& base_uri,
                                              const std::vector<std::string>& s_content,
                                              const std::vector<std::string>& p_content,
                                              const std::vector<std::string>& o_content,
                                              const std::vector<std::string>* g_content = nullptr) {
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

static bool prepare_simple_plan(const SimplePlan& source,
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

static void prepare_simple_plan_from_header(const SimplePlan& source,
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

static void render_compiled_term(const CompiledTerm& term,
                                 const std::vector<std::string_view>& projected_row,
                                 const std::string& base_uri,
                                 std::string& out,
                                 std::string& scratch) {
  if (term.map_type == CompiledTermMapType::Preformatted) {
    out = term.term_map;
    return;
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
        if (term.term_type == "uri") {
          append_safe_iri(value, true, scratch);
        } else if (term.term_type == "iri") {
          append_safe_iri(value, false, scratch);
        } else {
          scratch += value;
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

  const std::string datatype = term.infer_datatype ? infer_literal_datatype(rdf_term, term.lang_tag, term.data_type) : term.data_type;
  handle_term_type_into(term.term_type, rdf_term, term.lang_tag, datatype, out);
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

static bool has_invalid_iri_char(std::string_view value) {
  for (const char c : value) {
    switch (c) {
      case ' ':
      case '!':
      case '"':
      case '\'':
      case '(':
      case ')':
      case ',':
      case '[':
      case ']':
        return true;
      default:
        break;
    }
  }
  return false;
}

static void append_literal_term(std::string_view rdf_term,
                                const std::string& lang_tag,
                                const std::string& data_type,
                                bool infer_datatype,
                                std::string& out) {
  const std::string effective_data_type =
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
  if (!term.usable || term.term_type == "blanknode") {
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
        if (term.term_type == "uri") {
          append_safe_iri(value, true, scratch);
        } else if (term.term_type == "iri") {
          append_safe_iri(value, false, scratch);
        } else {
          scratch += value;
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

  if (term.term_type == "literal") {
    append_literal_term(rdf_term, term.lang_tag, term.data_type, term.infer_datatype, out);
    return true;
  }

  if (term.term_type == "uri" || term.term_type == "iri" || term.term_type == "unsafeiri") {
    if ((term.term_type == "uri" || term.term_type == "iri") && has_invalid_iri_char(rdf_term)) {
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
           term.compiled.term_type != "blanknode";
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

  std::string line;
  std::vector<std::string> split_line;
  std::vector<std::string_view> split_line_views;
  std::vector<std::string> projected_row;
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
  data.line.reserve(512);
  data.split_line.reserve(32);
  data.split_line_views.reserve(32);
  data.projected_row.reserve(32);
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

SetupData initialize_setup_dependent(const fs::path& output_file_name) {
  SetupData data;

  // Reserve memory for strings and vectors
  data.line.reserve(512);
  data.split_line.reserve(32);
  data.split_line_views.reserve(32);
  data.projected_row.reserve(32);
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

static void split_project_current_line(SetupData& setup_data,
                                       const std::vector<int>& projected_indices) {
  if (split_csv_line_views_into(setup_data.line, ',', setup_data.split_line_views)) {
    project_row_into(setup_data.split_line_views, projected_indices, setup_data.projected_row_views);
    return;
  }

  split_csv_line_into(setup_data.line, ',', setup_data.split_line);
  project_row_into(setup_data.split_line, projected_indices, setup_data.projected_row);
  project_row_views_from_strings(setup_data.projected_row, setup_data.projected_row_views);
}

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

struct FusedTermCacheEntry {
  std::string key;
  std::string value;
  std::string scratch;
  bool computed = false;
};

struct FusedSimpleProgram {
  std::vector<FusedSimpleRuntime> runtimes;
  std::vector<FusedSimpleGroup> groups;
  std::vector<FusedTermCacheEntry> term_cache_entries;
};

static bool can_cache_fused_term(const CompiledRuntimeTerm& term) {
  if (term.render_op == CompiledRuntimeTerm::RenderOp::Preformatted) {
    return true;
  }
  return term.render_op == CompiledRuntimeTerm::RenderOp::Compiled &&
         term.compiled.usable &&
         term.compiled.term_type != "blanknode";
}

static void append_key_part(std::string& key, std::string_view value) {
  key += std::to_string(value.size());
  key.push_back(':');
  key.append(value);
  key.push_back('|');
}

static std::string fused_term_cache_key(const std::string& base_uri,
                                        const CompiledRuntimeTerm& term) {
  std::string key;
  key.reserve(base_uri.size() + term.content.size() * 24 + 32);
  append_key_part(key, base_uri);
  for (const std::string& part : term.content) {
    append_key_part(key, part);
  }
  return key;
}

static void count_cacheable_fused_term(const std::string& base_uri,
                                       const CompiledRuntimeTerm& term,
                                       std::map<std::string, std::size_t>& counts) {
  if (can_cache_fused_term(term)) {
    counts[fused_term_cache_key(base_uri, term)]++;
  }
}

static int assign_cacheable_fused_term(const std::string& base_uri,
                                       const CompiledRuntimeTerm& term,
                                       const std::map<std::string, std::size_t>& counts,
                                       std::unordered_map<std::string, int>& ids,
                                       std::vector<FusedTermCacheEntry>& entries) {
  if (!can_cache_fused_term(term)) {
    return -1;
  }

  std::string key = fused_term_cache_key(base_uri, term);
  auto count_it = counts.find(key);
  if (count_it == counts.end() || count_it->second < 2) {
    return -1;
  }

  auto id_it = ids.find(key);
  if (id_it != ids.end()) {
    return id_it->second;
  }

  const int id = static_cast<int>(entries.size());
  FusedTermCacheEntry entry;
  entry.key = key;
  entry.value.reserve(512);
  entry.scratch.reserve(512);
  entries.push_back(std::move(entry));
  ids.emplace(std::move(key), id);
  return id;
}

static bool append_cached_fast_term(const CompiledRuntimeTerm& term,
                                    const std::vector<std::string_view>& projected_row,
                                    const std::string& base_uri,
                                    int cache_id,
                                    std::vector<FusedTermCacheEntry>& cache_entries,
                                    std::string& out,
                                    std::string& scratch) {
  if (cache_id < 0) {
    return append_fast_compiled_term(term, projected_row, base_uri, out, scratch);
  }

  FusedTermCacheEntry& entry = cache_entries[static_cast<std::size_t>(cache_id)];
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
                                       std::vector<FusedTermCacheEntry>& cache_entries,
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

static std::vector<FusedTermCacheEntry> compile_fused_term_cache(std::vector<FusedSimpleRuntime>& runtimes,
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
  std::vector<FusedTermCacheEntry> entries;
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

static void reset_fused_term_cache(FusedSimpleProgram& program) {
  for (FusedTermCacheEntry& entry : program.term_cache_entries) {
    entry.computed = false;
  }
}

static bool has_cached_fused_term(const FusedSimpleRuntime& runtime) {
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

static FusedSimpleProgram compile_fused_simple_program(const std::vector<SimplePlan>& plans,
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

static bool is_single_constant_statement(const SimplePlan& plan) {
  if (!plan.generate_graph) {
    return (plan.s_content[1] == "constant" && plan.p_content[1] == "constant" && plan.o_content[1] == "constant") ||
           (plan.s_content[1] == "preformatted" && plan.p_content[1] == "preformatted" && plan.o_content[1] == "preformatted");
  }
  return (plan.s_content[1] == "constant" && plan.p_content[1] == "constant" && plan.o_content[1] == "constant" && plan.g_content[1] == "constant") ||
         (plan.s_content[1] == "preformatted" && plan.p_content[1] == "preformatted" && plan.o_content[1] == "preformatted" && plan.g_content[1] == "preformatted");
}

///////////////////////////////////////////////////////////////7
/// HELPER FUNCTIONS
//////////////////////////////////////////////////////////////
std::vector<int> get_attribute_index(std::istream& file, const std::vector<std::string>& header, const std::vector<std::string>& projected_attributes) {
  // Get indices for projected attributes
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

// Open File or from map
static std::unique_ptr<std::istream> open_from_map_or_file(
    const std::unordered_map<std::string, std::string>& mem,
    const std::string& path) {
  if (auto it = mem.find(path); it != mem.end()) {
    return std::make_unique<std::istringstream>(it->second);
  }

  auto f = std::make_unique<std::ifstream>(path);
  if (!f->is_open()) {
    throw std::runtime_error("Could not open logical source: " + path);
  }
  return f;
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
  auto file = open_from_map_or_file(data_map, input_file_name);

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
  if (!prepare_simple_plan(source, *file, setup_data.line, prepared)) {
    return 0;
  }
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  int line_count = 0;
  // Iterate over file line by line
  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_project_current_line(setup_data, prepared.projected_indices);

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
  auto file = open_from_map_or_file(data_map, input_file_name);

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
  if (!prepare_simple_plan(source, *file, setup_data.line, prepared)) {
    return unique_triple;
  }
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  // Iterate over file line by line
  int line_count = 0;
  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_project_current_line(setup_data, prepared.projected_indices);

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
  auto file = open_from_map_or_file(data_map, input_file_name);

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
  if (!prepare_simple_plan(source, *file, setup_data.line, prepared)) {
    return 0;
  }
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  int line_count = 0;
  // Iterate over file line by line
  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_project_current_line(setup_data, prepared.projected_indices);

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
  auto file = open_from_map_or_file(data_map, input_file_name);

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
  if (!prepare_simple_plan(source, *file, setup_data.line, prepared)) {
    return unique_triple;
  }
  initialize_row_map(setup_data.row, prepared.projected_header);
  const CompiledSimplePlan& plan = prepared.compiled;

  // Iterate over file line by line
  int line_count = 0;

  // Check if funciton is needed
  bool function_called = plan.function_called;

  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_project_current_line(setup_data, prepared.projected_indices);

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

SimplePlan parse_simple_plan(const std::string& information) {
  SimplePlan data;

  // Extract relevant parts
  std::vector<std::string> split_info = split_by_substring(information, "\n");
  if (split_info.size() != 5) {
    std::cout << "Plan is too long for standalone simple mapping. Got size: " << split_info.size() << std::endl;
    std::exit(1);
  }

  data.output_file_name = split_info[2];
  data.base_uri = split_info[3];

  std::vector<std::string> split_info_first = split_by_substring(split_info[0], "|||");
  std::vector<std::string> split_info_second = split_by_substring(split_info[1], "|||");

  data.input_file_name = split_info_first[1];
  data.projected_attributes = split_by_substring(split_info_first[2], "===");

  data.s_content = split_by_substring(split_info_second[1], "===");
  data.p_content = split_by_substring(split_info_second[2], "===");
  data.o_content = split_by_substring(split_info_second[3], "===");

  if (split_info_second.size() == 4) {
    data.generate_graph = false;
  } else {
    data.generate_graph = true;
    data.g_content = split_by_substring(split_info_second[4], "===");
  }

  return data;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t standalone_simple_mapping(const std::string& information, const std::unordered_map<std::string, std::string>& data_map) {
  return execute_standalone_simple_plan(parse_simple_plan(information), data_map);
}

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
                                  OutputChunkWriter* writer) {
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
  auto file = open_from_map_or_file(data_map, input_file_name);

  std::string header_line;
  if (!std::getline(*file, header_line)) {
    return 0;
  }
  std::vector<std::string> header = split_csv_line(header_line, ',');

  FusedSimpleProgram program = compile_fused_simple_program(plans, header);
  std::vector<FusedSimpleRuntime>& runtimes = program.runtimes;
  const std::vector<FusedSimpleGroup>& groups = program.groups;
  std::vector<FusedTermCacheEntry>& term_cache_entries = program.term_cache_entries;

  int line_count = 0;
  while (std::getline(*file, setup_data.line)) {
    line_count++;
    reset_fused_term_cache(program);

    const bool use_views = split_csv_line_views_into(setup_data.line, ',', setup_data.split_line_views);
    if (!use_views) {
      split_csv_line_into(setup_data.line, ',', setup_data.split_line);
    }

    for (const auto& group : groups) {
      if (group.reuse_subject_predicate_object && group.runtime_indices.size() > 1) {
        FusedSimpleRuntime& representative = runtimes[group.runtime_indices.front()];
        const CompiledSimplePlan& representative_plan = representative.prepared.compiled;
        if (use_views) {
          project_row_into(setup_data.split_line_views, representative.prepared.projected_indices, representative.projected_row);
        } else {
          project_row_into(setup_data.split_line, representative.prepared.projected_indices, representative.projected_row_storage);
          project_row_views_from_strings(representative.projected_row_storage, representative.projected_row);
        }

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
        }
        continue;
      }

      FusedSimpleRuntime& runtime = runtimes[group.runtime_indices.front()];
      const CompiledSimplePlan& plan = runtime.prepared.compiled;
      if (use_views) {
        project_row_into(setup_data.split_line_views, runtime.prepared.projected_indices, runtime.projected_row);
      } else {
        project_row_into(setup_data.split_line, runtime.prepared.projected_indices, runtime.projected_row_storage);
        project_row_views_from_strings(runtime.projected_row_storage, runtime.projected_row);
      }

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
    }
  }

  flush_setup_buffer(setup_data);
  return setup_data.triple_counter;
}

std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map) {
  return execute_dependent_simple_plan(parse_simple_plan(information), unique_triple, data_map);
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
