#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Definitions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NTriple {
  std::string subject;
  std::string predicate;
  std::string object;

  bool operator==(const NTriple& other) const {
    return subject == other.subject &&
           predicate == other.predicate &&
           object == other.object;
  }
};

// used to store string result
static std::string g_result_str;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Transformation functions from string to vector and the other way round
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Function to split a single line into an NTriple
NTriple split_line(const std::string& line) {
  NTriple triple;
  size_t pos1 = line.find("|||");
  size_t pos2 = line.find("|||", pos1 + 3);

  if (pos1 != std::string::npos && pos2 != std::string::npos) {
    triple.subject = line.substr(0, pos1);
    triple.predicate = line.substr(pos1 + 3, pos2 - (pos1 + 3));
    triple.object = line.substr(pos2 + 3);
  }

  return triple;
}

// Function to process the entire RDF string
std::vector<NTriple> rdf_string_to_vector(const std::string& rdf_string) {
  std::vector<NTriple> triples;
  std::istringstream stream(rdf_string);
  std::string line;

  while (std::getline(stream, line)) {
    if (!line.empty()) {
      NTriple triple = split_line(line);
      // Optionally skip malformed lines (where predicate is empty etc.)
      if (!triple.subject.empty() || !triple.predicate.empty() || !triple.object.empty()) {
        triples.push_back(std::move(triple));
      }
    }
  }

  return triples;
}

std::vector<std::string> split_by_substring(const std::string& str, const std::string& delimiter) {
  std::vector<std::string> result;
  size_t start = 0;
  size_t end = str.find(delimiter);

  while (end != std::string::npos) {
    result.push_back(str.substr(start, end - start));
    start = end + delimiter.length();
    end = str.find(delimiter, start);
  }

  result.push_back(str.substr(start));
  return result;
}

static std::string graph_vector_to_string(const std::vector<NTriple>& triples) {
  std::string out;
  out.reserve(triples.size() * 64);
  for (const auto& t : triples) {
    out += t.subject;
    out += "|||";
    out += t.predicate;
    out += "|||";
    out += t.object;
    out += "\n";
  }
  return out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// RML Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static std::string get_local_now_iso8601() {
  using namespace std::chrono;
  using centiseconds = duration<long long, std::centi>;
  auto now = floor<centiseconds>(system_clock::now());
  zoned_time<centiseconds> zt{current_zone(), now};
  
  return std::format("{:%FT%T%Ez}", zt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Functions supported in RML
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern "C" {

const char* resolve_rml_functions(const char* input_rdf_mapping) {
  std::string rdf_rule_str(input_rdf_mapping ? input_rdf_mapping : "");

  // Split into graphs
  std::vector<std::string> rdf_graph_strings = split_by_substring(rdf_rule_str, "===");

  std::vector<std::string> new_normalized_graph_arr;
  new_normalized_graph_arr.reserve(rdf_graph_strings.size());

  constexpr const char* GREL_DATE_NOW = "http://users.ugent.be/~bjdmeest/function/grel.ttl#date_now";
  constexpr const char* RML_CONSTANT = "http://w3id.org/rml/constant";
  constexpr const char* RML_FUNCTION_EXECUTION = "http://w3id.org/rml/functionExecution";
  constexpr const char* RML_FUNCTION = "http://w3id.org/rml/function";
  constexpr const char* RML_RETURN = "http://w3id.org/rml/return";

  for (const auto& graph_str : rdf_graph_strings) {
    std::vector<NTriple> rdf_vector = rdf_string_to_vector(graph_str);

    // Map execution node -> function IRI
    std::unordered_map<std::string_view, std::string_view> function_by_exec_node;
    function_by_exec_node.reserve(rdf_vector.size());

    for (const auto& t : rdf_vector) {
      if (t.predicate == RML_FUNCTION) {
        // keep first if duplicates exist
        function_by_exec_node.emplace(std::string_view(t.subject), std::string_view(t.object));
      }
    }

    std::vector<char> drop(rdf_vector.size(), 0);
    std::vector<NTriple> new_triples;
    new_triples.reserve(4);

    for (size_t i = 0; i < rdf_vector.size(); ++i) {
      const auto& t = rdf_vector[i];
      if (t.predicate != RML_FUNCTION_EXECUTION) continue;

      const std::string& function_value_source_node = t.subject;  // e.g. objectMap node
      const std::string& execution_node = t.object;               // blank node with rml:function

      auto it = function_by_exec_node.find(std::string_view(execution_node));
      if (it == function_by_exec_node.end()) {
        std::cerr << "rml:functionExecution target has no rml:function: "
                  << execution_node << "\n";
        g_result_str.clear();
        return g_result_str.c_str();
      }

      std::string_view function_name = it->second;

      std::string value;
      if (function_name == GREL_DATE_NOW) {
        value = get_local_now_iso8601();
      } else {
        std::cerr << "Called function is not supported: " << function_name << "\n";
        g_result_str.clear();
        return g_result_str.c_str();
      }

      // Drop the rml:functionExecution triple from the source node
      drop[i] = 1;

      // Add rml:constant with the resolved value
      new_triples.push_back(NTriple{
          function_value_source_node,
          RML_CONSTANT,
          std::move(value)});

      // Drop triples hanging off the execution node, such as:
      //   execution_node rml:function ...
      // and optionally other metadata like rml:return if attached there
      for (size_t j = 0; j < rdf_vector.size(); ++j) {
        if (rdf_vector[j].subject == execution_node) {
          if (rdf_vector[j].predicate == RML_FUNCTION ||
              rdf_vector[j].predicate == RML_RETURN) {
            drop[j] = 1;
          }
        }
      }
    }

    if (new_triples.empty()) {
      new_normalized_graph_arr.push_back(graph_str);
    } else {
      std::vector<NTriple> new_graph;
      new_graph.reserve(rdf_vector.size() + new_triples.size());

      for (size_t i = 0; i < rdf_vector.size(); ++i) {
        if (!drop[i]) new_graph.push_back(rdf_vector[i]);
      }
      for (auto& nt : new_triples) new_graph.push_back(std::move(nt));

      new_normalized_graph_arr.push_back(graph_vector_to_string(new_graph));
    }
  }

  // Rebuild output with the same "===" separator
  g_result_str.clear();
  for (size_t i = 0; i < new_normalized_graph_arr.size(); ++i) {
    g_result_str += new_normalized_graph_arr[i];
    if (i + 1 < new_normalized_graph_arr.size()) {
      g_result_str += "===";
    }
  }

  return g_result_str.c_str();
}

}  // extern "C"


