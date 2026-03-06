#include <algorithm>
#include <array>
#include <format>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Struct Definitions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// used to store string result
static std::string g_result_str;

struct NTriple {
  std::string subject;
  std::string predicate;
  std::string object;
};

struct Subject {
  std::string term_map_type;  // template, constant, reference
  std::string term_type;      // iri, blanknode, literal
  std::string term_map;       // contains value
};

struct Predicate {
  std::string term_map_type;  // template, constant, reference
  std::string term_type;      // iri, blanknode, literal
  std::string term_map;
};

struct Object {
  std::string term_map_type;  // template, constant, reference
  std::string term_type;      // iri, blanknode, literal
  std::string term_map;
  std::string lang_tag = "None";
  std::string data_type = "None";
  std::string join_type = "None";
  std::array<std::string, 2> join_condition = {"", ""};
};

struct Graph {
  std::string term_map_type;  // template, constant, reference
  std::string term_type;      // iri, blanknode, literal
  std::string term_map;       // contains value
};

std::unordered_set<std::string> valid_language_subtags = {
    "en",  // English
    "es",  // Spanish
    "fr",  // French
    "de",  // German
    "zh",  // Chinese
    "it",  // Italian
    "ja",  // Japanese
    "ko",  // Korean
    "no",  // Norwegian
    "pt",  // Portuguese
    "ru",  // Russian
    "ar",  // Arabic
    "cs",  // Czech
    "da",  // Danish
    "nl",  // Dutch
    "fi",  // Finnish
    "el",  // Modern Greek
    "hi",  // Hindi
    "hu",  // Hungarian
    "ro",  // Romanian
};

// Global unordered_set to track generated strings
std::unordered_set<std::string> generated_strings;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// General Helper Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Helper function to determine if a character at position `i` is escaped
bool isEscaped(const std::string &line, size_t i) {
  if (i == 0) return false;
  size_t backslash_count = 0;
  // Count the number of consecutive backslashes before the current character
  size_t j = i - 1;
  while (j < line.size() && line[j] == '\\') {
    backslash_count++;
    if (j == 0) break;
    j--;
  }
  // If the count is odd, the character is escaped
  return backslash_count % 2 != 0;
}

// Split input at given char
std::vector<std::string> split(std::string_view text, char delim) {
  std::vector<std::string> out;

  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t pos = text.find(delim, start);
    if (pos == std::string_view::npos) pos = text.size();  // last field
    out.emplace_back(text.substr(start, pos - start));
    start = pos + 1;  // skip delim
  }
  return out;
}

// Check for lang tag annotation, But only if no lang tag is given by languageMap
std::tuple<std::string, std::string> check_annotated_lang_tag(const std::string &rdf_term_map, const std::string &current_lang_tag) {
  if (current_lang_tag == "None") {
    // Split vector
    std::vector<std::string> split_rdf_term_map = split(rdf_term_map, '@');
    if (!split_rdf_term_map.empty()) {
      // Get last element if not empty
      std::string potential_lang_tag = split_rdf_term_map.back();
      // check if lang tag is valid
      if (valid_language_subtags.find(potential_lang_tag) != valid_language_subtags.end()) {
        // If valid build new rdf_term_map
        std::string new_rdf_term_map = "";
        for (int i = 0; i < split_rdf_term_map.size() - 1; i++) {
          new_rdf_term_map += split_rdf_term_map[i];
        }
        return std::make_tuple(new_rdf_term_map, potential_lang_tag);
      }
    }
  }
  return std::make_tuple(rdf_term_map, current_lang_tag);
}

std::string generate_random_string(size_t length) {
  const std::string characters =
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789";
  std::random_device rd;         // Seed for randomness
  std::mt19937 generator(rd());  // Random number generator
  std::uniform_int_distribution<> distribution(0, characters.size() - 1);

  std::string random_string;
  do {
    random_string.clear();  // Clear the string for regeneration
    for (size_t i = 0; i < length; ++i) {
      random_string += characters[distribution(generator)];
    }
  } while (generated_strings.find(random_string) != generated_strings.end());

  // Add the newly generated unique string to the set
  generated_strings.insert(random_string);

  return random_string;
}

