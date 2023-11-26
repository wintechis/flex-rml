#include "rml_extractor.h"

// Predefined list of valid primary language subtags
// https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes
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

/**
 * @brief Retrieves subject map data from a vector of NTriple objects.
 *
 * @param rml_triples Vector of NTriple objects containing the RDF triples to be searched.
 * @param tripleMap_node String representing the node in the RDF triples to be matched.
 * @return SubjectMapInfo struct containing all found information.
 *
 * @note This function will enter an infinite loop if any inconsistencies are found.
 */
SubjectMapInfo extract_rml_info_of_subjectMap(const std::vector<NTriple>& rml_triples, const std::string& tripleMap_node) {
  // -> Each triples map has exactly 1 subjectMap

  SubjectMapInfo subjectMapInfo;

  std::vector<std::string> temp_result;

  std::string node_uri;  // Uri of SubjectMap Object

  // Initialize with default
  subjectMapInfo.termType = IRI_TERM_TYPE;
  subjectMapInfo.constant = "";
  subjectMapInfo.reference = "";
  subjectMapInfo.template_str = "";
  subjectMapInfo.graph_template = "";
  subjectMapInfo.graph_constant = "";
  subjectMapInfo.graph_termType = IRI_TERM_TYPE;
  subjectMapInfo.name_triplesMap_node = tripleMap_node;

  // Get blank node of subjectMap
  temp_result = find_matching_object(rml_triples, tripleMap_node, RML_SUBJECT_MAP);
  if (temp_result.empty()) {
    throw_error("Error: No subjectMap blank node found!");
  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one subjectMap blank node found!");
  } else {
    // Assign the found source to source
    node_uri = temp_result[0];
  }

  // Get subjectMap constant
  temp_result = find_matching_object(rml_triples, node_uri, RML_CONSTANT);
  if (temp_result.size() == 1) {
    subjectMapInfo.constant = temp_result[0];
  }

  // Get subjectMap reference
  temp_result = find_matching_object(rml_triples, node_uri, RML_REFERENCE);
  if (temp_result.size() == 1) {
    subjectMapInfo.reference = temp_result[0];
  }

  // Get subjectMap template
  temp_result = find_matching_object(rml_triples, node_uri, RML_TEMPLATE);
  if (temp_result.size() == 1) {
    subjectMapInfo.template_str = temp_result[0];
  }

  // Get subjectMap termType
  temp_result = find_matching_object(rml_triples, node_uri, RML_TERM_TYPE);
  // If match is found store it -> else default is used
  if (!temp_result.empty()) {
    subjectMapInfo.termType = temp_result[0];
  }

  // Get subjectMap classes
  temp_result = find_matching_object(rml_triples, node_uri, RML_CLASS);
  if (!temp_result.empty()) {
    subjectMapInfo.classes = temp_result;
  }

  // Get subjectMap graphMap
  // Can contain rr:constant, rr:template anbd rr:termType

  // Get subjectMap graph blank node
  temp_result = find_matching_object(rml_triples, node_uri, RML_GRAPH_MAP);
  // Only if blank node is found a graph is specified
  if (!temp_result.empty()) {
    std::string subjectMap_graph_blank_node = temp_result[0];

    // Get graph constant
    temp_result = find_matching_object(rml_triples, subjectMap_graph_blank_node, RML_CONSTANT);
    if (!temp_result.empty()) {
      subjectMapInfo.graph_constant = temp_result[0];
    }

    // Get graph template
    temp_result = find_matching_object(rml_triples, subjectMap_graph_blank_node, RML_TEMPLATE);
    if (!temp_result.empty()) {
      subjectMapInfo.graph_template = temp_result[0];
    }

    // Get graph termType
    temp_result = find_matching_object(rml_triples, subjectMap_graph_blank_node, RML_TERM_TYPE);
    if (!temp_result.empty()) {
      subjectMapInfo.graph_termType = temp_result[0];
    }
  }

  return subjectMapInfo;
}

