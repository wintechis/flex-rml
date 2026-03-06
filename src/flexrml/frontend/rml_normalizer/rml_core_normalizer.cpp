#include <algorithm>
#include <format>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
/////// Helper functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string generate_bn(int& bn_counter) {
  return "b" + std::to_string(++bn_counter);
}

std::string generateUUID() {
  std::stringstream ss;
  ss << std::hex << std::uppercase;
  for (int i = 0; i < 16; ++i) {
    ss << (rand() % 16);
  }
  return ss.str();
}

bool IsBlankNode(const std::string& s) {
  if (s.empty() || s[0] != 'b') return false;

  const std::string number = s.substr(1);
  if (number.empty()) return false;

  try {
    std::size_t pos;
    int num = std::stoi(number, &pos);

    // Ensure the entire string was converted (no extra characters)
    return pos == number.length();
  } catch (...) {
    return false;
  }
}

bool IsURI(const std::string& s) {
  // absolute IRI
  if (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0) return true;
  // fragment / relative IRI e.g. "#Source"
  if (!s.empty() && s[0] == '#') return true;

  return false;
}

// Remove elements from the triples vector that match any element in to_remove
void removeElements(std::vector<NTriple>& triples, const std::vector<NTriple>& to_remove) {
  for (const auto& rem : to_remove) {
    auto it = std::find(triples.begin(), triples.end(), rem);
    if (it != triples.end()) {
      triples.erase(it);
    }
  }
}

// Add new elements from to_add into the triples vector
void addElements(std::vector<NTriple>& triples, const std::vector<NTriple>& to_add) {
  triples.insert(triples.end(), to_add.begin(), to_add.end());
}

