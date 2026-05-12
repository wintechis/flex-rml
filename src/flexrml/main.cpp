#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_preprocessor.h"
#include "physical_plan.h"
#include "version.h"

std::string parse_rdf_string(const std::string& rdf_mapping);
std::string normalize_rml_mapping_string(const std::string& input_rdf_mapping, int bn_number);
std::string resolve_rml_functions_string(const std::string& input_rdf_mapping);
std::string create_relational_algebra_string(const std::string& rml_input);
std::string get_ra_sources_string(const std::string& ra_text);
std::string create_plan_partitions_string(const std::string& ra_text,
                                          const std::string& base_uris_text,
                                          const std::string& default_base_uri,
                                          const std::string& output_file_path,
                                          const std::string& continue_on_error,
                                          bool materialize_constants,
                                          bool heuristic_ordering,
                                          bool can_order);
std::vector<PlanPartition> create_plan_partitions(const std::string& ra_text,
                                                  const std::string& base_uris_text,
                                                  const std::string& default_base_uri,
                                                  const std::string& output_file_path,
                                                  const std::string& continue_on_error,
                                                  bool materialize_constants,
                                                  bool heuristic_ordering,
                                                  bool can_order);
std::string execute_physical_plans_string(const std::string& information,
                                          const std::string& mode,
                                          const std::string& continue_error,
                                          const std::string& output_file_path,
                                          bool keep_in_memory,
                                          const std::string& json_data);
std::string execute_physical_plan_partitions(const std::vector<PlanPartition>& partitions,
                                             const std::string& mode,
                                             const std::string& continue_error,
                                             const std::string& output_file_path,
                                             bool keep_in_memory,
                                             const std::string& json_data);

namespace {

constexpr const char* kBaseUri = "http://example.com/base/";
constexpr int kBlankNodeSeed = 58932;

struct Options {
  std::string mapping_source;
  std::string output_file_path;
  std::string base_uri = kBaseUri;
  std::string continue_on_error = "false";
  std::string threading_enabled = "true";
  std::string materialize_constants = "true";
  std::string heuristic_ordering = "true";
  bool generate_plan = false;
};

using IteratorConfig = std::map<std::string, std::string>;
using IteratorMap = std::map<std::string, IteratorConfig>;

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Could not open file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string read_mapping_source(const std::string& source) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(source, ec)) {
    return read_text_file(source);
  }
  return source;
}

std::vector<std::string> split(const std::string& value, const std::string& delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    std::size_t pos = value.find(delimiter, start);
    if (pos == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }
    parts.push_back(value.substr(start, pos - start));
    start = pos + delimiter.size();
  }
  return parts;
}

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string join(const std::vector<std::string>& values, const std::string& delimiter) {
  std::ostringstream output;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      output << delimiter;
    }
    output << values[i];
  }
  return output.str();
}

std::vector<std::string> non_empty_lines(const std::string& value) {
  std::vector<std::string> lines;
  std::istringstream input(value);
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::string> raw_non_empty_lines(const std::string& value) {
  std::vector<std::string> lines;
  std::istringstream input(value);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::string> get_object(const std::vector<std::string>& lines,
                                    const std::string& subject,
                                    const std::string& predicate) {
  std::vector<std::string> result;
  for (const auto& line : lines) {
    auto triple = split(line, "|||");
    if (triple.size() == 3 && triple[0] == subject && triple[1] == predicate) {
      result.push_back(triple[2]);
    }
  }
  return result;
}

std::vector<std::string> get_subject(const std::vector<std::string>& lines,
                                     const std::string& predicate,
                                     const std::string& object) {
  std::vector<std::string> result;
  for (const auto& line : lines) {
    auto triple = split(line, "|||");
    if (triple.size() == 3 && triple[1] == predicate && triple[2] == object) {
      result.push_back(triple[0]);
    }
  }
  return result;
}

std::vector<std::pair<std::string, std::string>> get_all_predicates_objects(
    const std::vector<std::string>& lines,
    const std::string& subject) {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto& line : lines) {
    auto triple = split(line, "|||");
    if (triple.size() == 3 && triple[0] == subject) {
      result.emplace_back(triple[1], triple[2]);
    }
  }
  return result;
}

void set_object(std::vector<std::string>& lines,
                const std::string& subject,
                const std::string& predicate,
                const std::string& object) {
  for (auto& line : lines) {
    auto triple = split(line, "|||");
    if (triple.size() == 3 && triple[0] == subject && triple[1] == predicate) {
      triple[2] = object;
      line = join(triple, "|||");
      return;
    }
  }
}

std::string new_bnode(const std::vector<std::string>& lines, const std::string& prefix = "b") {
  int max_id = 0;
  for (const auto& line : lines) {
    auto triple = split(line, "|||");
    if (triple.size() != 3) {
      continue;
    }
    for (const auto& node : {triple[0], triple[2]}) {
      if (node.rfind(prefix, 0) == 0) {
        const auto suffix = node.substr(prefix.size());
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit)) {
          max_id = std::max(max_id, std::stoi(suffix));
        }
      }
    }
  }
  return prefix + std::to_string(max_id + 1);
}

