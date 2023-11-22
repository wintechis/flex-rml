#include "rdf_vector_helper.h"

//////////////////////////////////////////
///// Triple Manipulation Functions /////
////////////////////////////////////////

/**
 * @brief Removes a specified triple from a vector of NTriples.
 *
 * This function searches for a triple in the vector that matches the given subject, predicate, and object.
 * If such a triple is found, it is efficiently removed by swapping it with the last element of the vector
 * and then calling pop_back, thus avoiding the need to shift elements. If the triple to be removed is already
 * the last element, it is simply removed without swapping. If the triple is not found, the function does nothing.
 *
 * @param triples A reference to a vector of NTriples from which the specified triple will be removed.
 * @param subject The subject of the triple to be removed.
 * @param predicate The predicate of the triple to be removed.
 * @param object The object of the triple to be removed.
 */
void remove_triple(std::vector<NTriple>& triples, const std::string& subject, const std::string& predicate, const std::string& object) {
  auto it = std::find_if(triples.begin(), triples.end(),
                         [subject, predicate, object](const NTriple& triple) {
                           return triple.subject == subject && triple.predicate == predicate && triple.object == object;
                         });
  if (it != triples.end()) {
    if (it != std::prev(triples.end())) {            // check if it is not already the last element
      std::iter_swap(it, std::prev(triples.end()));  // swap with last element
    }
    triples.pop_back();  // remove last element
  }
}

/**
 * @brief Overload of remove_triple -> Removes all triples from a vector of NTriples which specified subject.
 *
 * @param triples A reference to a vector of NTriples from which the specified triple will be removed.
 * @param subject The subject of the triple to be removed.
 * @param object The object of the triple to be removed.
 */
void remove_triple(std::vector<NTriple>& triples, const std::string& subject) {
  auto it = std::remove_if(triples.begin(), triples.end(),
                           [subject](const NTriple& triple) {
                             return triple.subject == subject;
                           });

  triples.erase(it, triples.end());
}

/**
 * Queries the RDF data to find objects based on the provided subject and predicate.
 *
 * @param {std::vector<NTriple>&} triples - The triples to search in.
 * @param {std::string} subject - The subject to match against. If empty, this filter is ignored.
 * @param {std::string} predicate - The predicate to match against. If empty, this filter is ignored.
 *
 * @returns {std::vector<std::string>} The vector containing matched objects.
 */
