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
const bool SHOW_DEBUG = false;

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

struct FunctionInput {
  std::string parameter;  // e.g. ...#valueParam
  std::string kind;       // "reference", "constant", "template"
  std::string value;      // e.g. Name
};

struct ParsedFunctionExecution {
  std::string source_node;             // e.g. objectMap node (b5)
  std::string execution_node;          // e.g. http://example.com/base/#Execution
  std::string function_iri;            // original function IRI
  std::string internal_function_name;  // e.g. ==FUNC==TO_UPPER_CASE
  std::vector<FunctionInput> inputs;   // extracted inputs
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
/////// Helper functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_supported_function(std::string_view function_name, std::string& internal_name_out) {
  constexpr const char* IDLAB_RANDOM_FUNCTION = "https://w3id.org/imec/idlab/function#random";
  constexpr const char* IDLAB_ALWAYS_RETURNS_ABC = "https://w3id.org/imec/idlab/function#alwaysReturnsABC";
  constexpr const char* IDLAB_TO_UPPER_CASE_URL = "https://w3id.org/imec/idlab/function#toUpperCaseURL";
  constexpr const char* GREL_DATE_NOW = "http://users.ugent.be/~bjdmeest/function/grel.ttl#date_now";
  constexpr const char* GREL_TO_UPPER_CASE = "http://users.ugent.be/~bjdmeest/function/grel.ttl#toUpperCase";
  constexpr const char* GREL_STRING_LENGTH = "http://users.ugent.be/~bjdmeest/function/grel.ttl#string_length";
  constexpr const char* GREL_STRING_SUBSTRING = "http://users.ugent.be/~bjdmeest/function/grel.ttl#string_substring";
  constexpr const char* IDLAB_GENERATE_UNIQUE_IRI = "https://w3id.org/imec/idlab/function#generateUniqueIRI";

  if (function_name == GREL_DATE_NOW) {
    internal_name_out = "==FUNC==DATE_NOW";
    return true;
  } else if (function_name == IDLAB_RANDOM_FUNCTION) {
    internal_name_out = "==FUNC==RANDOM";
    return true;
  } else if (function_name == IDLAB_ALWAYS_RETURNS_ABC) {
    internal_name_out = "==FUNC==ALWAYS_RETURNS_ABC";
    return true;
  } else if (function_name == IDLAB_TO_UPPER_CASE_URL) {
    internal_name_out = "==FUNC==TO_UPPER_CASE";
    return true;
  } else if (function_name == GREL_TO_UPPER_CASE) {
    internal_name_out = "==FUNC==TO_UPPER_CASE";
    return true;
  } else if (function_name == GREL_STRING_LENGTH) {
    internal_name_out = "==FUNC==STRING_LENGTH";
    return true;
  } else if (function_name == GREL_STRING_SUBSTRING) {
    internal_name_out = "==FUNC==STRING_SUBSTRING";
    return true;
  } else if (function_name == IDLAB_GENERATE_UNIQUE_IRI) {
    internal_name_out = "==FUNC==GENERATE_IRI";
    return true;
  }

  return false;
}