void replace_blank_subjectmap_with_function(std::vector<std::string>& lines) {
  const std::string rml = "http://w3id.org/rml/";
  const std::string p_subject_map = rml + "subjectMap";
  const std::string p_term_type = rml + "termType";
  const std::string o_blank_node = rml + "BlankNode";
  const std::string p_function_execution = rml + "functionExecution";
  const std::string p_function = rml + "function";
  const std::string p_input = rml + "input";
  const std::string p_input_value_map = rml + "inputValueMap";
  const std::string p_constant = rml + "constant";
  const std::string fn_generate_unique_iri = "https://w3id.org/imec/idlab/function#generateUniqueIRI";

  std::vector<std::string> subject_maps;
  for (const auto& line : lines) {
    auto triple = split(line, "|||");
    if (triple.size() == 3 && triple[1] == p_subject_map) {
      subject_maps.push_back(triple[2]);
    }
  }

  for (const auto& subject_map : subject_maps) {
    auto outgoing = get_all_predicates_objects(lines, subject_map);
    if (outgoing.size() != 1 || outgoing[0] != std::make_pair(p_term_type, o_blank_node)) {
      continue;
    }
    if (!get_object(lines, subject_map, p_function_execution).empty()) {
      continue;
    }

    const auto function_node = new_bnode(lines);
    const auto input_node = new_bnode(lines);
    const auto input_value_map_node = new_bnode(lines);
    lines.push_back(subject_map + "|||" + p_function_execution + "|||" + function_node);
    lines.push_back(function_node + "|||" + p_function + "|||" + fn_generate_unique_iri);
    lines.push_back(function_node + "|||" + p_input + "|||" + input_node);
    lines.push_back(input_node + "|||" + p_input_value_map + "|||" + input_value_map_node);
    lines.push_back(input_value_map_node + "|||" + p_constant + "|||dt3fav");
  }
}

void handle_unique_iri_function_constants(std::vector<std::string>& lines) {
  const std::string function_predicate = "http://w3id.org/rml/function";
  const std::string function_object = "https://w3id.org/imec/idlab/function#generateUniqueIRI";
  const std::string input_predicate = "http://w3id.org/rml/input";
  const std::string input_value_map_predicate = "http://w3id.org/rml/inputValueMap";
  const std::string constant_predicate = "http://w3id.org/rml/constant";

  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  int counter = 0;

  for (const auto& function_node : get_subject(lines, function_predicate, function_object)) {
    auto input_nodes = get_object(lines, function_node, input_predicate);
    if (input_nodes.empty()) {
      continue;
    }
    auto input_value_map_nodes = get_object(lines, input_nodes[0], input_value_map_predicate);
    if (input_value_map_nodes.empty()) {
      continue;
    }
    auto constants = get_object(lines, input_value_map_nodes[0], constant_predicate);
    if (constants.empty()) {
      continue;
    }
    auto uri = constants[0];
    if (!uri.empty() && uri.back() != '/') {
      uri += "/";
    }
    set_object(lines, input_value_map_nodes[0], constant_predicate,
               uri + std::to_string(timestamp_ms) + std::to_string(counter++));
  }
}

std::string patch_mapping_graph(const std::string& rml_string) {
  auto lines = raw_non_empty_lines(rml_string);
  replace_blank_subjectmap_with_function(lines);
  handle_unique_iri_function_constants(lines);
  return join(lines, "\n");
}