std::vector<std::string> find_matching_object(const std::vector<NTriple>& triples, const std::string& subject, const std::string& predicate) {
  std::vector<std::string> results;

  for (const NTriple& triple : triples) {
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

/**
 * Queries the list of triples to find subjects based on the provided predicate and optional object.
 *
 * @param triples - A vector containing the list of triples to be queried.
 * @param predicate - The predicate to match against.
 * @param object - (Optional) The object to match against. If empty, this filter is ignored.
 *
 * @returns A vector containing matching subjects.
 */
std::vector<std::string> find_matching_subject(const std::vector<NTriple>& triples,
                                               const std::string& predicate,
                                               const std::string& object) {
  std::vector<std::string> results;

  for (const auto& triple : triples) {
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

/////////////////////////////////////////////////
///// Functions to modify/optimize RML rule /////
/////////////////////////////////////////////////

void expand_join_tripleMaps(std::vector<NTriple>& triples) {
  /*
  If in input like this is found:
  tripeMap2:
    bn1 objectMap bn2
    bn2 parentTriplesMap tripMap1
    bn2 joinCondition bn3
    bn3 child "Sport"
    bn3 parent "ID"

  tripMap1:
    bn5 subjectMap bn6
    bn6 template "http://ex.com/{ID}"

  a new structrue like this is added:
    bn1 objectMap bn10
    bn10 template "http://ex.com/{Sport}"
    bn10 ex:parentSource "Path/to/source/of/tripMap1"

  */

  // Check if some node has a parentTriplesMap
  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(triples, RDF_TYPE, TRIPLES_MAP);

  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get all predicateObjectMap Uris  of current tripleMap_node
    std::vector<std::string> predicateObjectMap_uris = find_matching_object(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP);

    // Iterate over all found predicateObjectMap_uris and extract objectMap
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      // Get objectMap uri / bnode
      std::vector<std::string> temp_result = find_matching_object(triples, predicateObjectMap_uri, RML_OBJECT_MAP);
      // Only size 1 is supported
      if (temp_result.size() != 1) {
        throw_error("Error: More than one objectMap; Can not expand!");
      }

      std::string objectMap_bn = temp_result[0];

      // Check if a parentTriplesMap is specified
      temp_result = find_matching_object(triples, objectMap_bn, RML_PARENT_TRIPLES_MAP);
      if (temp_result.size() == 0) {
        continue;
      } else if (temp_result.size() > 1) {
        throw_error("Error: More than one parentTriplesMap specified!");
      }

      std::string parentTriplesMap_node = temp_result[0];

      // Check if object node has rr:joinCondition specified
      temp_result = find_matching_object(triples, objectMap_bn, RML_JOIN_CONDITION);
      // If a joinCondition is found this method can not be used
      if (temp_result.size() != 1) {
        continue;
      }

      std::string join_condition_bn = temp_result[0];

      // Get child
      temp_result = find_matching_object(triples, join_condition_bn, "http://www.w3.org/ns/r2rml#child");
      if (temp_result.size() != 1) {
        throw_error("Error: More than one child in joinCondition specified!");
      }

      std::string child = temp_result[0];

      // Get parent
      temp_result = find_matching_object(triples, join_condition_bn, RML_PARENT);
      if (temp_result.size() != 1) {
        throw_error("Error: More than one parent in joinCondition specified!");
      }

      std::string parent = temp_result[0];

      ////// Extract and modify data from parent subject //////

      // Get subjectMap node of referenced TriplesMap
      temp_result = find_matching_object(triples, parentTriplesMap_node, RML_SUBJECT_MAP);
      if (temp_result.size() != 1) {
        throw_error("Error: More than one subjectMap; Can not expand!");
      }
      std::string subjectMap_node = temp_result[0];

      // Store extracted Info
      std::string new_template_of_parent = "";

      std::string join_reference_condition_available = "false";
      // Query for template in subjectMap of parent
      temp_result = find_matching_object(triples, subjectMap_node, RML_TEMPLATE);
      if (temp_result.size() != 0) {
        std::string current_template_parent = temp_result[0];

        std::string temp_parent = "{" + parent + "}";
        std::string temp_child = "{" + child + "}";

        // replace all entries of "parent" with "child"
        new_template_of_parent = replace_substring(current_template_parent, temp_parent, temp_child);

        std::vector<std::string> query_string = extract_substrings(current_template_parent);

        // Reference Condition is working if parent key and string in template are the same
        if (query_string[0] == parent) {
          join_reference_condition_available = "true";
        }
      }

      // -> Get source of parent triples map
      // Get logical source node
      temp_result = find_matching_object(triples, parentTriplesMap_node, RML_LOGICAL_SOURCE);
      if (temp_result.size() != 1) {
        throw_error("Error: More than one source; Can not expand!");
      }
      std::string parentTriplesMap_logicalSource_node = temp_result[0];

      // Get path of logical source
      temp_result = find_matching_object(triples, parentTriplesMap_logicalSource_node, RML_SOURCE);
      if (temp_result.size() != 1) {
        throw_error("Error: More than one logical source; Can not expand!");
      }
      std::string parentTriplesMap_source = temp_result[0];

      // Get reference formulation
      temp_result = find_matching_object(triples, parentTriplesMap_logicalSource_node, RML_REFERENCE_FORMULATION);
      if (temp_result.size() != 1) {
        throw_error("Error: More than one reference formulation; Can not expand!");
      }
      std::string parentTriplesMap_referenceFormulation = temp_result[0];

      ////// Create new Nodes //////
      // Generate blanke node
      std::string blank_node = "b" + std::to_string(blank_node_counter++);

      // Create new data node for objectMap
      NTriple temp_triple;
      temp_triple.subject = predicateObjectMap_uri;
      temp_triple.predicate = RML_OBJECT_MAP;
      temp_triple.object = blank_node;

      triples.push_back(temp_triple);

      // Object has now the modified subject of parent
      if (new_template_of_parent != "") {
        temp_triple.subject = blank_node;
        temp_triple.predicate = RML_TEMPLATE;
        temp_triple.object = new_template_of_parent;

        triples.push_back(temp_triple);
      }

      // Add parentSource triple
      temp_triple.subject = blank_node;
      temp_triple.predicate = PARENT_SOURCE;
      temp_triple.object = parentTriplesMap_source;

      triples.push_back(temp_triple);

      // Add reference formulation
      temp_triple.subject = blank_node;
      temp_triple.predicate = PARENT_REFERENCE_FORMULATION;
      temp_triple.object = parentTriplesMap_referenceFormulation;

      triples.push_back(temp_triple);

      // Add parent
      temp_triple.subject = blank_node;
      temp_triple.predicate = RML_PARENT;
      temp_triple.object = parent;

      triples.push_back(temp_triple);

      // Add child
      temp_triple.subject = blank_node;
      temp_triple.predicate = RML_CHILD;
      temp_triple.object = child;

      triples.push_back(temp_triple);

      // Add join_reference_condition
      temp_triple.subject = blank_node;
      temp_triple.predicate = JOIN_REFERENCE_CONDITION;
      temp_triple.object = join_reference_condition_available;

      triples.push_back(temp_triple);

      // Delete old nodes
      // Remove node referencing parent map
      remove_triple(triples, predicateObjectMap_uri, RML_OBJECT_MAP, objectMap_bn);

      // Remove all triple where the objectMap_bn is in subject position
      remove_triple(triples, objectMap_bn);

      // Remove all triple where the objectMap_bn is in subject position
      remove_triple(triples, join_condition_bn);
    }
  }
}

void expand_local_tripleMaps(std::vector<NTriple>& triples) {
  /*
  If an input like this is found:
    bn1 objectMap bn2
    bn2 a referenceObjectMap
    bn2 parentTriplesMap tripMap1

    It is transformed to

    bn1 objectMap bn5

    Where bn5 is equal to subjectMap bn of triplMap1.
  */

  // Check if some node has a parentTriplesMap
  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(triples, RDF_TYPE, TRIPLES_MAP);

  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get all predicateObjectMap Uris  of current tripleMap_node
    std::vector<std::string> predicateObjectMap_uris = find_matching_object(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP);

    // Iterate over all found predicateObjectMap_uris and extract objectMap
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      // Get objectMap uri / bnode
      std::vector<std::string> temp_result = find_matching_object(triples, predicateObjectMap_uri, RML_OBJECT_MAP);
      // Only size 1 is supported
      if (temp_result.size() != 1) {
        throw_error("Error: More than one objectMap; Can not expand!");
      }

      std::string objectMap_bn = temp_result[0];

      // Check if a parentTriplesMap is specified
      temp_result = find_matching_object(triples, objectMap_bn, RML_PARENT_TRIPLES_MAP);
      if (temp_result.size() == 0) {
        continue;
      } else if (temp_result.size() > 1) {
        throw_error("Error: More than one parentTriplesMap specified!");
      }
      std::string parentTriplesMap_node = temp_result[0];

      // Check if object node has rr:joinCondition specified
      temp_result = find_matching_object(triples, objectMap_bn, RML_JOIN_CONDITION);
      // If a joinCondition is found this method can not be used
      if (temp_result.size() != 0) {
        continue;
      }

      // Get subjectMap node of reference TriplesMap
      temp_result = find_matching_object(triples, parentTriplesMap_node, RML_SUBJECT_MAP);
      if (temp_result.size() != 1) {
        throw_error("Error: More than one subjectMap; Can not expand!");
      }
      std::string subjectMap_node = temp_result[0];

      // Remove node referencing parent map
      remove_triple(triples, predicateObjectMap_uri, RML_OBJECT_MAP, objectMap_bn);

      // Remove all triple where the objectMap_bn is in subject position
      remove_triple(triples, objectMap_bn);

      // Generate blanke node
      std::string blank_node = "b" + std::to_string(blank_node_counter++);

      // Generate new triple representing predicateObjectMap
      NTriple temp_triple;
      temp_triple.subject = predicateObjectMap_uri;
      temp_triple.predicate = RML_OBJECT_MAP;
      temp_triple.object = subjectMap_node;

      triples.push_back(temp_triple);
    }
  }
}