/**
 * @brief Retrieves predicate map data from a vector of NTriple objects.
 *
 * @param rml_triples Vector of NTriple objects containing the RDF triples to be searched.
 * @param predicateObjectMap_uri String representing the node in the RDF triples to be matched.
 * @return PredicateMapInfo struct containing all found information.
 *
 * @note This function will enter an infinite loop if any inconsistencies are found.
 */
PredicateMapInfo extract_rml_info_of_predicateMap(const std::vector<NTriple>& rml_triples, const std::string& predicateObjectMap_uri) {
  std::vector<std::string> temp_result;

  //// Handle predicateMap ////
  PredicateMapInfo predicateMapInfo;

  // Initialize with default
  predicateMapInfo.constant = "";
  predicateMapInfo.template_str = "";
  predicateMapInfo.reference = "";

  // Get Uri of predicateMap
  std::string node_uri;
  temp_result = find_matching_object(rml_triples, predicateObjectMap_uri, RML_PREDICATE_MAP);
  if (temp_result.empty()) {
    throw_error("Error: No predicateMap uri/blank node found!");

  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one predicateMap uri/blank node found!");
  } else {
    // Assign the found source to source
    node_uri = temp_result[0];
  }

  // Get predicateMap constant
  temp_result = find_matching_object(rml_triples, node_uri, RML_CONSTANT);
  if (temp_result.size() == 1) {
    predicateMapInfo.constant = temp_result[0];
  }

  // Get predicateMap template
  temp_result = find_matching_object(rml_triples, node_uri, RML_TEMPLATE);
  if (temp_result.size() == 1) {
    predicateMapInfo.template_str = temp_result[0];
  }

  // Get predicateMap reference
  temp_result = find_matching_object(rml_triples, node_uri, RML_REFERENCE);
  if (temp_result.size() == 1) {
    predicateMapInfo.reference = temp_result[0];
  }

  return predicateMapInfo;
}

PredicateObjectMapInfo extract_rml_info_of_predicateObjectMap(const std::vector<NTriple>& rml_triples, const std::string& predicateObjectMap_uri) {
  std::vector<std::string> temp_result;

  //// Handle predicateMap ////
  PredicateObjectMapInfo predicateObjectMapInfo;

  // Initialize with default
  predicateObjectMapInfo.graph_constant = "";
  predicateObjectMapInfo.graph_template = "";
  predicateObjectMapInfo.graph_termType = IRI_TERM_TYPE;

  // Get predicateObjectMap graphMap
  // Can contain rr:constant, rr:template anbd rr:termType

  // Get predicateObjectMap graph blank node
  temp_result = find_matching_object(rml_triples, predicateObjectMap_uri, RML_GRAPH_MAP);
  // Only if blank node is found a graph is specified
  if (!temp_result.empty()) {
    std::string subjectMap_graph_blank_node = temp_result[0];

    // Get graph constant
    temp_result = find_matching_object(rml_triples, subjectMap_graph_blank_node, RML_CONSTANT);
    if (!temp_result.empty()) {
      predicateObjectMapInfo.graph_constant = temp_result[0];
    }

    // Get graph template
    temp_result = find_matching_object(rml_triples, subjectMap_graph_blank_node, RML_TEMPLATE);
    if (!temp_result.empty()) {
      predicateObjectMapInfo.graph_template = temp_result[0];
    }

    // Get graph termType
    temp_result = find_matching_object(rml_triples, subjectMap_graph_blank_node, RML_TERM_TYPE);
    if (!temp_result.empty()) {
      predicateObjectMapInfo.graph_termType = temp_result[0];
    }
  }

  return predicateObjectMapInfo;
}

/**
 * @brief Retrieves object map data from a vector of NTriple objects.
 *
 * @param rml_triples Vector of NTriple objects containing the RDF triples to be searched.
 * @param objectObjectMap_uri String representing the node in the RDF triples to be matched.
 * @return PredicateMapInfo struct containing all found information.
 *
 * @note This function will enter an infinite loop if any inconsistencies are found.
 */