std::vector<std::string> extract_triple_map_nodes(const std::vector<NTriple>& triples) {
  std::vector<std::string> triple_maps;

  const std::string triples_map_type = "http://w3id.org/rml/TriplesMap";

  for (const NTriple& triple : triples) {
    if (triple.predicate == "http://www.w3.org/1999/02/22-rdf-syntax-ns#type" && triple.object == triples_map_type) {
      triple_maps.push_back(triple.subject);
    }
  }

  if (triple_maps.size() == 0) {
    std::cout << "No TMs found." << std::endl;
    std::exit(1);
  }

  return triple_maps;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// Normalization functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<NTriple> expand_classes(const std::vector<NTriple>& input_triples, int& bn_counter) {
  std::vector<NTriple> triples = input_triples;

  std::vector<NTriple> triples_to_remove;
  std::vector<NTriple> triples_to_add;

  std::string predicate_object_map_predicate = "http://w3id.org/rml/predicateObjectMap";
  std::string predicate_predicate = "http://w3id.org/rml/predicate";
  std::string object_predicate = "http://w3id.org/rml/object";
  std::string rdf_type = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";

  for (const auto& triple : triples) {
    // Only process triples that have the relevant predicate
    if (triple.predicate != "http://w3id.org/rml/class") {
      continue;
    }

    // Mark the triple for removal
    triples_to_remove.push_back(triple);

    // Initialize the variable for rml_subject_map_node
    std::string rml_subject_map_node = "";

    // Search for the triple where the object matches the subject of the rml:class triple
    for (const auto& triple1 : triples) {
      if (triple1.object == triple.subject) {
        // We assume that the triple has the desired subject map node
        rml_subject_map_node = triple1.subject;
        break;
      }
    }

    // If no rml_subject_map_node found, skip processing this triple
    if (rml_subject_map_node.empty()) {
      continue;
    }

    std::string bn = generate_bn(bn_counter);

    // Create new triple: rml_subject_map_node -> rml:predicateObjectMap -> new blank node
    triples_to_add.push_back({rml_subject_map_node, predicate_object_map_predicate, bn});

    // Create triple for rdf:type: new blank node -> rml:predicate -> rdf:type
    triples_to_add.push_back({bn, predicate_predicate, rdf_type});

    // Create triple for class: new blank node -> rml:object -> class object
    triples_to_add.push_back({bn, object_predicate, triple.object});
  }

  // Remove the old triples
  removeElements(triples, triples_to_remove);

  // Add the new triples
  addElements(triples, triples_to_add);

  return triples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<NTriple> expand_constants(const std::vector<NTriple>& input_triples, int& bn_counter) {
  std::vector<NTriple> triples = input_triples;

  // Map of predicates to expand and their respective maps.
  std::unordered_map<std::string, std::string> predicates_to_expand = {
      {"http://w3id.org/rml/subject", "http://w3id.org/rml/subjectMap"},
      {"http://w3id.org/rml/predicate", "http://w3id.org/rml/predicateMap"},
      {"http://w3id.org/rml/object", "http://w3id.org/rml/objectMap"},
      {"http://w3id.org/rml/graph", "http://w3id.org/rml/graphMap"},
      {"http://w3id.org/rml/datatype", "http://w3id.org/rml/datatypeMap"},
      {"http://w3id.org/rml/language", "http://w3id.org/rml/languageMap"}};

  std::string constant_predicate = "http://w3id.org/rml/constant";

  std::vector<NTriple> triples_to_remove;
  std::vector<NTriple> triples_to_add;

  for (const auto& triple : triples) {
    // Check if the triple's predicate is in the predicates_to_expand map.
    auto it = predicates_to_expand.find(triple.predicate);
    if (it == predicates_to_expand.end()) {
      continue;
    }

    // Get the corresponding predicate map
    std::string predicate_map = it->second;

    // Generate a new blank node
    std::string bn = generate_bn(bn_counter);

    // Triple 1: subject -> predicate_map -> blank node
    triples_to_add.push_back({triple.subject, predicate_map, bn});

    // Triple 2: blank node -> constant -> object
    triples_to_add.push_back({bn, constant_predicate, triple.object});

    // Mark the original triple for removal
    triples_to_remove.push_back(triple);
  }

  // Remove the old triples
  removeElements(triples, triples_to_remove);

  // Add the new triples
  addElements(triples, triples_to_add);

  return triples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<NTriple> expand_predicate_object_maps(const std::vector<NTriple>& input_triples, int& bn_counter) {
  std::vector<NTriple> triples = input_triples;

  // Dictionaries to store relationships:
  //  - pom_node_to_parent_nodes: maps a predicateObjectMap node (key) to its parent nodes.
  //  - pom_node_to_predicate_maps: maps a pom node (key) to its predicateMap nodes.
  //  - pom_node_to_object_maps: maps a pom node (key) to its objectMap nodes.
  std::unordered_map<std::string, std::vector<std::string>> pom_node_to_parent_nodes;
  std::unordered_map<std::string, std::vector<std::string>> pom_node_to_predicate_maps;
  std::unordered_map<std::string, std::vector<std::string>> pom_node_to_object_maps;

  // Collect data from all triples.
  for (const auto& triple : triples) {
    const std::string& s = triple.subject;
    const std::string& p = triple.predicate;
    const std::string& o = triple.object;

    if (p == "http://w3id.org/rml/predicateObjectMap") {
      // Map the object (which is the pom node) to its parent (s)
      pom_node_to_parent_nodes[o].push_back(s);
    } else if (p == "http://w3id.org/rml/predicateMap") {
      pom_node_to_predicate_maps[s].push_back(o);
    } else if (p == "http://w3id.org/rml/objectMap") {
      pom_node_to_object_maps[s].push_back(o);
    }
  }

  // Iterate over each predicateObjectMap node.
  for (const auto& entry : pom_node_to_parent_nodes) {
    const std::string& pom_node = entry.first;
    const std::vector<std::string>& parent_nodes = entry.second;

    // Get predicateMaps and objectMaps associated with this pom_node.
    std::vector<std::string> predicate_maps;
    std::vector<std::string> object_maps;

    auto it_predicate = pom_node_to_predicate_maps.find(pom_node);
    if (it_predicate != pom_node_to_predicate_maps.end()) {
      predicate_maps = it_predicate->second;
    }

    auto it_object = pom_node_to_object_maps.find(pom_node);
    if (it_object != pom_node_to_object_maps.end()) {
      object_maps = it_object->second;
    }

    // If there is only one predicateMap and one objectMap, no expansion is needed.
    if (predicate_maps.size() <= 1 && object_maps.size() <= 1) {
      continue;
    }

    // Vectors to store triples that need to be removed and added.
    std::vector<NTriple> triples_to_remove;
    std::vector<NTriple> triples_to_add;

    // Collect all triples where the subject is the pom_node.
    for (const auto& triple : triples) {
      if (triple.subject == pom_node) {
        triples_to_remove.push_back(triple);
      }
    }

    // Also remove triples where the predicate is predicateObjectMap and the object is the pom_node.
    for (const auto& triple : triples) {
      if (triple.predicate == "http://w3id.org/rml/predicateObjectMap" && triple.object == pom_node) {
        triples_to_remove.push_back(triple);
      }
    }

    // Create new predicateObjectMap nodes for every combination of predicateMap and objectMap.
    for (const auto& predicate_map : predicate_maps) {
      for (const auto& object_map : object_maps) {
        // Generate a new blank node for the new predicateObjectMap.
        std::string bn = generate_bn(bn_counter);

        // Connect each parent node to the new predicateObjectMap node.
        for (const auto& parent_node : parent_nodes) {
          triples_to_add.push_back({parent_node, "http://w3id.org/rml/predicateObjectMap", bn});
        }

        // Connect the new pom node to its predicateMap and objectMap.
        triples_to_add.push_back({bn, "http://w3id.org/rml/predicateMap", predicate_map});
        triples_to_add.push_back({bn, "http://w3id.org/rml/objectMap", object_map});
      }
    }

    // Remove the old triples and add the new ones.
    removeElements(triples, triples_to_remove);
    addElements(triples, triples_to_add);
  }

  return triples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<NTriple> separate_predicate_object_maps(const std::vector<NTriple>& input_triples) {
  std::vector<NTriple> triples = input_triples;

  std::string triples_map_type = "http://w3id.org/rml/TriplesMap";
  std::string predicate_object_map_predicate = "http://w3id.org/rml/predicateObjectMap";
  std::string subject_map_predicate = "http://w3id.org/rml/subjectMap";
  std::string logical_source_predicate = "http://w3id.org/rml/logicalSource";
  std::string parent_triples_map_predicate = "http://w3id.org/rml/parentTriplesMap";
  std::string join_condition_predicate = "http://w3id.org/rml/joinCondition";

  // Find all TriplesMaps
  std::vector<std::string> triple_maps;
  for (const auto& triple : triples) {
    if (triple.predicate == "http://www.w3.org/1999/02/22-rdf-syntax-ns#type" &&
        triple.object == triples_map_type) {
      triple_maps.push_back(triple.subject);
    }
  }

  // Process each TriplesMap
  for (const auto& tm : triple_maps) {
    // Find all predicateObjectMaps (POMs) for this TriplesMap
    std::vector<std::string> pom_nodes;
    for (const auto& triple : triples) {
      if (triple.subject == tm && triple.predicate == predicate_object_map_predicate) {
        pom_nodes.push_back(triple.object);
      }
    }

    if (pom_nodes.size() <= 1) {
      continue;  // No need to split if only one predicateObjectMap
    }

    // Find the original TriplesMap's subjectMap and logicalSource
    std::string original_subject_map = "";
    std::string original_logical_source = "";
    for (const auto& triple : triples) {
      if (triple.subject == tm && triple.predicate == subject_map_predicate) {
        original_subject_map = triple.object;
        break;  // Assuming only one subjectMap
      }
    }
    for (const auto& triple : triples) {
      if (triple.subject == tm && triple.predicate == logical_source_predicate) {
        original_logical_source = triple.object;
        break;  // Assuming only one logicalSource
      }
    }

    // Iterate over each predicateObjectMap and create a new TriplesMap
    for (const auto& pom : pom_nodes) {
      // Check if this POM has a parentTriplesMap
      std::vector<std::string> parent_tms;
      for (const auto& triple : triples) {
        if (triple.subject == pom && triple.predicate == parent_triples_map_predicate) {
          parent_tms.push_back(triple.object);
        }
      }
      bool has_parent = !parent_tms.empty();

      // Generate a new unique TriplesMap URI by concatenating a UUID
      std::string uuid = generateUUID();
      std::string new_tm = tm + uuid;

      // Add the type triple for the new TriplesMap
      triples.push_back({new_tm, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", triples_map_type});

      if (has_parent) {
        std::string parent_tm = parent_tms[0];
        // Link the new TriplesMap to the parent
        triples.push_back({new_tm, parent_triples_map_predicate, parent_tm});

        // Retrieve parent's logicalSource and subjectMap
        std::string parent_logical_source = "";
        std::string parent_subject_map = "";
        for (const auto& triple : triples) {
          if (triple.subject == parent_tm && triple.predicate == logical_source_predicate) {
            parent_logical_source = triple.object;
            break;
          }
        }
        for (const auto& triple : triples) {
          if (triple.subject == parent_tm && triple.predicate == subject_map_predicate) {
            parent_subject_map = triple.object;
            break;
          }
        }

        // Assign parent's logicalSource and subjectMap to the new TriplesMap if available
        if (!parent_logical_source.empty()) {
          triples.push_back({new_tm, logical_source_predicate, parent_logical_source});
        }
        if (!parent_subject_map.empty()) {
          triples.push_back({new_tm, subject_map_predicate, parent_subject_map});
        }

        // Handle join conditions from the POM
        std::vector<std::string> join_conditions;
        for (const auto& triple : triples) {
          if (triple.subject == pom && triple.predicate == join_condition_predicate) {
            join_conditions.push_back(triple.object);
          }
        }
        for (const auto& jc : join_conditions) {
          triples.push_back({new_tm, join_condition_predicate, jc});
        }
      } else {
        // No parentTriplesMap; use the original TriplesMap's subjectMap and logicalSource
        if (!original_subject_map.empty()) {
          triples.push_back({new_tm, subject_map_predicate, original_subject_map});
        }
        if (!original_logical_source.empty()) {
          triples.push_back({new_tm, logical_source_predicate, original_logical_source});
        }
      }

      // Add the single predicateObjectMap to the new TriplesMap
      triples.push_back({new_tm, predicate_object_map_predicate, pom});
    }

    // Remove the original TriplesMap's predicateObjectMap triples
    for (const auto& pom : pom_nodes) {
      NTriple toRemove{tm, predicate_object_map_predicate, pom};
      std::vector<NTriple> triples_to_remove{toRemove};
      removeElements(triples, triples_to_remove);
    }
  }

  return triples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// --- Function: get_parent_source_node ---
// Implements the SPARQL query:
//   SELECT ?p_tm_sm
//     WHERE {
//         ?c_tm <http://www.w3.org/ns/r2rml#parentTriplesMap> ?p_tm .
//         ?p_tm <http://www.w3.org/ns/r2rml#subjectMap> ?p_tm_sm .
//     }
std::string get_parent_source_node(const std::vector<NTriple>& triples) {
  std::vector<std::string> fitting_objects0;
  // Find all objects of triples with predicate parentTriplesMap
  for (const auto& triple : triples) {
    if (triple.predicate == "http://w3id.org/rml/parentTriplesMap") {
      fitting_objects0.push_back(triple.object);
    }
  }

  std::vector<std::string> fitting_objects1;
  // For each candidate, find the subjectMap triple
  for (const auto& fitting_object : fitting_objects0) {
    for (const auto& triple : triples) {
      if (triple.subject == fitting_object && triple.predicate == "http://w3id.org/rml/subjectMap") {
        fitting_objects1.push_back(triple.object);
      }
    }
  }

  if (fitting_objects1.size() == 1) {
    return fitting_objects1[0];
  } else if (fitting_objects1.empty()) {
    // No join found
    return "";
  } else {
    std::cerr << "Found more than one! Found:";
    for (const auto& s : fitting_objects1) {
      std::cerr << " " << s;
    }
    std::cerr << std::endl;
    throw std::runtime_error("Found more than one! Found");
  }
}

// Given a starting TriplesMap (tm) URI, traverse outgoing edges and build a subgraph.
std::vector<NTriple> generate_subgraph(const std::vector<NTriple>& triples, const std::string& tm) {
  std::vector<NTriple> sub_graph;
  std::unordered_set<std::string> visited_set;
  std::stack<std::string> stack;

  // Start with the given TriplesMap
  stack.push(tm);

  // Define URIs for relevant predicates
  const std::string PREDICATE_OBJECT_MAP = "http://w3id.org/rml/predicateObjectMap";
  const std::string SUBJECT_MAP = "http://w3id.org/rml/subjectMap";

  // Step 1: Identify all subjectMaps in the graph (if needed later)
  std::unordered_set<std::string> subject_maps_set;
  for (const auto& triple : triples) {
    if (triple.predicate == SUBJECT_MAP) {
      subject_maps_set.insert(triple.object);
    }
  }

  // Boolean to track if the first predicateObjectMap has been encountered.
  bool found_first_pom = false;

  while (!stack.empty()) {
    std::string current = stack.top();
    stack.pop();

    // Skip if already visited
    if (visited_set.count(current) > 0) {
      continue;
    }
    visited_set.insert(current);

    // Add all outgoing triples from the current subject
    for (const auto& triple : triples) {
      if (triple.subject != current)
        continue;

      const std::string& p = triple.predicate;
      const std::string& o = triple.object;

      // Handle predicateObjectMap: only process the first encountered
      if (p == PREDICATE_OBJECT_MAP) {
        if (found_first_pom) {
          continue;  // Skip this triple and its outgoing connections.
        } else {
          found_first_pom = true;
        }
      }

      // Add the triple to the subgraph.
      sub_graph.push_back({current, p, o});

      // If the object is a blank node or a URI and hasn't been visited, add it to the stack.
      if ((IsBlankNode(o) || IsURI(o)) && visited_set.find(o) == visited_set.end()) {
        stack.push(o);
      }
    }
  }

  return sub_graph;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<std::vector<NTriple>> separate_triple_maps(const std::vector<std::string>& triple_maps, const std::vector<NTriple>& triples) {
  std::vector<std::vector<NTriple>> rdfSubGraphs;

  // Iterate over each TriplesMap identifier
  for (const auto& tm : triple_maps) {
    // Generate the subgraph starting from this TriplesMap
    std::vector<NTriple> sub_g = generate_subgraph(triples, tm);

    // Define URIs for the required predicates.
    const std::string subjectMap_predicate = "http://w3id.org/rml/subjectMap";
    const std::string predicateMap_predicate = "http://w3id.org/rml/predicateMap";
    const std::string objectMap_predicate = "http://w3id.org/rml/objectMap";

    bool found_subjectMap = false;
    bool found_predicateMap = false;
    bool found_objectMap = false;

    // Check for the presence of a subjectMap in the subgraph.
    for (const auto& triple : sub_g) {
      if (triple.predicate == subjectMap_predicate) {
        found_subjectMap = true;
        break;
      }
    }

    // Check for the presence of a predicateMap.
    for (const auto& triple : sub_g) {
      if (triple.predicate == predicateMap_predicate) {
        found_predicateMap = true;
        break;
      }
    }

    // Check for the presence of an objectMap.
    for (const auto& triple : sub_g) {
      if (triple.predicate == objectMap_predicate) {
        found_objectMap = true;
        break;
      }
    }

    // Only add the subgraph if all required maps are found.
    if (found_subjectMap && found_predicateMap && found_objectMap) {
      rdfSubGraphs.push_back(sub_g);
    }
  }

  return rdfSubGraphs;
}

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
      triples.push_back(triple);
    }
  }

  return triples;
}

// Funciton to convert rdf_vector to string
std::string rdf_string_to_vector(const std::vector<NTriple>& triples) {
  std::string result = "";

  for (const auto& triple : triples) {
    result += triple.subject + "|||" + triple.predicate + "|||" + triple.object + "\n";
  }

  return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<std::vector<NTriple>> normalize_mapping(const std::vector<NTriple>& rml_vector, const int& init_bnode_counter) {
  int bnode_counter = init_bnode_counter;

  const std::vector<NTriple> rml_vector_expanded_classes = expand_classes(rml_vector, bnode_counter);
  const std::vector<NTriple> rml_vector_expanded_constants = expand_constants(rml_vector_expanded_classes, bnode_counter);
  const std::vector<NTriple> rml_vector_expanded_poms = expand_predicate_object_maps(rml_vector_expanded_constants, bnode_counter);
  const std::vector<NTriple> rml_vector_separated_poms = separate_predicate_object_maps(rml_vector_expanded_poms);

  const std::vector<std::string> triple_maps = extract_triple_map_nodes(rml_vector_separated_poms);
  const std::vector<std::vector<NTriple>> rml_sub_graphs = separate_triple_maps(triple_maps, rml_vector_separated_poms);

  return rml_sub_graphs;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void validator(const std::vector<NTriple> &rdf_vector){
  // Check for multiple subject maps
  int tm_cnt = 0;
  for (const auto& triple : rdf_vector){
    if (triple.predicate == "http://www.w3.org/1999/02/22-rdf-syntax-ns#type" && triple.object == "http://w3id.org/rml/TriplesMap"){
      for (const auto& triple2: rdf_vector){
        if (triple2.subject == triple.subject && triple2.predicate == "http://w3id.org/rml/subjectMap" ){
          tm_cnt++;
        }
      }
      if (tm_cnt > 1){
        std::cout << "Error: Found multiple subject maps!" << std::endl;
        std::exit(1);
      }
      tm_cnt = 0;
    }
  }

}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern "C" {

const char* normalize_rml_mapping(const char* input_rdf_mapping, int bn_number) {
  // Transform string to vector
  std::string rdf_rule_str(input_rdf_mapping);
  std::vector<NTriple> rdf_vector = rdf_string_to_vector(rdf_rule_str);

  // Validate
  validator(rdf_vector);

  //  Normalize
  std::vector<std::vector<NTriple>> normalized_graphs = normalize_mapping(rdf_vector, bn_number);

  g_result_str.clear();
  // Transform vectors to one string
  for (const auto& normalized_graph : normalized_graphs) {
    std::string normalized_graph_str = rdf_string_to_vector(normalized_graph);
    g_result_str += normalized_graph_str;
    g_result_str += "====";
  }

  // Return result as a C-string
  return g_result_str.c_str();
}
}