/**
 * @brief Expands redicateObjectMaps with more than one predicate in a given set of NTriples.
 *
 * @param triples A reference to a vector of NTriples that will be expanded.
 */
void expand_multiple_predicates(std::vector<NTriple>& triples) {
  /*
  If an input like this is found:
    triplesMap  predObjMap  bn1
    bn1 predicateMap bn2
    bn1 predicate bn3
    bn1 objectMap bn4

    It is transformed to

    triplesMap  predObjMap  bn90
    bn90 predicateMap bn2
    bn90 objectMap bn4

    and

    triplesMap  predObjMap  bn91
    bn91 predicateMap bn3
    bn91 objectMap bn4

    ==========
    OR
    triplesMap predObjMap  bn1
    bn1 predicateMap bn2
    bn1 predicate bn3
    bn1 objectMap bn4
    bn1 objectMap bn10

    It is transformed to

    triplesMap  predObjMap  bn90
    bn90 predicateMap bn2
    bn90 objectMap bn4

    and

    triplesMap  predObjMap  bn91
    bn91 predicateMap bn3
    bn91 objectMap bn4

    and

    triplesMap  predObjMap  bn90
    bn90 predicateMap bn2
    bn90 objectMap bn10

    and

    triplesMap  predObjMap  bn91
    bn91 predicateMap bn3
    bn91 objectMap bn10
  */

  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(triples, RDF_TYPE, TRIPLES_MAP);

  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get all predicateObjectMap Uris  of current tripleMap_node
    std::vector<std::string> predicateObjectMap_uris = find_matching_object(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP);
    // Iterate over all found predicateObjectMap_uris and extract predicate
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      // get all predicate uris / bnodes
      std::vector<std::string> predicate_bns = find_matching_object(triples, predicateObjectMap_uri, RML_PREDICATE_MAP);

      // If size greater than 1 -> Needs to be expanded
      if (predicate_bns.size() <= 1) {
        continue;
      }
      // Get objectMap uri / bnode
      std::vector<std::string> objectMap_bns = find_matching_object(triples, predicateObjectMap_uri, RML_OBJECT_MAP);

      for (const auto& objectMap_bn : objectMap_bns) {
        // remove old triples
        // triplesMap  predicateObjectMap  bn
        remove_triple(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP, predicateObjectMap_uri);

        // remove old triples
        // bnode objectMap bnode
        remove_triple(triples, predicateObjectMap_uri, RML_OBJECT_MAP, objectMap_bn);

        // Expand each predicate
        for (size_t j = 0; j < predicate_bns.size(); j++) {
          std::string predicate_bn = predicate_bns[j];

          remove_triple(triples, predicateObjectMap_uri, RML_PREDICATE_MAP, predicate_bn);

          // Generate blanke node
          std::string blank_node = "b" + std::to_string(blank_node_counter++);

          // Generate new triple representing predicateObjectMap
          NTriple temp_triple;
          temp_triple.subject = tripleMap_node;
          temp_triple.predicate = RML_PREDICATE_OBJECT_MAP;
          temp_triple.object = blank_node;

          triples.push_back(temp_triple);

          temp_triple.subject = blank_node;
          temp_triple.predicate = RML_PREDICATE_MAP;
          temp_triple.object = predicate_bn;

          triples.push_back(temp_triple);

          temp_triple.subject = blank_node;
          temp_triple.predicate = RML_OBJECT_MAP;
          temp_triple.object = objectMap_bn;

          triples.push_back(temp_triple);
        }
      }
    }
  }
}