ObjectMapInfo extract_rml_info_of_objectMap(const std::vector<NTriple>& rml_triples, const std::string& objectObjectMap_uri) {
  std::vector<std::string> temp_result;

  //// Handle objectMap ////
  ObjectMapInfo objectMapInfo;

  // Initialize with default
  objectMapInfo.constant = "";
  objectMapInfo.template_str = "";
  objectMapInfo.termType = "";
  objectMapInfo.reference = "";
  objectMapInfo.language = "";
  objectMapInfo.parentRef = "";
  objectMapInfo.parentSource = "";
  objectMapInfo.parent = "";
  objectMapInfo.child = "";
  objectMapInfo.dataType = "";
  objectMapInfo.join_reference_condition_available = "";

  // Get Uri of objectMap
  std::string node_uri;
  temp_result = find_matching_object(rml_triples, objectObjectMap_uri, RML_OBJECT_MAP);
  if (temp_result.empty()) {
    throw_error("Error: No objectMap uri/blank node found!");
  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one objectMap uri/blank node found!");
  } else {
    // Assign the found source to source
    node_uri = temp_result[0];
  }

  // Get objectMap constant
  temp_result = find_matching_object(rml_triples, node_uri, RML_CONSTANT);
  if (temp_result.size() == 1) {
    objectMapInfo.constant = temp_result[0];
  }

  // Get objectMap template
  temp_result = find_matching_object(rml_triples, node_uri, RML_TEMPLATE);
  if (temp_result.size() == 1) {
    objectMapInfo.template_str = temp_result[0];
  }

  // Get objectMap reference
  temp_result = find_matching_object(rml_triples, node_uri, RML_REFERENCE);
  if (temp_result.size() == 1) {
    objectMapInfo.reference = temp_result[0];
  }

  // Get objectMap parentSource
  temp_result = find_matching_object(rml_triples, node_uri, PARENT_SOURCE);
  if (temp_result.size() == 1) {
    //  Check if source is a blanknode
    std::vector<std::string> temp_result2 = find_matching_object(rml_triples, temp_result[0], SD_NAME);
    if (temp_result2.empty()) {
      // Assign the found source to source
      objectMapInfo.parentSource = temp_result[0];
      objectMapInfo.parent_in_memory_name = "";
    } else {
      //  Handle in-memory structure
      objectMapInfo.parent_in_memory_name = temp_result2[0];
      objectMapInfo.parentSource = "";
    }
  }

  // Get objectMap parentSourceReferrence
  temp_result = find_matching_object(rml_triples, node_uri, PARENT_REFERENCE_FORMULATION);
  if (temp_result.size() == 1) {
    objectMapInfo.parentRef = temp_result[0];
  }

  // Get objectMap parent key
  temp_result = find_matching_object(rml_triples, node_uri, RML_PARENT);
  if (temp_result.size() == 1) {
    objectMapInfo.parent = temp_result[0];
  }

  // Get objectMap child key
  temp_result = find_matching_object(rml_triples, node_uri, RML_CHILD);
  if (temp_result.size() == 1) {
    objectMapInfo.child = temp_result[0];
  }

  // Get objectMap join_reference_condition
  temp_result = find_matching_object(rml_triples, node_uri, JOIN_REFERENCE_CONDITION);
  if (temp_result.size() == 1) {
    objectMapInfo.join_reference_condition_available = temp_result[0];
  }

  // Get objectMap data type
  temp_result = find_matching_object(rml_triples, node_uri, RML_DATA_TYPE_MAP);
  if (temp_result.size() == 1) {
    // Query for constant -> temp_result changes here!
    temp_result = find_matching_object(rml_triples, temp_result[0], RML_CONSTANT);
    if (temp_result.size() == 1) {
      objectMapInfo.dataType = temp_result[0];
    }
  }

  // Get objectMap language
  temp_result = find_matching_object(rml_triples, node_uri, RML_LANGUAGE_MAP);
  if (temp_result.size() == 1) {
    // Query for constant -> temp_result changes here!
    temp_result = find_matching_object(rml_triples, temp_result[0], RML_CONSTANT);
    if (temp_result.size() == 1) {
      std::string lang_tag = temp_result[0];
      // split en-US -> en
      std::size_t pos = temp_result[0].find_first_of('-');
      if (pos != std::string::npos) {
        lang_tag = temp_result[0].substr(0, pos);
      }

      // Check if value is supported
      if (valid_language_subtags.find(lang_tag) == valid_language_subtags.end()) {
        throw_error("Error: Language tag is not supported!");
      }

      objectMapInfo.language = lang_tag;
    }
  }

  // Set default value for object termType
  // TODO: Handle following logic:
  // If objectMap is reference based -> literal
  if (objectMapInfo.reference != "") {
    objectMapInfo.termType = LITERAL_TERM_TYPE;
  }
  // If objectMap is constant it can be IRI or literal
  else if (objectMapInfo.constant != "") {
    // Check if objectMapInfo.constant starts with "http"
    if (objectMapInfo.constant.substr(0, 4) == "http") {
      objectMapInfo.termType = IRI_TERM_TYPE;
    } else {
      objectMapInfo.termType = LITERAL_TERM_TYPE;
    }
  }
  // If objectMap has rml:languageMap and/or rr:language -> literal

  // If objectMap has rr:datatype -> literal

  // else -> IRI
  else {
    objectMapInfo.termType = IRI_TERM_TYPE;
  }

  // Get objectMap termType and overwrite default value
  temp_result = find_matching_object(rml_triples, node_uri, RML_TERM_TYPE);
  if (temp_result.size() == 1) {
    objectMapInfo.termType = temp_result[0];
  }

  return objectMapInfo;
}