std::vector<std::string> normalize_mapping(const std::string& rml_string) {
  auto normalized = normalize_rml_mapping_string(rml_string, kBlankNodeSeed);

  std::vector<std::string> graphs;
  for (const auto& graph : split(normalized, "====")) {
    auto cleaned = trim(graph);
    if (!cleaned.empty()) {
      graphs.push_back(cleaned);
    }
  }
  std::sort(graphs.begin(), graphs.end());
  return graphs;
}

std::vector<std::string> handle_functions(const std::vector<std::string>& graphs) {
  const auto input = join(graphs, "===");
  auto resolved = resolve_rml_functions_string(input);
  if (trim(resolved).empty()) {
    throw std::runtime_error("Function resolution failed.");
  }
  return split(trim(resolved), "===");
}

IteratorMap get_iterators_for_graph(const std::string& graph) {
  IteratorMap iterators;
  auto lines = non_empty_lines(graph);
  for (const auto& line : lines) {
    if (line.find("|||http://w3id.org/rml/iterator|||") == std::string::npos) {
      continue;
    }
    auto triple = split(line, "|||");
    if (triple.size() != 3) {
      continue;
    }
    const auto logical_source_node = triple[0];
    const auto iterator = triple[2];
    std::string path;
    std::string reference_formulation;

    for (const auto& source_line : lines) {
      auto source_triple = split(source_line, "|||");
      if (source_triple.size() != 3 || source_triple[0] != logical_source_node) {
        continue;
      }
      if (source_triple[1] == "http://w3id.org/rml/referenceFormulation") {
        reference_formulation = source_triple[2];
      } else if (source_triple[1] == "http://w3id.org/rml/source") {
        const auto source_node = source_triple[2];
        for (const auto& path_line : lines) {
          auto path_triple = split(path_line, "|||");
          if (path_triple.size() == 3 && path_triple[0] == source_node &&
              path_triple[1] == "http://w3id.org/rml/path") {
            path = path_triple[2];
          }
        }
      }
    }

    if (!path.empty()) {
      iterators[path] = {
          {"iterator", iterator},
          {"reference_formulation", reference_formulation},
      };
    }
  }
  return iterators;
}

std::vector<IteratorMap> get_iterators(const std::vector<std::string>& graphs) {
  std::vector<IteratorMap> iterators;
  for (const auto& graph : graphs) {
    iterators.push_back(get_iterators_for_graph(graph));
  }
  return iterators;
}

std::vector<std::string> get_base_uris(const std::vector<std::string>& graphs,
                                       const std::string& default_base_uri) {
  std::vector<std::string> base_uris;
  for (const auto& graph : graphs) {
    std::string triples_map;
    std::string graph_base_uri = default_base_uri;
    auto lines = non_empty_lines(graph);
    for (const auto& line : lines) {
      auto triple = split(line, "|||");
      if (triple.size() == 3 && triple[1] == "http://www.w3.org/1999/02/22-rdf-syntax-ns#type" &&
          triple[2] == "http://w3id.org/rml/TriplesMap") {
        triples_map = triple[0];
        break;
      }
    }
    if (!triples_map.empty()) {
      for (const auto& line : lines) {
        auto triple = split(line, "|||");
        if (triple.size() == 3 && triple[0] == triples_map &&
            triple[1] == "http://w3id.org/rml/baseIRI") {
          graph_base_uri = triple[2];
          break;
        }
      }
    }
    base_uris.push_back(graph_base_uri);
  }
  return base_uris;
}

std::string normalize_jsonpath_references(std::string ra_string) {
  ra_string = std::regex_replace(ra_string, std::regex(R"(\$\['([^']+)'\])"), "$1");
  std::size_t pos = 0;
  while ((pos = ra_string.find("$.", pos)) != std::string::npos) {
    ra_string.replace(pos, 2, "");
  }
  return ra_string;
}

std::string python_quote(const std::string& value) {
  std::string escaped = "'";
  for (char ch : value) {
    if (ch == '\\' || ch == '\'') {
      escaped += '\\';
    }
    escaped += ch;
  }
  escaped += "'";
  return escaped;
}