void expand_multiple_objects(std::vector<NTriple>& triples) {
  /*
  If an input like this is found:
    triplesMap  predObjMap  bn1
    bn1 predicateMap bn2
    bn1 objectMap bn3
    bn1 objectMap bn4

    It is transformed to

    triplesMap  predObjMap  bn90
    bn90 predicateMap bn2
    bn90 objectMap bn3

    and

    triplesMap  predObjMap  bn91
    bn91 predicateMap bn2
    bn91 objectMap bn4
  */

  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(triples, RDF_TYPE, TRIPLES_MAP);
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get all predicateObjectMap Uris  of current tripleMap_node
    std::vector<std::string> predicateObjectMap_uris = find_matching_object(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP);

    // Iterate over all found predicateObjectMap_uris and extract predicate
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      // get all object uris / bnodes
      std::vector<std::string> objectMap_bns = find_matching_object(triples, predicateObjectMap_uri, RML_OBJECT_MAP);
      //// If size is smaller than 1 -> No expansion is needed ////
      if (objectMap_bns.size() <= 1) {
        continue;
      }
      //// Otherwise expand ////
      for (const auto& objectMap_bn : objectMap_bns) {
        // get all predicate uris / bnodes -> should max be 1 at this point
        std::vector<std::string> predicate_bns = find_matching_object(triples, predicateObjectMap_uri, RML_PREDICATE_MAP);
        if (predicate_bns.size() != 1) {
          throw_error("Error: More than one predicate map found; Not supported at this point!");
        }

        // Add new triple
        // Generate blanke node
        std::string blank_node = "b" + std::to_string(blank_node_counter++);

        // Generate new triple representing predicateObjectMap
        NTriple temp_triple;
        temp_triple.subject = tripleMap_node;
        temp_triple.predicate = RML_PREDICATE_OBJECT_MAP;
        temp_triple.object = blank_node;

        triples.push_back(temp_triple);

        // Generate new triple representing predicate
        temp_triple.subject = blank_node;
        temp_triple.predicate = RML_PREDICATE_MAP;
        temp_triple.object = predicate_bns[0];

        triples.push_back(temp_triple);

        // Generate new triple representing predicate
        temp_triple.subject = blank_node;
        temp_triple.predicate = RML_OBJECT_MAP;
        temp_triple.object = objectMap_bn;

        triples.push_back(temp_triple);
      }
    }
    // TODO: Find better solution for deleting the old element
    // remove old triples
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      // get all object uris / bnodes
      std::vector<std::string> objectMap_bns = find_matching_object(triples, predicateObjectMap_uri, RML_OBJECT_MAP);
      //// If size is smaller than 1 -> No expansion is needed ////
      if (objectMap_bns.size() <= 1) {
        continue;
      }
      // triplesMap  predicateObjectMap  bn
      remove_triple(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP, predicateObjectMap_uri);

      // Remove all old triple with subject: predicateObjectMap_uri
      remove_triple(triples, predicateObjectMap_uri);
    }
  }
}