/**
 * @brief Retrieves logical source data including reference formulation and source from a vector of NTriple objects.
 *
 * @param rml_triples Vector of NTriple objects containing the RDF triples to be searched.
 * @param tripleMap_node String representing the node in the RDF triples to be matched.
 * @return LogicalSourceInfo struct containing all found information.
 * and the source as the second element.
 *
 * @note This function will enter an infinite loop if any inconsistencies are found or if an unsupported
 * reference formulation is identified.
 */
LogicalSourceInfo extract_rml_info_of_source_data(const std::vector<NTriple>& rml_triples, const std::string& tripleMap_node) {
  std::vector<std::string> temp_result;  // Temporary vector to store intermediate results

  LogicalSourceInfo temp_logicalSourceInfo;  // Store gained information

  std::string logicalSource_uri;

  // Find matching object for logical source
  temp_result = find_matching_object(rml_triples, tripleMap_node, RML_LOGICAL_SOURCE);
  if (temp_result.empty()) {
    throw_error("Error: No logical source found!");
  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one logical source found!");
  } else {
    logicalSource_uri = temp_result[0];
  }

  // Find matching object for source
  temp_result = find_matching_object(rml_triples, logicalSource_uri, RML_SOURCE);
  if (temp_result.empty()) {
    throw_error("Error: No source specified!");
  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one source definition found!");
  } else {
    // Check if source is a blanknode
    std::vector<std::string> temp_result2 = find_matching_object(rml_triples, temp_result[0], SD_NAME);
    if (temp_result2.empty()) {
      // Assign the found source to source
      temp_logicalSourceInfo.source_path = temp_result[0];
      temp_logicalSourceInfo.in_memory_name = "";
    } else {
      //  Handle in-memory structure
      temp_logicalSourceInfo.in_memory_name = temp_result2[0];
      temp_logicalSourceInfo.source_path = "";
    }
  }

  temp_result = find_matching_object(rml_triples, logicalSource_uri, "http://semweb.mmlab.be/ns/rml#iterator");
  if (temp_result.empty()) {
    temp_logicalSourceInfo.logical_iterator = "";
  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one iterator definition found!");
  } else {
    // Assign the found source to source
    temp_logicalSourceInfo.logical_iterator = temp_result[0];
  }

  // Find matching object for reference formulation
  temp_result = find_matching_object(rml_triples, logicalSource_uri, RML_REFERENCE_FORMULATION);
  if (temp_result.empty()) {
    throw_error("Error: No reference_formulation found!");
  } else if (temp_result.size() > 1) {
    throw_error("Error: More than one reference_formulation found!");
  } else {
    temp_logicalSourceInfo.reference_formulation = temp_result[0];
  }
  // Return the reference_formulation and source as a pair
  return temp_logicalSourceInfo;
}