static void debug_print_parsed_function(const ParsedFunctionExecution& pf) {
  std::cout << "Parsed function execution:\n";
  std::cout << "  source_node: " << pf.source_node << "\n";
  std::cout << "  execution_node: " << pf.execution_node << "\n";
  std::cout << "  function_iri: " << pf.function_iri << "\n";
  std::cout << "  internal_function_name: " << pf.internal_function_name << "\n";
  for (const auto& in : pf.inputs) {
    std::cout << "  input:\n";
    std::cout << "    parameter: " << in.parameter << "\n";
    std::cout << "    kind: " << in.kind << "\n";
    std::cout << "    value: " << in.value << "\n";
  }
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

  // RML / internal predicates
  constexpr const char* INTERNAL_FUNCTION_PREDICATE = "function";

  constexpr const char* RML_FUNCTION_EXECUTION = "http://w3id.org/rml/functionExecution";
  constexpr const char* RML_FUNCTION = "http://w3id.org/rml/function";
  constexpr const char* RML_RETURN = "http://w3id.org/rml/return";
  constexpr const char* RML_INPUT = "http://w3id.org/rml/input";
  constexpr const char* RML_PARAMETER = "http://w3id.org/rml/parameter";
  constexpr const char* RML_INPUT_VALUE_MAP = "http://w3id.org/rml/inputValueMap";
  constexpr const char* RML_INPUT_VALUE = "http://w3id.org/rml/inputValue";

  constexpr const char* RML_REFERENCE = "http://w3id.org/rml/reference";
  constexpr const char* RML_CONSTANT = "http://w3id.org/rml/constant";
  constexpr const char* RML_TEMPLATE = "http://w3id.org/rml/template";

  for (const auto& graph_str : rdf_graph_strings) {
    std::vector<NTriple> rdf_vector = rdf_string_to_vector(graph_str);

    // Lookup maps
    std::unordered_map<std::string_view, std::string_view> function_by_exec_node;
    std::unordered_map<std::string_view, std::vector<std::string_view>> inputs_by_exec_node;
    std::unordered_map<std::string_view, std::string_view> parameter_by_input_node;
    std::unordered_map<std::string_view, std::string_view> value_map_by_input_node;
    std::unordered_map<std::string_view, std::string_view> input_value_by_input_node;
    std::unordered_map<std::string_view, std::string_view> reference_by_value_map;
    std::unordered_map<std::string_view, std::string_view> constant_by_value_map;
    std::unordered_map<std::string_view, std::string_view> template_by_value_map;

    function_by_exec_node.reserve(rdf_vector.size());
    inputs_by_exec_node.reserve(rdf_vector.size());
    parameter_by_input_node.reserve(rdf_vector.size());
    value_map_by_input_node.reserve(rdf_vector.size());
    input_value_by_input_node.reserve(rdf_vector.size());
    reference_by_value_map.reserve(rdf_vector.size());
    constant_by_value_map.reserve(rdf_vector.size());
    template_by_value_map.reserve(rdf_vector.size());

    for (const auto& t : rdf_vector) {
      std::string_view subj(t.subject);
      std::string_view pred(t.predicate);
      std::string_view obj(t.object);

      if (pred == RML_FUNCTION) {
        function_by_exec_node.emplace(subj, obj);
      } else if (pred == RML_INPUT) {
        inputs_by_exec_node[subj].push_back(obj);
      } else if (pred == RML_PARAMETER) {
        parameter_by_input_node.emplace(subj, obj);
      } else if (pred == RML_INPUT_VALUE_MAP) {
        value_map_by_input_node.emplace(subj, obj);
      } else if (pred == RML_INPUT_VALUE) {
        input_value_by_input_node.emplace(subj, obj);
      } else if (pred == RML_REFERENCE) {
        reference_by_value_map.emplace(subj, obj);
      } else if (pred == RML_CONSTANT) {
        constant_by_value_map.emplace(subj, obj);
      } else if (pred == RML_TEMPLATE) {
        template_by_value_map.emplace(subj, obj);
      }
    }

    std::vector<char> drop(rdf_vector.size(), 0);
    std::vector<NTriple> new_triples;
    std::vector<ParsedFunctionExecution> parsed_functions;

    new_triples.reserve(8);
    parsed_functions.reserve(4);

    for (size_t i = 0; i < rdf_vector.size(); ++i) {
      const auto& t = rdf_vector[i];
      if (t.predicate != RML_FUNCTION_EXECUTION) {
        continue;
      }

      const std::string& function_value_source_node = t.subject;  // e.g. objectMap node
      const std::string& execution_node = t.object;               // execution resource / blank node

      auto fit = function_by_exec_node.find(std::string_view(execution_node));
      if (fit == function_by_exec_node.end()) {
        std::cerr << "rml:functionExecution target has no rml:function: "
                  << execution_node << "\n";
        g_result_str.clear();
        return g_result_str.c_str();
      }

      std::string internal_function_name;
      if (!is_supported_function(fit->second, internal_function_name)) {
        std::cerr << "Called function is not supported: " << fit->second << "\n";
        g_result_str.clear();
        return g_result_str.c_str();
      }

      ParsedFunctionExecution parsed;
      parsed.source_node = function_value_source_node;
      parsed.execution_node = execution_node;
      parsed.function_iri = std::string(fit->second);
      parsed.internal_function_name = internal_function_name;

      // Extract inputs
      auto inputs_it = inputs_by_exec_node.find(std::string_view(execution_node));
      if (inputs_it != inputs_by_exec_node.end()) {
        for (std::string_view input_node : inputs_it->second) {
          FunctionInput fi;

          auto pit = parameter_by_input_node.find(input_node);
          if (pit != parameter_by_input_node.end()) {
            fi.parameter = std::string(pit->second);
          }

          auto vmit = value_map_by_input_node.find(input_node);
          if (vmit == value_map_by_input_node.end()) {
            auto ivit = input_value_by_input_node.find(input_node);
            if (ivit != input_value_by_input_node.end()) {
              fi.kind = "constant";
              fi.value = std::string(ivit->second);
              parsed.inputs.push_back(std::move(fi));
              continue;
            }

            std::cerr << "rml:input node has no rml:inputValueMap or rml:inputValue: " << input_node << "\n";
            g_result_str.clear();
            return g_result_str.c_str();
          }

          std::string_view value_map_node = vmit->second;

          auto rit = reference_by_value_map.find(value_map_node);
          auto cit = constant_by_value_map.find(value_map_node);
          auto tit = template_by_value_map.find(value_map_node);

          if (rit != reference_by_value_map.end()) {
            fi.kind = "reference";
            fi.value = std::string(rit->second);
          } else if (cit != constant_by_value_map.end()) {
            fi.kind = "constant";
            fi.value = std::string(cit->second);
          } else if (tit != template_by_value_map.end()) {
            fi.kind = "template";
            fi.value = std::string(tit->second);
          } else {
            std::cerr << "inputValueMap has no supported value: " << value_map_node << "\n";
            g_result_str.clear();
            return g_result_str.c_str();
          }

          parsed.inputs.push_back(std::move(fi));
        }
      }

      parsed_functions.push_back(parsed);

      // Replace:
      //   source_node rml:functionExecution execution_node
      // with:
      //   source_node "function" ==FUNC==...
      drop[i] = 1;

      std::string encoded_function = internal_function_name;

      for (const auto& in : parsed.inputs) {
        encoded_function += ";;";
        encoded_function += in.parameter;
        encoded_function += ";;";
        encoded_function += in.kind;
        encoded_function += ";;";
        encoded_function += in.value;
      }

      encoded_function;

      new_triples.push_back(NTriple{
          function_value_source_node,
          INTERNAL_FUNCTION_PREDICATE,
          std::move(encoded_function)});

      // Drop whole subtree hanging off the execution node:
      //   execution_node rml:function ...
      //   execution_node rml:return ...
      //   execution_node rml:input inputNode
      //   inputNode rml:parameter ...
      //   inputNode rml:inputValueMap valueMapNode
      //   valueMapNode rml:reference|constant|template ...
      std::vector<std::string_view> input_nodes_to_drop;
      std::vector<std::string_view> value_map_nodes_to_drop;

      auto exec_inputs_it = inputs_by_exec_node.find(std::string_view(execution_node));
      if (exec_inputs_it != inputs_by_exec_node.end()) {
        input_nodes_to_drop = exec_inputs_it->second;
        value_map_nodes_to_drop.reserve(input_nodes_to_drop.size());

        for (std::string_view input_node : input_nodes_to_drop) {
          auto vm_it = value_map_by_input_node.find(input_node);
          if (vm_it != value_map_by_input_node.end()) {
            value_map_nodes_to_drop.push_back(vm_it->second);
          }
        }
      }

      for (size_t j = 0; j < rdf_vector.size(); ++j) {
        const auto& tj = rdf_vector[j];

        if (tj.subject == execution_node) {
          if (tj.predicate == RML_FUNCTION ||
              tj.predicate == RML_RETURN ||
              tj.predicate == RML_INPUT) {
            drop[j] = 1;
            continue;
          }
        }

        for (std::string_view input_node : input_nodes_to_drop) {
          if (tj.subject == input_node) {
            if (tj.predicate == RML_PARAMETER ||
              tj.predicate == RML_INPUT_VALUE_MAP) {
            drop[j] = 1;
          }
          if (tj.predicate == RML_INPUT_VALUE) {
            drop[j] = 1;
          }
        }
        }

        for (std::string_view value_map_node : value_map_nodes_to_drop) {
          if (tj.subject == value_map_node) {
            if (tj.predicate == RML_REFERENCE ||
                tj.predicate == RML_CONSTANT ||
                tj.predicate == RML_TEMPLATE) {
              drop[j] = 1;
            }
          }
        }
      }
    }

    // Optional debug output
    if (SHOW_DEBUG) {
      for (const auto& pf : parsed_functions) {
        debug_print_parsed_function(pf);
      }
    }

    if (new_triples.empty()) {
      new_normalized_graph_arr.push_back(graph_str);
    } else {
      std::vector<NTriple> new_graph;
      new_graph.reserve(rdf_vector.size() + new_triples.size());

      for (size_t i = 0; i < rdf_vector.size(); ++i) {
        if (!drop[i]) {
          new_graph.push_back(rdf_vector[i]);
        }
      }

      for (auto& nt : new_triples) {
        new_graph.push_back(std::move(nt));
      }

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