void expand_classes(std::vector<NTriple>& triples) {
  // Query for logical source subject
  std::vector<std::string> triple_maps_subjects = find_matching_subject(triples, RML_LOGICAL_SOURCE, "");
  std::vector<std::string> triple_maps_subjects_with_classes = find_matching_subject(triples, RDF_TYPE, TRIPLES_MAP);

  for (const auto& triple_map_subject : triple_maps_subjects) {
    // Check if triple_map_subject is already in triple_maps_subjects_with_classes
    if (std::find(triple_maps_subjects_with_classes.begin(), triple_maps_subjects_with_classes.end(), triple_map_subject) != triple_maps_subjects_with_classes.end()) {
      continue;
    }

    NTriple temp_triple;
    temp_triple.subject = triple_map_subject;
    temp_triple.predicate = RDF_TYPE;
    temp_triple.object = TRIPLES_MAP;
    triples.push_back(temp_triple);
  }
}

/**
 * @brief Expands the constants in a given set of NTriples.
 *
 * This function takes a vector of NTriples and expands the constants found within
 * them, based on predefined constant predicates and map types. The function processes
 * each triple and replaces them with new triples that represent the expanded constants.
 *
 * @param triples A reference to a vector of NTriples that will be expanded.
 */
void expand_constants(std::vector<NTriple>& triples) {
  // Define all possible short handles for constants
  const std::array<std::string, 6> constant_predicates = {
      RML_SUBJECT,
      RML_PREDICATE,
      RML_OBJECT,
      RML_GRAPH,
      RML_DATA_TYPE,
      RML_LANGUAGE};

  // Define all possible map types
  const std::array<std::string, 6> constant_map_types = {
      RML_SUBJECT_MAP,
      RML_PREDICATE_MAP,
      RML_OBJECT_MAP,
      RML_GRAPH_MAP,
      RML_DATA_TYPE_MAP,
      RML_LANGUAGE_MAP};

  // Temporary storage for query results
  std::vector<std::string> temp_query_results;

  // Iterate over each constant predicate
  for (size_t i = 0; i < constant_predicates.size(); i++) {
    // Initialize current constant predicate and map type
    const std::string current_constant_predicate = constant_predicates[i];
    const std::string current_map_type = constant_map_types[i];

    // Find all subjects that have the current constant predicate
    std::vector<std::string> searched_map_nodes = find_matching_subject(triples, current_constant_predicate, "");

    // Expand each found node
    for (const auto& searched_map_node : searched_map_nodes) {
      // Retrieve all values of the objects associated with the current constant predicate
      temp_query_results = find_matching_object(triples, searched_map_node, current_constant_predicate);

      // If results are found, replace each occurrence
      if (!temp_query_results.empty()) {
        for (const auto& temp_query_result : temp_query_results) {
          std::string object_position_value = temp_query_result;
          std::string predicate_position_value = current_constant_predicate;

          // Find and replace the triple
          // TODO: Optimize or use remove_triple function?
          auto it = std::find_if(triples.begin(), triples.end(),
                                 [predicate_position_value, object_position_value](const NTriple& triple) {
                                   return triple.predicate == predicate_position_value && triple.object == object_position_value;
                                 });

          // If the triple is found, remove and replace it with expanded triples
          if (it != triples.end()) {
            triples.erase(it);

            NTriple temp_triple;

            std::string blank_node = "b" + std::to_string(blank_node_counter++);

            temp_triple.subject = searched_map_node;
            temp_triple.predicate = current_map_type;
            temp_triple.object = blank_node;
            triples.push_back(temp_triple);

            temp_triple.subject = blank_node;
            temp_triple.predicate = RML_CONSTANT;
            temp_triple.object = object_position_value;
            triples.push_back(temp_triple);
          }
        }
      }
    }
  }
}

//////////////////////////////////////////////////////
///// Main function to prepare rml triple vector /////
//////////////////////////////////////////////////////

void read_and_prepare_rml_triple(const std::string& rml_rule, std::vector<NTriple>& rml_triple, std::string& base_uri) {
  // Parse rml in own scope
  {
    RDFParser parser;
    rml_triple = parser.parse(rml_rule);
    base_uri = parser.extracted_base_uri;
  }

  // Add types to triples map
  expand_classes(rml_triple);

  // Expand the constants contained in rml_triples
  expand_constants(rml_triple);

  // Expand multiple predicates
  expand_multiple_predicates(rml_triple);

  // Expand multiple objects
  expand_multiple_objects(rml_triple);

  // Expand nodes which reference as parent triple map a local one
  expand_local_tripleMaps(rml_triple);

  expand_join_tripleMaps(rml_triple);

  // Print all the triples if in debug mode
  for (const NTriple& trip : rml_triple) {
    log_debug(trip.subject.c_str());
    log_debug("   ");
    log_debug(trip.predicate.c_str());
    log_debug("   ");
    log_debug(trip.object.c_str());
    logln_debug(" .");
  }
}