void parse_rml_rules(
    const std::vector<NTriple>& rml_triple,
    const std::vector<std::string>& tripleMap_nodes,
    const std::string& base_uri,
    std::vector<LogicalSourceInfo>& logicalSourceInfo_of_tripleMaps,
    std::vector<SubjectMapInfo>& subjectMapInfo_of_tripleMaps,
    std::vector<std::vector<PredicateMapInfo>>& predicateMapInfo_of_tripleMaps,
    std::vector<std::vector<ObjectMapInfo>>& objectMapInfo_of_tripleMaps,
    std::vector<std::vector<PredicateObjectMapInfo>>& predicateObjectMapInfo_of_tripleMaps) {
  // Handle all tripleMaps
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    // Initialize current tripleMap_node
    std::string tripleMap_node = tripleMap_nodes[i];

    ///////////////////////////////////////////////
    // Get information about the subject mapping
    ///////////////////////////////////////////////
    SubjectMapInfo subjectMapInfo = extract_rml_info_of_subjectMap(rml_triple, tripleMap_node);
    // Add base uri
    subjectMapInfo.base_uri = base_uri;
    subjectMapInfo_of_tripleMaps.push_back(subjectMapInfo);

    //////////////////////////
    //// Handle Predicate ////
    //////////////////////////

    // Get all predicateObjectMap Uris  of current tripleMap_node
    std::vector<std::string> predicateObjectMap_uris = find_matching_object(rml_triple, tripleMap_node, RML_PREDICATE_OBJECT_MAP);

    // Stores all extracted inforation of all predicateMaps in current tripleMap
    std::vector<PredicateMapInfo> predicateMapInfos;

    // Iterate over all found predicateObjectMap_uris and extract predicate
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      PredicateMapInfo predicateMapInfo = extract_rml_info_of_predicateMap(rml_triple, predicateObjectMap_uri);
      predicateMapInfos.push_back(predicateMapInfo);
    }
    // Add generated vector of predicateMaps in current tripleMap to vector outside
    predicateMapInfo_of_tripleMaps.push_back(predicateMapInfos);

    //////////////////////////////////////
    // Handle PredicateObjectMap graph //
    /////////////////////////////////////
    std::vector<PredicateObjectMapInfo> predicateObjectMapInfos;

    // Iterate over all found predicateObjectMap_uris and extract predicate
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      PredicateObjectMapInfo predicateObjectMapInfo = extract_rml_info_of_predicateObjectMap(rml_triple, predicateObjectMap_uri);
      predicateObjectMapInfos.push_back(predicateObjectMapInfo);
    }
    // Add generated vector of predicateMaps in current tripleMap to vector outside
    predicateObjectMapInfo_of_tripleMaps.push_back(predicateObjectMapInfos);

    ///////////////////////
    //// Handle Object ////
    ///////////////////////

    // Stores all extracted inforation of all objectMaps in current tripleMap
    std::vector<ObjectMapInfo> objectMapInfos;

    // Iterate over all found objectObjectMap_uris and extract predicate
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      ObjectMapInfo objectMapInfo = extract_rml_info_of_objectMap(rml_triple, predicateObjectMap_uri);
      objectMapInfos.push_back(objectMapInfo);
    }
    // Add generated vector of objectMaps in current tripleMap to vector outside
    objectMapInfo_of_tripleMaps.push_back(objectMapInfos);

    ///////////////////////////////////////////////
    // Get meta data of mapping
    ///////////////////////////////////////////////

    LogicalSourceInfo logicalSourceInfo = extract_rml_info_of_source_data(rml_triple, tripleMap_node);
    logicalSourceInfo_of_tripleMaps.push_back(logicalSourceInfo);
  }
}