std::vector<std::string> extract_substrings(const std::string &str) {
  std::vector<std::string> substrings;

  size_t startPos = 0, endPos = 0;

  while ((startPos = str.find('{', startPos)) != std::string::npos) {
    if (startPos == 0 || str[startPos - 1] != '\\') {
      endPos = str.find('}', startPos);
      if (endPos != std::string::npos) {
        substrings.emplace_back(str, startPos + 1, endPos - startPos - 1);
        startPos = endPos + 1;
      } else {
        break;  // no matching closing brace found
      }
    } else {
      startPos++;
    }
  }
  return substrings;
}

std::vector<std::string> find_matching_subjects(
    const std::vector<NTriple> &triples, const std::string &predicate,
    const std::string &object) {
  std::vector<std::string> results;

  for (const auto &triple : triples) {
    // Check predicate
    if (triple.predicate != predicate) {
      continue;  // skip this triple if predicates don't match
    }

    // Check object if not empty
    if (!object.empty() && triple.object != object) {
      continue;  // skip this triple if objects don't match
    }

    // If we reach here, predicate matched (and object, if provided)
    results.push_back(triple.subject);
  }

  return results;  // returns the vector of matching subjects
}

std::vector<std::string> find_matching_objects(
    const std::vector<NTriple> &triples, const std::string &subject,
    const std::string &predicate) {
  std::vector<std::string> results;

  for (const NTriple &triple : triples) {
    // Check subject if not empty
    if (!subject.empty() && triple.subject != subject) {
      continue;  // skip this triple if subjects don't match
    }

    // Check predicate if not empty
    if (!predicate.empty() && triple.predicate != predicate) {
      continue;  // skip this triple if predicates don't match
    }

    // If we reach here, both subject and predicate matched
    results.push_back(triple.object);
  }

  return results;  // returns the vector of matching objects
}

int get_number_suject_maps(const std::vector<NTriple> &triples) {
  int subject_map_counter = 0;

  for (const auto &triple : triples) {
    if (triple.predicate == "http://www.w3.org/ns/r2rml#subjectMap") {
      subject_map_counter++;
    }
  }

  return subject_map_counter;
}

std::string get_root_tm(const std::vector<NTriple> &triples) {
  // Get triple maps
  std::vector<std::string> tms = find_matching_subjects(
      triples, "http://w3id.org/rml/subjectMap", "");

  // Ensure tms is not empty
  if (tms.empty()) {
    throw std::runtime_error("No triple maps found.");
  }

  // Check if node is root (has predicateObjectMap)
  size_t i = 0;
  for (; i < tms.size(); i++) {
    std::vector<std::string> res = find_matching_objects(
        triples, tms[i], "http://w3id.org/rml/predicateObjectMap");
    if (res.size() == 1) {
      break;
    }
  }
  // Handle case where no valid root subject node is found
  if (i >= tms.size()) {
    throw std::runtime_error(
        "No root subject node with predicateObjectMap found.");
  }

  return tms[i];
}