std::string iterators_repr(const std::vector<IteratorMap>& iterators) {
  std::vector<std::string> graph_entries;
  for (const auto& graph_iterators : iterators) {
    std::vector<std::string> source_entries;
    for (const auto& [source, config] : graph_iterators) {
      std::vector<std::string> config_entries;
      for (const auto& [key, value] : config) {
        config_entries.push_back(python_quote(key) + ": " + python_quote(value));
      }
      source_entries.push_back(python_quote(source) + ": {" + join(config_entries, ", ") + "}");
    }
    graph_entries.push_back("{" + join(source_entries, ", ") + "}");
  }
  return "[" + join(graph_entries, ", ") + "]";
}

std::string list_repr(const std::vector<std::string>& values) {
  std::vector<std::string> quoted;
  for (const auto& value : values) {
    quoted.push_back(python_quote(value));
  }
  return "[" + join(quoted, ", ") + "]";
}

std::string generate_plan(const Options& options) {
  const auto raw_mapping = read_mapping_source(options.mapping_source);
  std::string parsed_mapping = parse_rdf_string(raw_mapping);
  if (parsed_mapping.rfind("Error:", 0) == 0) {
    throw std::runtime_error("RML parsing failed: " + parsed_mapping);
  }

  auto patched_mapping = patch_mapping_graph(parsed_mapping);
  auto normalized_graphs = normalize_mapping(patched_mapping);
  normalized_graphs = handle_functions(normalized_graphs);

  auto graph_iterators = get_iterators(normalized_graphs);
  auto graph_base_uris = get_base_uris(normalized_graphs, options.base_uri);
  std::vector<std::string> ra_expressions;
  std::vector<IteratorMap> ra_iterators;
  std::vector<std::string> ra_base_uris;
  for (std::size_t i = 0; i < normalized_graphs.size(); ++i) {
    const auto converted = create_relational_algebra_string(normalized_graphs[i]);
    for (const auto& line : non_empty_lines(converted)) {
      ra_expressions.push_back(line);
      ra_iterators.push_back(graph_iterators[i]);
      ra_base_uris.push_back(graph_base_uris[i]);
    }
  }
  auto ra_string = normalize_jsonpath_references(join(ra_expressions, "\n") + "\n");

  return ra_string + "<==>" + iterators_repr(ra_iterators) + "<==>" + list_repr(ra_base_uris);
}

struct RuntimePlan {
  std::string ra;
  std::vector<IteratorMap> iterators;
  std::vector<std::string> base_uris;
};

RuntimePlan build_runtime_plan(const Options& options) {
  const auto raw_mapping = read_mapping_source(options.mapping_source);
  std::string parsed_mapping = parse_rdf_string(raw_mapping);
  if (parsed_mapping.rfind("Error:", 0) == 0) {
    throw std::runtime_error("RML parsing failed: " + parsed_mapping);
  }

  auto patched_mapping = patch_mapping_graph(parsed_mapping);
  auto normalized_graphs = normalize_mapping(patched_mapping);
  normalized_graphs = handle_functions(normalized_graphs);

  auto graph_iterators = get_iterators(normalized_graphs);
  auto graph_base_uris = get_base_uris(normalized_graphs, options.base_uri);
  std::vector<std::string> ra_expressions;
  std::vector<IteratorMap> ra_iterators;
  std::vector<std::string> ra_base_uris;
  for (std::size_t i = 0; i < normalized_graphs.size(); ++i) {
    const auto converted = create_relational_algebra_string(normalized_graphs[i]);
    for (const auto& line : non_empty_lines(converted)) {
      ra_expressions.push_back(line);
      ra_iterators.push_back(graph_iterators[i]);
      ra_base_uris.push_back(graph_base_uris[i]);
    }
  }

  return {
      normalize_jsonpath_references(join(ra_expressions, "\n") + "\n"),
      ra_iterators,
      ra_base_uris,
  };
}

std::vector<std::vector<std::string>> get_ra_sources(const std::string& ra) {
  auto result = get_ra_sources_string(ra);

  std::vector<std::vector<std::string>> sources;
  for (const auto& line : non_empty_lines(result)) {
    sources.push_back(split(line, "|||"));
  }
  return sources;
}

std::string encode_in_memory_entry(const std::string& source, const std::string& csv_data) {
  return "|||===|||" + source + "===|||===" + csv_data;
}