std::string get_predicate_object_map(const std::vector<NTriple> &triples,
                                     const std::string &root_tm) {
  std::vector<std::string> res = find_matching_objects(
      triples, root_tm, "http://w3id.org/rml/predicateObjectMap");

  if (res.size() != 1) {
    throw std::runtime_error("No predicateObjectMap found.");
  }

  return res[0];
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Functions to extract information and fill structs
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<Graph> get_graph(const std::vector<NTriple> &triples,
                             const std::string &root_tm,
                             const std::string &pom) {
  std::vector<Graph> graphs;

  // Get subject nodes
  std::vector<std::string> subject_nodes = find_matching_objects(
      triples, root_tm, "http://w3id.org/rml/subjectMap");
  std::string subject_node = subject_nodes[0];

  // Check if graph is available at subject
  std::vector<std::string> graph_nodes = find_matching_objects(
      triples, subject_node, "http://w3id.org/rml/graphMap");

  // Initialize Graph result with default values
  Graph result;
  result.term_map_type = "";
  result.term_type = "iri";
  result.term_map = "";

  if (graph_nodes.size() != 1) {
    graphs.push_back(result);
    return graphs;
  }

  std::string graph_node = graph_nodes[0];

  // Check if constant
  std::vector<std::string> results = find_matching_objects(
      triples, graph_node, "http://w3id.org/rml/constant");
  if (results.size() == 1) {
    result.term_map_type = "constant";
    if (results[0] != "http://w3id.org/rml/defaultGraph") {
      result.term_map = results[0];
    }
    graphs.push_back(result);
  }

  // Check if reference
  results = find_matching_objects(triples, graph_node, "http://w3id.org/rml/reference");
  if (results.size() == 1) {
    result.term_map_type = "reference";
    if (results[0] != "http://www.w3.org/ns/r2rml#defaultGraph") {
      result.term_map = results[0];
    }
    graphs.push_back(result);
  }

  // Check if template
  results = find_matching_objects(triples, graph_node, "http://w3id.org/rml/template");
  if (results.size() == 1) {
    result.term_map_type = "template";
    if (results[0] != "http://www.w3.org/ns/r2rml#defaultGraph") {
      result.term_map = results[0];
    }
    graphs.push_back(result);
  }

  // Check if graph is available at object
  std::vector<std::string> pom_graph_nodes = find_matching_objects(
      triples, pom, "http://w3id.org/rml/graphMap");
  if (pom_graph_nodes.size() != 1) {
    return graphs;
  }

  std::string pom_graph_node = pom_graph_nodes[0];

  // Initialize Graph result with default values
  Graph result2;
  result2.term_map_type = "";
  result2.term_type = "iri";
  result2.term_map = "";

  // Check if constant
  results = find_matching_objects(triples, pom_graph_node, "http://w3id.org/rml/constant");
  if (results.size() == 1) {
    result2.term_map_type = "constant";
    if (results[0] != "http://www.w3.org/ns/r2rml#defaultGraph") {
      result2.term_map = results[0];
    }
    graphs.push_back(result2);
  }

  // Check if reference
  results = find_matching_objects(triples, pom_graph_node, "http://semweb.mmlab.be/ns/rml#reference");
  if (results.size() == 1) {
    result2.term_map_type = "reference";
    if (results[0] != "http://www.w3.org/ns/r2rml#defaultGraph") {
      result2.term_map = results[0];
    }
    graphs.push_back(result2);
  }

  // Check if template
  results = find_matching_objects(triples, pom_graph_node, "http://w3id.org/rml/template");
  if (results.size() == 1) {
    result2.term_map_type = "template";
    if (results[0] != "http://www.w3.org/ns/r2rml#defaultGraph") {
      result2.term_map = results[0];
    }
    graphs.push_back(result2);
  }

  return graphs;
}

Subject get_subject(const std::vector<NTriple> &triples,
                    const std::string &root_tm) {
  // Get subject nodes
  std::vector<std::string> subject_nodes = find_matching_objects(
      triples, root_tm, "http://w3id.org/rml/subjectMap");
  std::string subject_node = subject_nodes[0];

  // Initialize Subject result with default values
  Subject result;
  result.term_map_type = "";
  result.term_type = "iri";
  result.term_map = "";

  // check if term typ is given
  std::vector<std::string> results = find_matching_objects(triples, subject_node, "http://w3id.org/rml/termType");
  if (results.size() == 1) {
    std::string new_term_type = results[0];
    if (new_term_type == "http://w3id.org/rml/BlankNode") {
      result.term_type = "blanknode";
    } else if (new_term_type == "http://w3id.org/rml/Literal") {
      std::cout << "Literal not supported!" << std::endl;
      std::exit(1);
    }
  }

  // Check if constant
  results = find_matching_objects(triples, subject_node, "http://w3id.org/rml/constant");
  if (results.size() == 1) {
    result.term_map_type = "constant";
    result.term_map = results[0];
    return result;
  }

  // Check if reference
  results = find_matching_objects(triples, subject_node, "http://w3id.org/rml/reference");
  if (results.size() == 1) {
    result.term_map_type = "reference";
    result.term_map = results[0];
    return result;
  }

  // Check if template
  results = find_matching_objects(triples, subject_node, "http://w3id.org/rml/template");
  if (results.size() == 1) {
    result.term_map_type = "template";
    result.term_map = results[0];
    return result;
  }

  // Return the result with default or set values
  return result;
}

Predicate get_predicate(const std::vector<NTriple> &triples,
                        const std::string &pom) {
  // Get predicate nodes
  std::vector<std::string> predicate_nodes = find_matching_objects(
      triples, pom, "http://w3id.org/rml/predicateMap");
  std::string predicate_node = predicate_nodes[0];

  // Initialize Subject result with default values
  Predicate result;
  result.term_map_type = "";
  result.term_type = "iri";
  result.term_map = "";

  // Check if constant
  std::vector<std::string> results = find_matching_objects(
      triples, predicate_node, "http://w3id.org/rml/constant");
  if (results.size() == 1) {
    result.term_map_type = "constant";
    result.term_map = results[0];
    return result;
  }

  // Check if reference
  results = find_matching_objects(triples, predicate_node, "http://w3id.org/rml/reference");
  if (results.size() == 1) {
    result.term_map_type = "reference";
    result.term_map = results[0];
    return result;
  }

  // Check if template
  results = find_matching_objects(triples, predicate_node, "http://w3id.org/rml/template");
  if (results.size() == 1) {
    result.term_map_type = "template";
    result.term_map = results[0];
    return result;
  }

  // Return the result with default or set values
  return result;
}

Object get_object_wo_join(const std::vector<NTriple> &triples,
                          const std::string &pom) {
  // Get object nodes
  std::vector<std::string> object_nodes = find_matching_objects(
      triples, pom, "http://w3id.org/rml/objectMap");
  std::string object_node = object_nodes[0];

  // Initialize Subject result with default values
  Object result;
  result.term_map_type = "";
  result.term_type = "literal";
  result.term_map = "";

  // Handle language map
  std::vector<std::string> lang_map_nodes = find_matching_objects(
      triples, object_node, "http://w3id.org/rml/languageMap");
  if (lang_map_nodes.size() == 1) {
    std::string lang_tag = find_matching_objects(triples, lang_map_nodes[0], "http://w3id.org/rml/constant")[0];
    // Check if lang tag is valid
    if (valid_language_subtags.find(lang_tag) == valid_language_subtags.end()) {
      std::cout << "Runtime error occurred. Language tag is not supported!" << std::endl;
      std::exit(1);
    }
    result.lang_tag = lang_tag;
  }

  // Handle data type
  std::vector<std::string> data_type_map_nodes = find_matching_objects(
      triples, object_node, "http://w3id.org/rml/datatypeMap");
  if (data_type_map_nodes.size() == 1) {
    std::string data_type = find_matching_objects(triples, data_type_map_nodes[0], "http://w3id.org/rml/constant")[0];
    // Check if lang tag is valid
    result.data_type = data_type;
  }

  bool term_type_given = false;

  // check if term typ is given
  std::vector<std::string> results = find_matching_objects(triples, object_node, "http://w3id.org/rml/termType");
  if (results.size() == 1) {
    term_type_given = true;
    std::string new_term_type = results[0];
    if (new_term_type == "http://w3id.org/rml/IRI") {
      result.term_type = "iri";
    } else if (new_term_type == "http://w3id.org/rml/BlankNode"){
      result.term_type = "blanknode";
    } else if (new_term_type == "http://w3id.org/rml/Literal") {
      result.term_type = "literal";
    }
  }

  // Check if constant
  results = find_matching_objects(triples, object_node, "http://w3id.org/rml/constant");
  if (results.size() == 1) {
    result.term_map_type = "constant";
    result.term_map = results[0];

    if (result.term_map.substr(0, 4) == "http" && !term_type_given) {
      result.term_type = "iri";
    }

    // Check for lang tag annotation
    auto [temp_term_map, temp_lang_tag] = check_annotated_lang_tag(result.term_map, result.lang_tag);
    result.term_map = std::move(temp_term_map);
    result.lang_tag = std::move(temp_lang_tag);

    return result;
  }

  // Check if reference
  results = find_matching_objects(triples, object_node, "http://w3id.org/rml/reference");
  if (results.size() == 1) {
    result.term_map_type = "reference";
    result.term_map = results[0];

    // Check for lang tag annotation
    auto [temp_term_map, temp_lang_tag] = check_annotated_lang_tag(result.term_map, result.lang_tag);
    result.term_map = std::move(temp_term_map);
    result.lang_tag = std::move(temp_lang_tag);

    return result;
  }

  // Check if template
  results = find_matching_objects(triples, object_node, "http://w3id.org/rml/template");
  if (results.size() == 1) {
    result.term_map_type = "template";
    result.term_map = results[0];
    if (!term_type_given) {
      result.term_type = "iri";
    }

    // Check for lang tag annotation
    auto [temp_term_map, temp_lang_tag] = check_annotated_lang_tag(result.term_map, result.lang_tag);
    result.term_map = std::move(temp_term_map);
    result.lang_tag = std::move(temp_lang_tag);

    return result;
  }

  // TODO: Check for data_type and lang_map

  // Return the result with default or set values
  return result;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// RA generation functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::tuple<Object, std::string> get_object_w_join(
    const std::vector<NTriple> &triples, const std::string &pom) {
  // Initialize Object result with default values
  Object result;
  result.term_map_type = "";
  result.term_type = "literal";
  result.term_map = "";

  // Get object nodes
  std::vector<std::string> object_nodes = find_matching_objects(
      triples, pom, "http://w3id.org/rml/objectMap");
  std::string object_node = object_nodes[0];

  // If no joinCondition is specified -> "naturalJoin" else "innerJoin"
  result.join_type = "natural-join";

  std::vector<std::string> join_condition_nodes = find_matching_objects(
      triples, object_node, "http://w3id.org/rml/joinCondition");
  std::string join_condition_node = "";
  if (join_condition_nodes.size() == 1) {
    join_condition_node = join_condition_nodes[0];
    result.join_type = "equi-join";

    // Get Join conditions
    std::vector<std::string> child_arr = find_matching_objects(
        triples, join_condition_node, "http://w3id.org/rml/child");
    std::string child = child_arr[0];

    std::vector<std::string> parent_arr = find_matching_objects(
        triples, join_condition_node, "http://w3id.org/rml/parent");
    std::string parent = parent_arr[0];

    result.join_condition[0] = child;
    result.join_condition[1] = parent;
  }

  // Get parentTM
  std::vector<std::string> parent_tm_nodes = find_matching_objects(triples, object_node, "http://w3id.org/rml/parentTriplesMap");
  std::string parent_tm_node = parent_tm_nodes[0];

  // Get parent source
  std::vector<std::string> parent_tm_logical_source_nodes = find_matching_objects(triples, parent_tm_node, "http://w3id.org/rml/logicalSource");
  std::string parent_tm_logical_source_node = parent_tm_logical_source_nodes[0];

  std::vector<std::string> parent_tm_source_nodes = find_matching_objects(triples, parent_tm_logical_source_node, "http://w3id.org/rml/source");
  std::string parent_tm_source_node = parent_tm_source_nodes[0];

  std::vector<std::string> parent_tm_sources = find_matching_objects(triples, parent_tm_source_node, "http://w3id.org/rml/path");
  std::string parent_tm_source = parent_tm_sources[0];

  // Get parent subject aka object
  std::vector<std::string> parent_tm_subject_nodes = find_matching_objects(triples, parent_tm_node, "http://w3id.org/rml/subjectMap");
  std::string parent_tm_subject_node = parent_tm_subject_nodes[0];

  // Check if constant
  std::vector<std::string> results = find_matching_objects(
      triples, parent_tm_subject_node, "http://w3id.org/rml/constant");
  if (results.size() == 1) {
    result.term_map_type = "constant";
    result.term_map = results[0];
    if (result.term_map.substr(0, 4) == "http") {
      result.term_type = "iri";
    }
    return {result, parent_tm_source};
  }

  // Check if reference
  results = find_matching_objects(triples, parent_tm_subject_node, "http://w3id.org/rml/reference");
  if (results.size() == 1) {
    result.term_map_type = "reference";
    result.term_map = results[0];
    result.term_type = "literal";
    return {result, parent_tm_source};
  }

  // Check if template
  results = find_matching_objects(triples, parent_tm_subject_node, "http://w3id.org/rml/template");
  if (results.size() == 1) {
    result.term_map_type = "template";
    result.term_map = results[0];
    result.term_type = "iri";
    return {result, parent_tm_source};
  }

  // TODO: Check for data_type and lang_map

  // Return the result with default or set values
  return {result, parent_tm_source};
}

std::vector<std::string> get_projected_attributes(const Subject &subj,
                                                  const Predicate &pred,
                                                  const Object &obj) {
  std::set<std::string> unique_attributes;

  // Handle Subject
  if (!subj.term_map_type.empty()) {
    if (subj.term_map_type == "template") {
      std::vector<std::string> res = extract_substrings(subj.term_map);
      unique_attributes.insert(res.begin(), res.end());
    } else if (subj.term_map_type == "reference") {
      unique_attributes.insert(subj.term_map);
    }
  }

  // Handle Predicate
  if (!pred.term_map_type.empty()) {
    if (pred.term_map_type == "template") {
      std::vector<std::string> res = extract_substrings(pred.term_map);
      unique_attributes.insert(res.begin(), res.end());
    } else if (pred.term_map_type == "reference") {
      unique_attributes.insert(pred.term_map);
    }
  }

  // Handle Object
  if (!obj.term_map_type.empty()) {
    if (obj.term_map_type == "template") {
      std::vector<std::string> res = extract_substrings(obj.term_map);
      unique_attributes.insert(res.begin(), res.end());
    } else if (obj.term_map_type == "reference") {
      unique_attributes.insert(obj.term_map);
    }
  }

  // Handle join condition if available
  bool is_empty = std::all_of(obj.join_condition.begin(), obj.join_condition.end(), [](const std::string &s) { return s.empty(); });
  // if object is empty -> generation of child join; else geneartion of parent
  // join

  if (!is_empty) {
    if (obj.term_map_type.empty()) {
      unique_attributes.insert(obj.join_condition[0]);
    } else {
      unique_attributes.insert(obj.join_condition[1]);
    }
  }

  std::vector<std::string> projected_attributes;
  // Copy unique attributes back into the projected_attributes vector
  projected_attributes.assign(unique_attributes.begin(), unique_attributes.end());

  return projected_attributes;
}

std::string replace_substring(const std::string &original,
                              const std::string &toReplace,
                              const std::string &replacement) {
  std::string result = original;
  std::size_t pos = result.find(toReplace);
  if (pos != std::string::npos) {
    result.replace(pos, toReplace.length(), replacement);
  }
  return result;
}

std::string create_complex_tree(const std::vector<NTriple> &triples) {
  // Get source
  std::vector<std::string> sources = find_matching_objects(triples, "", "http://w3id.org/rml/path");

  // Get root tm
  std::string root_tm = get_root_tm(triples);

  // Get pom
  std::string pom = get_predicate_object_map(triples, root_tm);

  // get subject
  Subject subj = get_subject(triples, root_tm);

  // get predicate
  Predicate pred = get_predicate(triples, pom);

  // get object
  auto [obj, parent_source] = get_object_w_join(triples, pom);

  // get graph
  std::vector<Graph> graphs = get_graph(triples, root_tm, pom);

  // Get normal source:
  if (sources.size() > 1) {
    if (sources[0] != sources[1]) {
      sources.erase(std::remove(sources.begin(), sources.end(), parent_source), sources.end());
    }
  }

  ///////////////////////////

  // Get projected attributes of input 1
  Object empty_obj;
  empty_obj.join_condition = obj.join_condition;  // copy join condition for projection
  std::vector<std::string> proj_attributes1 = get_projected_attributes(subj, pred, empty_obj);

  // Get projected attributeds of input 2
  Subject empty_subj;
  Predicate empty_pred;
  std::vector<std::string> proj_attributes2 = get_projected_attributes(empty_subj, empty_pred, obj);

  // Generate Create

  // Generate argument string
  std::string arguments_file1 = "";
  for (size_t i = 0; i < proj_attributes1.size(); ++i) {
    arguments_file1 += std::format("{}", proj_attributes1[i]);
    if (i < proj_attributes1.size() - 1) {
      arguments_file1 += ",";
    }
  }

  // Generate projection
  std::string projection_file1_node = "pi[" + arguments_file1 + "](" + sources[0] + ")";

  //////////////////////////////////////

  // Generate argument string
  std::string arguments_file2 = "";
  for (size_t i = 0; i < proj_attributes2.size(); ++i) {
    arguments_file2 += std::format("{}", proj_attributes2[i]);
    if (i < proj_attributes2.size() - 1) {
      arguments_file2 += ",";
    }
  }

  std::string projection_file2_node = "pi[" + arguments_file2 + "](" + parent_source + ")";

  //////////////////////////////////////

  // Generate join
  // 1|equi-join|{'type': 'equi-join', 'arguments': ['i53c2_Sport', 'i0d95_ID'],
  // 'in_relation': ['i53c2', 'i0d95'], 'out_relation': 'i9008'}

  std::string join_node;
  if (obj.join_type == "natural-join") {
    join_node = "(" + projection_file1_node + ") bowtie (" + projection_file2_node + ")";
  } else {
    join_node = "(" + projection_file1_node + ") bowtie [" + sources[0] + "_" + obj.join_condition[0] + "=" + parent_source + "_" + obj.join_condition[1] + "] (" + projection_file2_node + ")";
  }

  //////////////////////////////////////

  // Format elements correctly
  // Subject
  if (obj.join_type == "natural-join") {
  } else {
    if (subj.term_map_type == "template") {
      std::vector<std::string> sub_strings = extract_substrings(subj.term_map);
      for (const auto &sub_str : sub_strings) {
        std::string replacement = std::format("{}_{}", sources[0], sub_str);
        subj.term_map = replace_substring(subj.term_map, "{" + sub_str + "}", "{" + replacement + "}");
      }
    } else if (subj.term_map_type == "reference") {
      std::string replacement = std::format("{}_{}", sources[0], subj.term_map);
      subj.term_map = replace_substring(subj.term_map, "{" + subj.term_map + "}", "{" + replacement + "}");
    }

    // Predicate
    if (pred.term_map_type == "template") {
      std::vector<std::string> sub_strings = extract_substrings(pred.term_map);
      for (const auto &sub_str : sub_strings) {
        std::string replacement = std::format("{}_{}", sources[0], sub_str);
        pred.term_map = replace_substring(pred.term_map, "{" + sub_str + "}", "{" + replacement + "}");
      }
    } else if (pred.term_map_type == "reference") {
      std::string replacement = std::format("{}_{}", sources[0], pred.term_map);
      pred.term_map = replace_substring(pred.term_map, "{" + pred.term_map + "}", "{" + replacement + "}");
    }

    // Object
    if (obj.term_map_type == "template") {
      std::vector<std::string> sub_strings = extract_substrings(obj.term_map);
      for (const auto &sub_str : sub_strings) {
        std::string replacement = std::format("{}_{}", parent_source, sub_str);
        obj.term_map = replace_substring(obj.term_map, "{" + sub_str + "}", "{" + replacement + "}");
      }
    } else if (obj.term_map_type == "reference") {
      std::string replacement = std::format("{}_{}", parent_source, obj.term_map);
      obj.term_map = replace_substring(obj.term_map, "{" + obj.term_map + "}", "{" + replacement + "}");
    }

    // Graph
    for (auto &graph : graphs) {
      if (!graph.term_map.empty()) {
        if (graph.term_map_type == "template") {
          std::vector<std::string> sub_strings =
              extract_substrings(graph.term_map);
          for (const auto &sub_str : sub_strings) {
            std::string replacement = std::format("{}_{}", sources[0], sub_str);
            graph.term_map = replace_substring(graph.term_map, sub_str, replacement);
          }
        } else if (graph.term_map_type == "reference") {
          std::string replacement = std::format("{}_{}", sources[0], graph.term_map);
          graph.term_map = replace_substring(graph.term_map, graph.term_map, replacement);
        }
      }
    }
  }

  // Generate create projection
  std::string subj_create = "create(" + subj.term_map + "," + subj.term_map_type + "," + subj.term_type + ") -> S";
  std::string pred_create = "create(" + pred.term_map + "," + pred.term_map_type + "," + pred.term_type + ") -> P";
  std::string obj_create = "create(" + obj.term_map + "," + obj.term_map_type + "," + obj.term_type + "," + obj.lang_tag + "," + obj.data_type + ") -> O";

  std::string graph_create1 = "";
  std::string graph_create2 = "";
  if (graphs.size() == 1) {
    if (!graphs[0].term_map.empty()) {
      graph_create1 = "create(" + graphs[0].term_map + "," + graphs[0].term_map_type + "," + graphs[0].term_type + ") -> G";
    }
  } else if (graphs.size() == 2) {
    if (!graphs[0].term_map.empty()) {
      graph_create1 = "create(" + graphs[0].term_map + "," + graphs[0].term_map_type + "," + graphs[0].term_type + ") -> G";
    }
    if (!graphs[1].term_map.empty()) {
      graph_create2 = "create(" + graphs[1].term_map + "," + graphs[1].term_map_type + "," + graphs[1].term_type + ") -> G";
    }
  }

  std::vector<std::string> proj_nodes;
  if (!graph_create1.empty()) {
    std::string create_projection_node;
    create_projection_node = "pi[" + subj_create + "," + pred_create + "," + obj_create + "," + graph_create1 + "]";
    proj_nodes.push_back(create_projection_node);
  }

  if (!graph_create2.empty()) {
    std::string create_projection_node;
    create_projection_node = "pi[" + subj_create + "," + pred_create + "," + obj_create + "," + graph_create2 + "]";
    proj_nodes.push_back(create_projection_node);
  }

  if (graph_create1.empty() && graph_create2.empty()) {
    std::string create_projection_node;
    create_projection_node = "pi[" + subj_create + "," + pred_create + "," + obj_create + "]";
    proj_nodes.push_back(create_projection_node);
  }

  // Generate final result
  std::string final_result = "";
  for (const auto &proj_node : proj_nodes) {
    std::string result = proj_node + "(" + join_node + ")";
    final_result += result + "\n";
  }

  return final_result;
}

std::string create_simple_tree(const std::vector<NTriple> &triples) {
  /////////////////////
  std::vector<std::string> final_result;
  /////////////////////
  // Get source
  std::string source = find_matching_objects(triples, "", "http://w3id.org/rml/path")[0];

  // Get root tm
  std::string root_tm = get_root_tm(triples);

  // Get pom
  std::string pom = get_predicate_object_map(triples, root_tm);

  // get subject
  Subject subj = get_subject(triples, root_tm);

  // get predicate
  Predicate pred = get_predicate(triples, pom);

  // get object
  Object obj = get_object_wo_join(triples, pom);

  // get graph
  std::vector<Graph> graphs = get_graph(triples, root_tm, pom);

  ///////////////////////////

  // Get projected attributes
  std::vector<std::string> proj_attributes = get_projected_attributes(subj, pred, obj);

  // Generate Create //

  // Generate argument string
  std::string arguments = "";
  for (size_t i = 0; i < proj_attributes.size(); ++i) {
    arguments += std::format("{}", proj_attributes[i]);
    if (i < proj_attributes.size() - 1) {
      arguments += ",";
    }
  }

  // Generate projection
  std::string projection_node = "pi[" + arguments + "](" + source + ")";

  // Generate create projection
  std::string create_val = "pi[create(" + subj.term_map + "," + subj.term_map_type + "," + subj.term_type + ") -> S," +
                           "create(" + pred.term_map + "," + pred.term_map_type + "," + pred.term_type + ") -> P," +
                           "create(" + obj.term_map + "," + obj.term_map_type + "," + obj.term_type + "," + obj.lang_tag + "," + obj.data_type + ") -> O";

  if (graphs.size() == 1 && !graphs[0].term_map.empty()) {
    create_val += ", create(" + graphs[0].term_map + "," + graphs[0].term_map_type + "," + graphs[0].term_type + ") -> G]";

    std::string res = create_val + "(" + projection_node + ")";
    final_result.push_back(res);
  } else if (graphs.size() == 2) {
    std::string create_val_1 = create_val;
    std::string create_val_2 = create_val;

    create_val_1 += ", create(" + graphs[0].term_map + "," + graphs[0].term_map_type + "," + graphs[0].term_type + ") -> G]";
    create_val_2 += ", create(" + graphs[1].term_map + "," + graphs[1].term_map_type + "," + graphs[1].term_type + ") -> G]";

    std::string res1 = create_val_1 + "(" + projection_node + ")";
    final_result.push_back(res1);

    std::string res2 = create_val_2 + "(" + projection_node + ")";
    final_result.push_back(res2);
  } else {
    create_val += "]";
    std::string res = create_val + "(" + projection_node + ")";
    final_result.push_back(res);
  }

  std::string res_str = "";
  for (const auto &result : final_result) {
    res_str += result + "\n";
  }

  return res_str;
}

std::string converter(const std::vector<NTriple> &triples) {
  // Check if with join, i.e. two subj. maps
  std::vector<std::string> subject_nodes = find_matching_objects(triples, "", "http://w3id.org/rml/subjectMap");

  // Handle join
  if (subject_nodes.size() == 2) {
    std::string result = create_complex_tree(triples);
    return result;
  }

  // Handle without join
  std::string results = create_simple_tree(triples);
  return results;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Transformation functions from string to vector and the other way round
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Function to split a single line into an NTriple
NTriple split_line(const std::string &line) {
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
std::vector<NTriple> rdf_string_to_vector(const std::string &rdf_string) {
  std::vector<NTriple> triples;
  std::istringstream stream(rdf_string);
  std::string line;

  while (std::getline(stream, line)) {
    if (!line.empty()) {
      NTriple triple = split_line(line);
      triples.push_back(triple);
    }
  }

  return triples;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {
const char *create_relational_algebra(const char *rml_input) {
  // Convert the C-style string into a std::string
  std::string rml(rml_input);

  // Parse string
  std::vector<NTriple> rdf_vector = rdf_string_to_vector(rml);

  // Clear the global result string
  g_result_str.clear();

  g_result_str = converter(rdf_vector);

  // Return result as a C-string
  return g_result_str.c_str();
}

void converter_free(char *ptr) {
  delete[] ptr;
}

}  // extern "C"