std::string preprocess_runtime_inputs(const RuntimePlan& plan) {
  auto sources = get_ra_sources(plan.ra);
  std::string in_memory_data;
  std::vector<std::string> loaded_sources;

  for (std::size_t i = 0; i < sources.size(); ++i) {
    const IteratorMap empty_iterators;
    const auto& iterators = i < plan.iterators.size() ? plan.iterators[i] : empty_iterators;

    for (const auto& source : sources[i]) {
      if (std::find(loaded_sources.begin(), loaded_sources.end(), source) != loaded_sources.end()) {
        continue;
      }

      const auto iterator = iterators.find(source);
      if (iterator == iterators.end()) {
        continue;
      }

      const auto reference_formulation = iterator->second.find("reference_formulation");
      const auto iterator_value = iterator->second.find("iterator");
      if (iterator_value == iterator->second.end()) {
        throw std::runtime_error("Missing iterator for source: " + source);
      }
      if (reference_formulation != iterator->second.end() &&
          reference_formulation->second == "http://w3id.org/rml/XPath") {
        throw std::runtime_error("XML/XPath sources are not supported: " + source);
      }
      if (reference_formulation != iterator->second.end() &&
          reference_formulation->second != "http://w3id.org/rml/JSONPath") {
        throw std::runtime_error("Unsupported reference formulation for source '" + source +
                                 "': " + reference_formulation->second);
      }

      in_memory_data += encode_in_memory_entry(source, preprocess_json_to_csv(source, iterator_value->second));
      loaded_sources.push_back(source);
    }
  }
  return in_memory_data;
}

std::string execute_mapping(const Options& options) {
  auto plan = build_runtime_plan(options);
  auto in_memory_data = preprocess_runtime_inputs(plan);
  const bool keep_in_memory = options.output_file_path.empty();
  const auto plan_partitions = create_plan_partitions(
      plan.ra,
      join(plan.base_uris, "\n"),
      options.base_uri,
      options.output_file_path,
      options.continue_on_error,
      options.materialize_constants == "true",
      options.heuristic_ordering == "true",
      in_memory_data.empty());

  const auto execution_result = execute_physical_plan_partitions(
      plan_partitions,
      options.threading_enabled,
      options.continue_on_error,
      options.output_file_path,
      keep_in_memory,
      in_memory_data);
  const auto sep = execution_result.find("|||");
  if (sep == std::string::npos) {
    throw std::runtime_error("Executor returned malformed result.");
  }
  return keep_in_memory ? trim(execution_result.substr(sep + 3)) : "";
}

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto require_value = [&](const std::string& flag) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + flag);
      }
      return argv[++i];
    };

    if (arg == "-m" || arg == "--mapping") {
      options.mapping_source = require_value(arg);
    } else if (arg == "-o" || arg == "--output") {
      options.output_file_path = require_value(arg);
    } else if (arg == "-b" || arg == "--base") {
      options.base_uri = require_value(arg);
    } else if (arg == "-gp" || arg == "--generate-plan") {
      options.generate_plan = true;
    } else if (arg == "--continue-on-error") {
      options.continue_on_error = "true";
    } else if (arg == "--no-threading") {
      options.threading_enabled = "false";
    } else if (arg == "--no-const-folding") {
      options.materialize_constants = "false";
    } else if (arg == "--no-ordering") {
      options.heuristic_ordering = "false";
    } else if (arg == "--version") {
      std::cout << "flexrml " << FLEXRML_VERSION << "\n";
      std::exit(0);
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "usage: flexrml -m MAPPING [-o OUTPUT] [-b BASE]\n"
                   "       flexrml -m MAPPING [-b BASE] -gp\n"
                   "       flexrml --version\n";
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }
  if (options.mapping_source.empty()) {
    throw std::runtime_error("No mapping specified. Use -m MAPPING.");
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto options = parse_args(argc, argv);
    if (options.generate_plan) {
      std::cout << generate_plan(options) << "\n";
      return 0;
    }
    auto output = execute_mapping(options);
    if (!output.empty()) {
      std::cout << output << "\n";
    }
    return 0;
  } catch (const std::exception& exc) {
    std::cerr << "Error: " << exc.what() << "\n";
    return 1;
  }
}
