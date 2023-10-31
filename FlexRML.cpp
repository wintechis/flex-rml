#include "FlexRML.h"

// Counter for generated blank nodes
int blank_node_counter = 18956;

bool get_index_of_element(const std::vector<std::string>& vec, const std::string& value, uint& index) {
  auto it = std::find(vec.begin(), vec.end(), value);
  if (it != vec.end()) {
    index = it - vec.begin();
    return true;
  } else {
    return false;
  }
}

/**
 * @brief Fills in the template provided in RML rule.
 *
 * @param template_str String containing template.
 * @param split_data Vector containing data from the input source.
 * @param split_header Vector containing header information.
 * @param term_type Optional parameter specifying the term type.
 *
 * @return A pair containing the filled template and the last generated value.
 *         If a field is empty, the filled template is an empty string.
 *
 */
std::pair<std::string, std::string> fill_in_template(
    const std::string& template_str,
    const std::vector<std::string>& split_data,
    const std::vector<std::string>& split_header,
    const std::string& term_type = "") {
  // Stores the queries used to find data in source e.g. table name, XPath, JsonPath, ...
  std::vector<std::string> query_strings = extract_substrings(template_str);
  // Stores queries enclosed in { } to replace in template string
  std::vector<std::string> query_strings_in_braces = enclose_in_braces(query_strings);

  std::string filled_template_str = template_str;
  std::string generated_value_last = "";  // Used to store the last generated value

  // Iterate over found elements
  for (size_t i = 0; i < query_strings.size(); i++) {
    // get index of element
    uint index = 0;
    if (!get_index_of_element(split_header, query_strings[i], index)) {
      throw_error("Error: Element not found in csv.");
    }

    // Get data
    generated_value_last = split_data[index];

    // Check if field is empty
    if (generated_value_last.empty()) {
      return std::make_pair("", "");
    }

    // If value is of type IRI -> It must be made URL safe
    if (term_type == IRI_TERM_TYPE) {
      generated_value_last = url_encode(generated_value_last);
    }

    // Fill entry
    filled_template_str = replace_substring(filled_template_str, query_strings_in_braces[i], generated_value_last);
  }

  return std::make_pair(filled_template_str, generated_value_last);
}

/////////////////////////////////
/////// INDEX GENERATION ///////
/////////////////////////////////

/**
 * @brief Creates an index map where each key is a value from the specified column in a CSV file and the corresponding value is the stream position of that line in the file.
 *
 * @param filePath The path to the CSV file.
 * @param columnIndex The 0-based index of the column from which the keys for the map will be extracted.
 *
 * @return Returns an unordered map with the values from the specified column as keys and the corresponding stream positions as values.
 */
std::unordered_map<std::string, std::streampos> createIndex(const std::string& filePath, u_int columnIndex) {
  std::unordered_map<std::string, std::streampos> indexMap;
  std::ifstream csvFile(filePath, std::ios::in | std::ios::binary);

  std::string line;
  std::streampos position = csvFile.tellg();

  while (std::getline(csvFile, line)) {
    std::vector<std::string> split_data = split_csv_line(line, ',');
    if (split_data.size() > columnIndex) {
      indexMap[split_data[columnIndex]] = position;
    }
    position = csvFile.tellg();
  }

  csvFile.close();
  return indexMap;
}

std::unordered_map<std::string, std::streampos> createIndexFromCSVString(const std::string& csvString, unsigned int columnIndex) {
  std::unordered_map<std::string, std::streampos> indexMap;
  std::istringstream csvStream(csvString);

  std::string line;
  std::streampos position = csvStream.tellg();

  while (std::getline(csvStream, line)) {
    std::vector<std::string> split_data = split_csv_line(line, ',');
    if (split_data.size() > columnIndex) {
      indexMap[split_data[columnIndex]] = position;
    }
    position = csvStream.tellg();
  }

  return indexMap;
}

///////////////////////////////////////////
/////// TRIPLES MAP SIZE ESTIMATION ///////
///////////////////////////////////////////

std::string generate_object_with_hash_join_ex(const ObjectMapInfo& objectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header, CsvReader& reader, const std::unordered_map<std::string, std::streampos>& parent_file_index) {
  std::string generated_object = "";
  // Check if template is available
  if (objectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating object based on template...");

    std::pair<std::string, std::string> result = fill_in_template(objectMapInfo.template_str, split_data, split_header);
    generated_object = result.first;
    std::string generate_value = result.second;

    // GENERATE VALUE
    uint index_old = 0;
    if (!get_index_of_element(split_header, objectMapInfo.child, index_old)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string childValue = split_data[index_old];
    if (parent_file_index.find(childValue) != parent_file_index.end()) {
      std::streampos rowPosition;
      try {
        rowPosition = parent_file_index.at(childValue);
      } catch (const std::out_of_range&) {
        return "";  // Element not found
      }
      reader.seekg(rowPosition);

      std::string next_element;
      reader.readNext(next_element);
      std::vector<std::string> split_data_ref = split_csv_line(next_element, ',');
    } else {
      return "";  // Element not found
    }
  }

  return generated_object;
}

std::string generate_subsample(std::string& file_path, float p) {
  std::string result;

  // Create a random number generator
  std::random_device rd;
  std::mt19937 mt(rd());

  // Create a distribution from 0 to 1
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  CsvReader reader(file_path);
  std::string next_element;

  // Get header
  reader.readNext(next_element);
  result += next_element + "\n";

  // Read all lines and decide if they should be added to the result
  while (reader.readNext(next_element)) {
    // Generate a random float
    float randomFloat = dist(mt);
    if (randomFloat <= p) {
      result += next_element + "\n";
    }
  }
  reader.close();

  return result;
}

int estimate_join_size(std::string& source, std::string& source_parent, ObjectMapInfo& objectMapInfo) {
  float p1 = 0.05;
  float p2 = 0.05;

  // Step 1 : Create Bernoulli samples S1 and S2 from tables T1 and T2
  std::string sampled_table1 = generate_subsample(source, p1);
  std::string sampled_table2 = generate_subsample(source_parent, p2);

  // Step 2 : Compute the join size J' of the two samples

  // Load child data
  int join_element_counter = 0;
  CsvReader reader(sampled_table1, false);

  // Get header
  std::string header;
  reader.readNext(header);

  std::vector<std::string> split_header = split_csv_line(header, ',');

  std::string next_element;

  if (objectMapInfo.parentSource == "") {
    return 0;
  }
  // Load data of parent source
  CsvReader reader_parent(sampled_table2, false);

  // Get Header
  std::string header_ref;
  reader_parent.readNext(header_ref);

  // Split header
  std::vector<std::string> split_header_ref = split_csv_line(header_ref, ',');

  // Get index of element in header
  u_int index = 0;
  if (!get_index_of_element(split_header_ref, objectMapInfo.parent, index)) {
    throw_error("Element not found -> Join not working!");
  }

  auto string_index = createIndexFromCSVString(sampled_table2, index);

  while (reader.readNext(next_element)) {
    reader_parent.reset();
    std::vector<std::string> split_data = split_csv_line(next_element, ',');
    auto res = generate_object_with_hash_join_ex(objectMapInfo, split_data, split_header, reader_parent, string_index);
    if (res != "") {
      join_element_counter += 1;
    }
  }
  reader_parent.close();
  reader.close();

  float scaling_factor = 1 / (p1 * p2);

  float j_hat = join_element_counter * scaling_factor;

  int j_hat_int = j_hat;
  return j_hat_int;
}

int estimate_generated_triple(std::vector<NTriple>& triples) {
  // count predicate object maps for each triple
  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(triples, RDF_TYPE, TRIPLES_MAP);

  int result = 0;

  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get logical source
    std::string logicalSourceNode = find_matching_object(triples, tripleMap_node, RML_LOGICAL_SOURCE)[0];
    // Get source path
    std::string source = find_matching_object(triples, logicalSourceNode, RML_SOURCE)[0];
    // Get reference formulation
    std::string referenceFormulation = find_matching_object(triples, logicalSourceNode, RML_REFERENCE_FORMULATION)[0];

    int objects_wo_join = 0;

    // Start at -1 to ignore header
    int element_count = -1;
    // Count number of elements in source
    if (referenceFormulation == CSV_REFERENCE_FORMULATION) {
      CsvReader reader(source);
      std::string next_element;

      while (reader.readNext(next_element)) {
        element_count++;
      }
      reader.close();
    }
    // Get all predicateObjectMap Uris  of current tripleMap_node
    std::vector<std::string> predicateObjectMap_uris = find_matching_object(triples, tripleMap_node, RML_PREDICATE_OBJECT_MAP);
    // Iterate over all found predicateObjectMap_uris and extract predicate
    for (const std::string& predicateObjectMap_uri : predicateObjectMap_uris) {
      // Get objectMap node
      std::string objectMap_node = find_matching_object(triples, predicateObjectMap_uri, RML_OBJECT_MAP)[0];

      // Get ex:parentSource
      std::vector<std::string> parentSource = find_matching_object(triples, objectMap_node, PARENT_SOURCE);

      if (parentSource.size() == 0) {
        // No join required
        objects_wo_join++;

      } else {
        // Join required

        // Get matching template
        std::string template_str = find_matching_object(triples, objectMap_node, RML_TEMPLATE)[0];

        // Get parent
        std::string parent = find_matching_object(triples, objectMap_node, RML_PARENT)[0];

        // Get child
        std::string child = find_matching_object(triples, objectMap_node, RML_CHILD)[0];

        ObjectMapInfo objectMapInfo;
        objectMapInfo.template_str = template_str;
        objectMapInfo.parent = parent;
        objectMapInfo.child = child;

        result += estimate_join_size(source, parentSource[0], objectMapInfo);
      }
    }

    // Estimate number of triples
    result += element_count * objects_wo_join;
  }
  return result;
}

//////////////////////////////////////
///// GRAPH GENERATOR FUNCTIONS /////
/////////////////////////////////////

/**
 * @brief Generates a graph as definied in the RML rules.
 *
 * @param graph_termType The type of the graph term (should ideally be 'IRI').
 * @param graph_template The template string used to generate the graph, if provided.
 * @param graph_constant The constant value for the graph, if provided.
 * @param split_data Data that's been pre-processed and split.
 * @param split_header Header information corresponding to the split_data.
 *
 * @return A string containing the generated graph. It can return a blank string, a processed
 *         string based on template or constant, or a default value (NO_GRAPH).
 */
std::string generate_graph_logic(
    const std::string& graph_termType,
    const std::string& graph_template,
    const std::string& graph_constant,
    const std::vector<std::string>& split_data,
    const std::vector<std::string>& split_header) {
  // Ensure the provided termType is supported (only 'IRI' is supported).
  if (graph_termType != IRI_TERM_TYPE) {
    throw_error("Error: Unsupported graph termType - termType of graph can only be 'IRI'.");
  }

  // If a graph template is provided, process and return the graph based on the template.
  if (!graph_template.empty()) {
    logln_debug("Generating graph based on template...");

    std::pair<std::string, std::string> result = fill_in_template(graph_template, split_data, split_header, IRI_TERM_TYPE);
    std::string current_graph = result.first;

    if (current_graph.empty()) {
      return "";
    }
    return handle_term_type(IRI_TERM_TYPE, current_graph);
  } else if (!graph_constant.empty()) {
    // Check if the provided constant refers to the default graph.
    if (graph_constant == DEFAULT_GRAPH) {
      return NO_GRAPH;
    }
    logln_debug("Generating graph based on constant...");
    return handle_term_type(IRI_TERM_TYPE, graph_constant);
  }

  // If no template or constant is provided, return the default value.
  return NO_GRAPH;
}

// Calls generate graph if input is SubjectMapInfo
std::string generate_graph(const SubjectMapInfo& subjectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header) {
  return generate_graph_logic(subjectMapInfo.graph_termType, subjectMapInfo.graph_template, subjectMapInfo.graph_constant, split_data, split_header);
}

// Calls generate graph if input is PredicateObjectMapInfo
std::string generate_graph(const PredicateObjectMapInfo& predicateObjectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header) {
  return generate_graph_logic(predicateObjectMapInfo.graph_termType, predicateObjectMapInfo.graph_template, predicateObjectMapInfo.graph_constant, split_data, split_header);
}

/////////////////////////////////////////
///// SUBJECT GENERATOR FUNCTIONS //////
////////////////////////////////////////

/**
 * @brief Generates a subject as definied in the RML rules.
 *
 * @param subjectMapInfo SubjectMapInfo containing RML rule mapping information.
 * @param format String containing the reference formulation / format of the data source.
 * @param line_nr Integer specifing the line number in the input data.
 *
 * @return std::string String containing the processed subject.
 *
 */
std::string generate_subject(const SubjectMapInfo& subjectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header) {
  std::string current_generated_subject = "";

  // Check if term type is supported on subject -> only blank node and IRI
  if (subjectMapInfo.termType != IRI_TERM_TYPE && subjectMapInfo.termType != "http://www.w3.org/ns/r2rml#BlankNode") {
    throw_error("Error: termType 'literal' is not supported in subject position.");
  }

  // Check if template is available
  if (subjectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating subject based on template...");
    std::pair<std::string, std::string> result = fill_in_template(subjectMapInfo.template_str, split_data, split_header, IRI_TERM_TYPE);
    current_generated_subject = result.first;
    // If result is empty string -> no value available -> return
    if (current_generated_subject == "") {
      return "";
    }

    // Check if termtype is IRI
    if (subjectMapInfo.termType == IRI_TERM_TYPE) {
      // Check if template is a valid uri -> e.g. starts with http
      if (current_generated_subject.substr(0, 7) != "http://" && current_generated_subject.substr(0, 8) != "https://") {
        // Generate uri using base
        current_generated_subject = subjectMapInfo.base_uri + current_generated_subject;
      }
    }

  }
  // Check if reference value is available
  else if (subjectMapInfo.reference != "") {
    logln_debug("Generating subject based on reference...");
    std::string temp_template = "{" + subjectMapInfo.reference + "}";
    std::pair<std::string, std::string> result = fill_in_template(temp_template, split_data, split_header);
    current_generated_subject = result.first;
    // If result is empty string -> no value available -> return
    if (current_generated_subject == "") {
      return "";
    }
    // Check if template is a valid uri -> e.g. starts with http
    if (current_generated_subject.substr(0, 4) != "http") {
      // Generate uri using base
      current_generated_subject = subjectMapInfo.base_uri + current_generated_subject;
    }
  }
  // Check if constant value is available
  else if (subjectMapInfo.constant != "") {
    // Set constant value as subject
    logln_debug("Generating subject based on constant...");
    current_generated_subject = subjectMapInfo.constant;
  }

  // Hanlde term type
  return handle_term_type(subjectMapInfo.termType, current_generated_subject);
}

//////////////////////////////////////////
///// PREDICATE GENERATOR FUNCTIONS //////
/////////////////////////////////////////

/**
 * @brief Generates a predicate as definied in the RML rules.
 *
 * @param predicateMapInfo PredicateMapInfo containing RML rule mapping information.
 * @param format String containing the reference formulation / format of the data source.
 * @param line_nr Integer specifing the line number in the input data.
 *
 * @return std::string String containing the processed predicate.
 *
 */
std::string generate_predicate(const PredicateMapInfo& predicateMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header) {
  std::string current_predicate = "";
  // Check if template is available
  if (predicateMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating predicate based on template...");
    std::pair<std::string, std::string> result = fill_in_template(predicateMapInfo.template_str, split_data, split_header);
    current_predicate = result.first;
    // If result is empty string -> no value available -> return
    if (current_predicate == "") {
      return "";
    }
  }
  // Check if constant value is available
  else if (predicateMapInfo.constant != "") {
    // Set constant value as subject
    logln_debug("Generating predicate based on constant...");
    current_predicate = predicateMapInfo.constant;
  }
  // TODO: Handle reference -> And find out if needed?

  // Hanlde term type -> predicate is always IRI
  return handle_term_type(IRI_TERM_TYPE, current_predicate);
}

////////////////////////////////////////
///// OBJECT GENERATOR FUNCTIONS //////
///////////////////////////////////////

std::string generate_object_with_nested_loop_join(const ObjectMapInfo& objectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header, CsvReader& reader) {
  std::string generated_object = "";
  // Check if template is available
  if (objectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating object based on template...");

    std::pair<std::string, std::string> result = fill_in_template(objectMapInfo.template_str, split_data, split_header);
    generated_object = result.first;
    std::string generate_value = result.second;
    // Reset file
    reader.reset();

    // Get Header
    std::string header_ref;
    reader.readNext(header_ref);

    // Split header
    std::vector<std::string> split_header_ref = split_csv_line(header_ref, ',');

    // Get index of element in header
    uint index = 0;
    if (!get_index_of_element(split_header_ref, objectMapInfo.parent, index)) {
      throw_error("Element not found -> Join not working!");
    }

    // GENERATE VALUE
    uint index_old = 0;
    if (!get_index_of_element(split_header, objectMapInfo.child, index_old)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string next_element;
    // Flag to indicate if the element was found
    bool found_element = false;
    while (reader.readNext(next_element)) {
      std::vector<std::string> split_data_ref = split_csv_line(next_element, ',');
      if (split_data_ref[index] == split_data[index_old]) {
        // If column name is found exit. And add triple
        found_element = true;
        break;
      }
    }

    // If the element is not found in the
    if (!found_element) {
      return "";
    }

    // If result is empty string -> no value available -> return
    if (generated_object == "") {
      return "";
    }
  }
  // Check if constant value is available
  else if (objectMapInfo.constant != "") {
    // Set constant value as subject
    logln_debug("Generating object based on constant...");
    generated_object = objectMapInfo.constant;
  }
  // Check if reference is available
  else if (objectMapInfo.reference != "") {
    logln_debug("Generating object based on reference...");
    std::string temp_template = "{" + objectMapInfo.reference + "}";
    std::pair<std::string, std::string> result = fill_in_template(temp_template, split_data, split_header);
    generated_object = result.first;
    // If result is empty string -> no value available -> return
    if (generated_object == "") {
      return "";
    }
  }

  generated_object = handle_term_type(objectMapInfo.termType, generated_object);

  // Handle language info
  if (objectMapInfo.language != "" && objectMapInfo.termType == LITERAL_TERM_TYPE) {
    generated_object = generated_object + "@" + objectMapInfo.language;
  }

  return generated_object;
}

std::string generate_object_with_hash_join(const ObjectMapInfo& objectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header, CsvReader& reader, const std::unordered_map<std::string, std::streampos>& parent_file_index) {
  std::string generated_object = "";
  // Check if template is available
  if (objectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating object based on template...");

    std::pair<std::string, std::string> result = fill_in_template(objectMapInfo.template_str, split_data, split_header);
    generated_object = result.first;
    std::string generate_value = result.second;

    // GENERATE VALUE
    uint index_old = 0;
    if (!get_index_of_element(split_header, objectMapInfo.child, index_old)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string childValue = split_data[index_old];
    if (parent_file_index.find(childValue) != parent_file_index.end()) {
      std::streampos rowPosition;
      try {
        rowPosition = parent_file_index.at(childValue);
      } catch (const std::out_of_range&) {
        return "";  // Element not found
      }
      reader.seekg(rowPosition);

      std::string next_element;
      reader.readNext(next_element);
      std::vector<std::string> split_data_ref = split_csv_line(next_element, ',');
    } else {
      return "";  // Element not found
    }

    // If result is empty string -> no value available -> return
    if (generated_object == "") {
      return "";
    }
  }
  // Check if constant value is available
  else if (objectMapInfo.constant != "") {
    // Set constant value as subject
    logln_debug("Generating object based on constant...");
    generated_object = objectMapInfo.constant;
  }
  // Check if reference is available
  else if (objectMapInfo.reference != "") {
    logln_debug("Generating object based on reference...");
    std::string temp_template = "{" + objectMapInfo.reference + "}";
    std::pair<std::string, std::string> result = fill_in_template(temp_template, split_data, split_header);
    generated_object = result.first;
    // If result is empty string -> no value available -> return
    if (generated_object == "") {
      return "";
    }
  }

  generated_object = handle_term_type(objectMapInfo.termType, generated_object);

  //// NOTE: Datatype is more important than language!!! ////
  if (objectMapInfo.dataType == "") {
    // Handle language info
    if (objectMapInfo.language != "" && objectMapInfo.termType == LITERAL_TERM_TYPE) {
      generated_object = generated_object + "@" + objectMapInfo.language;
    }
  } else {
    generated_object = generated_object + "^^<" + objectMapInfo.dataType + ">";
  }

  return generated_object;
}

/**
 * @brief Generates an object as definied in the RML rules without joins.
 *
 * @param objectMapInfo ObjectMapInfo containing RML rule mapping information.
 * @param format String containing the reference formulation / format of the data source.
 * @param line_nr Integer specifing the line number in the input data.
 *
 * @return std::string String containing the processed object.
 *
 */
std::string generate_object_wo_join(const ObjectMapInfo& objectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header) {
  std::string generated_object = "";
  // Check if template is available
  if (objectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating object based on template...");
    std::pair<std::string, std::string> result = fill_in_template(objectMapInfo.template_str, split_data, split_header);
    generated_object = result.first;
    // If result is empty string -> no value available -> return
    if (generated_object == "") {
      return "";
    }
  }
  // Check if constant value is available
  else if (objectMapInfo.constant != "") {
    // Set constant value as subject
    logln_debug("Generating object based on constant...");
    generated_object = objectMapInfo.constant;
  }
  // Check if reference is available
  else if (objectMapInfo.reference != "") {
    logln_debug("Generating object based on reference...");
    std::string temp_template = "{" + objectMapInfo.reference + "}";
    std::pair<std::string, std::string> result = fill_in_template(temp_template, split_data, split_header);
    generated_object = result.first;
    // If result is empty string -> no value available -> return
    if (generated_object == "") {
      return "";
    }
  }

  generated_object = handle_term_type(objectMapInfo.termType, generated_object);

  //// NOTE: Datatype is more important than language!!! ////
  if (objectMapInfo.dataType == "") {
    // Handle language info
    if (objectMapInfo.language != "" && objectMapInfo.termType == LITERAL_TERM_TYPE) {
      generated_object = generated_object + "@" + objectMapInfo.language;
    }
  } else {
    generated_object = generated_object + "^^<" + objectMapInfo.dataType + ">";
  }

  return generated_object;
}

// function to call correct generate object function depending if join is required
std::string generate_object(const ObjectMapInfo& objectMapInfo, const std::vector<std::string>& split_data, const std::vector<std::string>& split_header, std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>>& parent_file_index) {
  // Check if a join is required
  if (objectMapInfo.parentSource != "") {
    // Load data of parent source
    CsvReader reader(objectMapInfo.parentSource);

    // auto res = generate_object_with_nested_loop_join(objectMapInfo, split_data, split_header, reader);
    auto res = generate_object_with_hash_join(objectMapInfo, split_data, split_header, reader, parent_file_index.at(objectMapInfo.parentSource));

    reader.close();
    // return generate_object_with_hash_join(objectMapInfo, split_data, split_header, reader, parent_file_index.at(objectMapInfo.parentSource));
    return res;

  } else {
    // No join required

    return generate_object_wo_join(objectMapInfo, split_data, split_header);
  }
}

////////////////////////////////
////// MAPPING FUNCTIONS ///////
////////////////////////////////

void generate_quads(
    std::unordered_set<NQuad>& generated_quads,
    const SubjectMapInfo& subjectMapInfo,
    const std::vector<PredicateObjectMapInfo>& predicateObjectMapInfo_vec,
    const std::vector<PredicateMapInfo>& predicateMapInfo_vec,
    const std::vector<ObjectMapInfo>& objectMapInfo_vec,
    const std::string& line_data,
    const std::vector<std::string>& split_header,
    std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>>& parent_file_index) {
  // Parse data for this iteration

  std::vector<std::string> split_data = split_csv_line(line_data, ',');

  // store subject of current iteration
  std::string current_generated_subject = "";

  // store graph of current iteration
  std::string current_graph = "";

  /////////////////////
  // Generate Graph //
  ///////////////////

  current_graph = generate_graph(subjectMapInfo, split_data, split_header);
  // If current_graph is "" -> No value in input data
  if (current_graph == "") {
    // Generatin finished
    return;
  }
  // If current_graph is NO_GRAPH -> no graph has been generated -> set to ""
  else if (current_graph == NO_GRAPH) {
    current_graph = "";
  }
  ///////////////////////
  // Generate Subject //
  /////////////////////

  // Generate subject
  current_generated_subject = generate_subject(subjectMapInfo, split_data, split_header);

  // If current_generated_subject is "" -> Invalid IRI detected or no value in input data
  if (current_generated_subject == "") {
    // Generatin finished
    return;
  }

  // Add all available classes
  for (const std::string& subject_class : subjectMapInfo.classes) {
    // Generate quad for subject_class
    NQuad temp_quad;

    // Set subject to generated subject
    temp_quad.subject = current_generated_subject;
    // Set predicate which is constant
    temp_quad.predicate = "<http://www.w3.org/1999/02/22-rdf-syntax-ns#type>";
    // Set object which is the class
    temp_quad.object = "<" + subject_class + ">";  // Class is always IRI
    // Set graph to generated graph
    temp_quad.graph = current_graph;

    // Add generated quad to result
    generated_quads.insert(temp_quad);
  }

  // Iterate over all predicateObjectMaps
  for (size_t k = 0; k < predicateMapInfo_vec.size(); k++) {
    // Generate quad for finalized triple
    NQuad temp_quad;

    // add subject
    temp_quad.subject = current_generated_subject;

    // add graph from subject
    temp_quad.graph = current_graph;

    /////////////////////////
    // Generate Predicate //
    ///////////////////////
    temp_quad.predicate = generate_predicate(predicateMapInfo_vec[k], split_data, split_header);
    // If predicate is "" -> No value in input data
    if (temp_quad.predicate == "") {
      // Generatin finished
      continue;
    }

    //////////////////////
    // Generate Object //
    /////////////////////

    temp_quad.object = generate_object(objectMapInfo_vec[k], split_data, split_header, parent_file_index);
    // If current_graph is "" -> No value in input data
    if (temp_quad.object == "") {
      // Generatin finished
      continue;
    }

    // Add generated quad to result
    generated_quads.insert(temp_quad);

    ///////////////////////////////
    // Generate additional graph //
    //////////////////////////////

    // Check if graph is available inside of predicateObjectMap -> If so add another triple
    if (predicateObjectMapInfo_vec[k].graph_constant != "" || predicateObjectMapInfo_vec[k].graph_template != "") {
      // add graph if available -> else use subject graph
      temp_quad.graph = generate_graph(predicateObjectMapInfo_vec[k], split_data, split_header);
      // If current_graph is NO_GRAPH -> no graph has been generated -> set to ""
      if (temp_quad.graph == NO_GRAPH) {
        temp_quad.graph = "";
      }

      generated_quads.insert(temp_quad);
    }
  }
}

std::unordered_set<NQuad> map_data(std::string& rml_rule, const std::string& input_data) {
  ///////////////////////////////////
  //// STEP 1: Read RDF triples ////
  //////////////////////////////////
  std::string base_uri;
  std::vector<NTriple> rml_triple;
  read_and_prepare_rml_triple(rml_rule, rml_triple, base_uri);

  /////////////////////////////////
  //// STEP 2: Parse RML rules ////
  /////////////////////////////////

  // Get all tripleMaps
  std::vector<std::string>
      tripleMap_nodes = find_matching_subject(rml_triple, RDF_TYPE, TRIPLES_MAP);

  // Store all logicalSourceInfos of all tripleMaps
  std::vector<LogicalSourceInfo> logicalSourceInfo_of_tripleMaps;

  // Store all subjectMapInfos of all tripleMaps
  std::vector<SubjectMapInfo> subjectMapInfo_of_tripleMaps;
  // Store all predicateMapInfos of all tripleMaps
  std::vector<std::vector<PredicateMapInfo>> predicateMapInfo_of_tripleMaps;
  // Store all objectMapInfos of all tripleMaps
  std::vector<std::vector<ObjectMapInfo>> objectMapInfo_of_tripleMaps;
  // Store all predicateObjectMapInfos of all tripleMaps
  std::vector<std::vector<PredicateObjectMapInfo>> predicateObjectMapInfo_of_tripleMaps;

  // Parse rml rule in definied data structures
  parse_rml_rules(
      rml_triple,
      tripleMap_nodes,
      base_uri,
      logicalSourceInfo_of_tripleMaps,
      subjectMapInfo_of_tripleMaps,
      predicateMapInfo_of_tripleMaps,
      objectMapInfo_of_tripleMaps,
      predicateObjectMapInfo_of_tripleMaps);

  // Shrink to fit all vectors
  logicalSourceInfo_of_tripleMaps.shrink_to_fit();
  subjectMapInfo_of_tripleMaps.shrink_to_fit();
  predicateMapInfo_of_tripleMaps.shrink_to_fit();
  objectMapInfo_of_tripleMaps.shrink_to_fit();
  predicateObjectMapInfo_of_tripleMaps.shrink_to_fit();

  logln_debug("Finished parsing RML rules.");

  ///////////////////////////////////////////////////////
  //// STEP 3: Generate Mapping based on RML rules ////
  /////////////////////////////////////////////////////

  // Stores generate quads
  std::unordered_set<NQuad> generated_quads;

  // Generate mapping for each tripleMap
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    // Initialize current tripleMap_node
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get specified format of source file
    std::string current_logical_source_format = logicalSourceInfo_of_tripleMaps[i].reference_formulation;

    // Get path of specified source file
    std::string file_path = logicalSourceInfo_of_tripleMaps[i].source_path;

    // Handle CSV
    if (current_logical_source_format == CSV_REFERENCE_FORMULATION) {
      // Check if join is required

      // Load data of parent source if needed
      std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> parent_file_index;
      for (const auto& objectMap : objectMapInfo_of_tripleMaps[i]) {
        if (objectMap.parentSource == "") {
          continue;
        }
        // Load data of parent source
        CsvReader reader(objectMap.parentSource);

        // Get Header
        std::string header_ref;
        reader.readNext(header_ref);

        // Split header
        std::vector<std::string> split_header_ref = split_csv_line(header_ref, ',');

        // Get index of element in header
        u_int index = 0;
        if (!get_index_of_element(split_header_ref, objectMap.parent, index)) {
          throw_error("Element not found -> Join not working!");
        }

        parent_file_index[objectMap.parentSource] = createIndex(objectMap.parentSource, index);
      }

      // Load data
      CsvReader reader(file_path);

      // Get Header and split it
      std::string header;
      reader.readNext(header);
      std::vector<std::string> split_header = split_csv_line(header, ',');

      std::string next_element;
      while (reader.readNext(next_element)) {
        generate_quads(generated_quads, subjectMapInfo_of_tripleMaps[i], predicateObjectMapInfo_of_tripleMaps[i], predicateMapInfo_of_tripleMaps[i], objectMapInfo_of_tripleMaps[i], next_element, split_header, parent_file_index);
      }
    } else {
      throw_error("Error: Found unsupported encoding.");
    }
  }

  return generated_quads;
}

// Map directly to file -> only available on PC
#ifndef ARDUINO
///////////////////////////////////////////
//// MAP DATA TO FILE - SINGLE THREAD ////
/////////////////////////////////////////

void map_data_to_file(std::string& rml_rule, std::ofstream& outFile, bool remove_duplicates) {
  // Set dummy value for input data
  std::string input_data = "";

  // init hash function used to check duplicates
  std::hash<NQuad> hasher;
  // Used to store hashes
  std::unordered_set<uint64_t> nquad_hashes;

  //////////////////////////////////
  //// STEP 1: Read RDF triple ////
  /////////////////////////////////

  // Vector to store all provided rml_triples
  std::vector<NTriple> rml_triple;
  std::string base_uri;
  read_and_prepare_rml_triple(rml_rule, rml_triple, base_uri);

  auto start = std::chrono::high_resolution_clock::now();
  int res = estimate_generated_triple(rml_triple);
  auto end = std::chrono::high_resolution_clock::now();
  int duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  log("DURATION ");
  log(duration);
  logln(" ms");
  log("ESTIMATED NUMBER: ");
  logln(res);

  /////////////////////////////////
  //// STEP 2: Parse RML rules ////
  /////////////////////////////////

  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(rml_triple, RDF_TYPE, TRIPLES_MAP);

  // Store all logicalSourceInfos of all tripleMaps
  std::vector<LogicalSourceInfo> logicalSourceInfo_of_tripleMaps;

  // Store all subjectMapInfos of all tripleMaps
  std::vector<SubjectMapInfo> subjectMapInfo_of_tripleMaps;
  // Store all predicateMapInfos of all tripleMaps
  std::vector<std::vector<PredicateMapInfo>> predicateMapInfo_of_tripleMaps;
  // Store all objectMapInfos of all tripleMaps
  std::vector<std::vector<ObjectMapInfo>> objectMapInfo_of_tripleMaps;
  // Store all predicateObjectMapInfos of all tripleMaps
  std::vector<std::vector<PredicateObjectMapInfo>> predicateObjectMapInfo_of_tripleMaps;

  // Parse rml rule in definied data structures
  parse_rml_rules(
      rml_triple,
      tripleMap_nodes,
      base_uri,
      logicalSourceInfo_of_tripleMaps,
      subjectMapInfo_of_tripleMaps,
      predicateMapInfo_of_tripleMaps,
      objectMapInfo_of_tripleMaps,
      predicateObjectMapInfo_of_tripleMaps);

  // Shrink to fit all vectors
  logicalSourceInfo_of_tripleMaps.shrink_to_fit();
  subjectMapInfo_of_tripleMaps.shrink_to_fit();
  predicateMapInfo_of_tripleMaps.shrink_to_fit();
  objectMapInfo_of_tripleMaps.shrink_to_fit();
  predicateObjectMapInfo_of_tripleMaps.shrink_to_fit();

  logln_debug("Finished parsing RML rules.");

  ///////////////////////////////////////////////////////
  //// STEP 3: Generate Mapping based on RML rules ////
  /////////////////////////////////////////////////////
  // Stores generate quads
  std::unordered_set<NQuad> generated_quads;

  // Generate mapping for each tripleMap without join condition
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    // Initialize current tripleMap_node
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get specified format
    std::string current_logical_source_format = logicalSourceInfo_of_tripleMaps[i].reference_formulation;

    // Get specified source
    std::string file_path = logicalSourceInfo_of_tripleMaps[i].source_path;

    // Get specified iterator
    std::string logical_iterator = logicalSourceInfo_of_tripleMaps[i].logical_iterator;

    // Handle CSV
    if (current_logical_source_format == CSV_REFERENCE_FORMULATION) {
      std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> parent_file_index;

      for (const auto& objectMap : objectMapInfo_of_tripleMaps[i]) {
        if (objectMap.parentSource == "") {
          continue;
        }
        // Load data of parent source
        CsvReader reader(objectMap.parentSource);

        // Get Header
        std::string header_ref;
        reader.readNext(header_ref);

        // Split header
        std::vector<std::string> split_header_ref = split_csv_line(header_ref, ',');

        // Get index of element in header
        u_int index = 0;
        if (!get_index_of_element(split_header_ref, objectMap.parent, index)) {
          throw_error("Element not found -> Join not working!");
        }

        parent_file_index[objectMap.parentSource] = createIndex(objectMap.parentSource, index);

        reader.close();
      }

      // Load data
      CsvReader reader(file_path);

      // Get Header and split it
      std::string header;
      reader.readNext(header);
      std::vector<std::string> split_header = split_csv_line(header, ',');

      std::string next_element;
      while (reader.readNext(next_element)) {
        generate_quads(generated_quads, subjectMapInfo_of_tripleMaps[i], predicateObjectMapInfo_of_tripleMaps[i], predicateMapInfo_of_tripleMaps[i], objectMapInfo_of_tripleMaps[i], next_element, split_header, parent_file_index);

        // Write to file
        for (const NQuad& quad : generated_quads) {
          bool add_triple = true;

          // Check if value is a duplicate
          // TODO: Move outside of loop
          if (remove_duplicates) {
            uint64_t hash_of_quad = hasher(quad);
            auto result = nquad_hashes.insert(hash_of_quad);
            if (!result.second) {
              // hash_of_quad was already in the set
              add_triple = false;
            }
          }
          if (add_triple) {
            outFile << quad.subject << " "
                    << quad.predicate << " "
                    << quad.object << " ";

            if (quad.graph != "") {
              outFile << quad.graph << " .\n";
            } else {
              outFile << ".\n";
            }
          }
        }

        generated_quads.clear();
      }
    } else {
      throw_error("Error: Found unsupported encoding.");
    }
  }
}

/////////////////////////////////////////////
//// MAP DATA TO FILE - MULTIPLE THREAD ////
///////////////////////////////////////////

// Define a thread-safe queue for passing data between threads.
template <typename T>
class ThreadSafeQueue {
 private:
  // The underlying non-thread-safe queue
  std::queue<T> queue;

  // Mutex used to synchronize access to the queue
  std::mutex mtx;

  // Condition variable used for synchronization between threads
  std::condition_variable cv;

  // Flag to indicate that no more items will be pushed to the queue
  bool done = false;

 public:
  // Push an item to the queue and notify a waiting thread
  void push(const T& item) {
    std::unique_lock<std::mutex> lock(mtx);  // Lock the mutex
    queue.push(item);                        // Add the item to the queue
    cv.notify_one();                         // Notify one waiting thread
  }

  // Pop an item from the queue. If the queue is empty, it waits until an item is available or done flag is set
  bool pop(T& item) {
    std::unique_lock<std::mutex> lock(mtx);  // Lock the mutex
    while (queue.empty() && !done) {
      cv.wait(lock);  // Wait until an item is pushed or done flag is set
    }
    if (queue.empty() && done) return false;  // If queue is empty and done flag is set, return false
    item = queue.front();                     // Get the item from the front of the queue
    queue.pop();                              // Remove the item from the queue
    return true;                              // Indicate success
  }

  // Set the done flag and notify all waiting threads
  void mark_done() {
    std::unique_lock<std::mutex> lock(mtx);  // Lock the mutex
    done = true;                             // Set the done flag
    cv.notify_all();                         // Notify all waiting threads
  }

  // Check if the queue is empty
  bool isEmpty() {
    std::unique_lock<std::mutex> lock(mtx);  // Lock the mutex
    return queue.empty();
  }

  // Check the status of the done flag
  bool isDone() {
    std::unique_lock<std::mutex> lock(mtx);  // Lock the mutex
    return done;
  }
};

void process_triple_map(
    const SubjectMapInfo& subjectMapInfo_of_tripleMap,
    const std::vector<PredicateObjectMapInfo>& predicateObjectMapInfo_of_tripleMap,
    const std::vector<ObjectMapInfo>& objectMapInfo_of_tripleMap,
    const std::vector<PredicateMapInfo>& predicateMapInfo_of_tripleMap,
    const LogicalSourceInfo& logicalSourceInfo_of_tripleMap,
    ThreadSafeQueue<NQuad>& quadQueue) {
  // Store index of parent file
  std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> parent_file_index;
  // Stores generate quads
  std::unordered_set<NQuad> generated_quads;
  // Get specified source
  std::string file_path = logicalSourceInfo_of_tripleMap.source_path;

  // Get specified format
  std::string current_logical_source_format = logicalSourceInfo_of_tripleMap.reference_formulation;

  // Handle CSV
  if (current_logical_source_format == CSV_REFERENCE_FORMULATION) {
    for (const auto& objectMap : objectMapInfo_of_tripleMap) {
      if (objectMap.parentSource == "") {
        continue;
      }
      // Load data of parent source
      CsvReader reader(objectMap.parentSource);

      // Get Header
      std::string header_ref;
      reader.readNext(header_ref);

      // Split header
      std::vector<std::string> split_header_ref = split_csv_line(header_ref, ',');

      // Get index of element in header
      u_int index = 0;
      if (!get_index_of_element(split_header_ref, objectMap.parent, index)) {
        throw_error("Element not found -> Join not working!");
      }

      parent_file_index[objectMap.parentSource] = createIndex(objectMap.parentSource, index);

      reader.close();
    }

    // Load data
    CsvReader reader(file_path);

    // Get Header and split it
    std::string header;
    reader.readNext(header);
    std::vector<std::string> split_header = split_csv_line(header, ',');

    std::string next_element;
    while (reader.readNext(next_element)) {
      generate_quads(
          generated_quads,
          subjectMapInfo_of_tripleMap,
          predicateObjectMapInfo_of_tripleMap,
          predicateMapInfo_of_tripleMap,
          objectMapInfo_of_tripleMap,
          next_element,
          split_header,
          parent_file_index);

      // Write to file
      for (const NQuad& quad : generated_quads) {
        quadQueue.push(quad);
      }

      generated_quads.clear();
    }
    reader.close();
  } else {
    throw_error("Error: Found unsupported encoding.");
  }
}

void writerThread(std::ofstream& outFile, ThreadSafeQueue<NQuad>& quadQueue, bool remove_duplicates) {
  // Used to store hashes
  std::unordered_set<uint64_t> nquad_hashes;
  // init hash function used to check duplicates
  std::hash<NQuad> hasher;
  NQuad quad;
  while (true) {
    bool success = quadQueue.pop(quad);
    if (success) {
      bool add_triple = true;

      // Check if value is a duplicate
      // TODO: Move outside of loop
      if (remove_duplicates) {
        uint64_t hash_of_quad = hasher(quad);
        // If not found
        if (nquad_hashes.find(hash_of_quad) == nquad_hashes.end()) {
          // Add to hashes
          nquad_hashes.insert(hash_of_quad);
        } else {
          // Otherwise dont add triple
          add_triple = false;
        }
      }

      if (add_triple) {
        // Push to queue

        outFile << quad.subject << " " << quad.predicate << " " << quad.object << " ";
        if (quad.graph != "") {
          outFile << quad.graph << " .\n";
        } else {
          outFile << ".\n";
        }
      }
    } else {
      break;  // Exit the loop when no more data and the queue is marked done.
    }
  }

  log("Number of generated triples: ");
  logln(std::to_string(nquad_hashes.size()).c_str());
}

void map_data_to_file_threading(std::string& rml_rule, std::ofstream& outFile, bool remove_duplicates, uint8_t num_threads) {
  // Set dummy value for input data
  std::string input_data = "";

  //////////////////////////////////
  //// STEP 1: Read RDF triple ////
  /////////////////////////////////

  // Vector to store all provided rml_triples
  std::vector<NTriple> rml_triple;
  std::string base_uri;
  read_and_prepare_rml_triple(rml_rule, rml_triple, base_uri);

  /////////////////////////////////
  //// STEP 2: Parse RML rules ////
  /////////////////////////////////

  // Get all tripleMaps
  std::vector<std::string> tripleMap_nodes = find_matching_subject(rml_triple, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "http://www.w3.org/ns/r2rml#TriplesMap");

  // Store all logicalSourceInfos of all tripleMaps
  std::vector<LogicalSourceInfo> logicalSourceInfo_of_tripleMaps;

  // Store all subjectMapInfos of all tripleMaps
  std::vector<SubjectMapInfo> subjectMapInfo_of_tripleMaps;
  // Store all predicateMapInfos of all tripleMaps
  std::vector<std::vector<PredicateMapInfo>> predicateMapInfo_of_tripleMaps;
  // Store all objectMapInfos of all tripleMaps
  std::vector<std::vector<ObjectMapInfo>> objectMapInfo_of_tripleMaps;
  // Store all predicateObjectMapInfos of all tripleMaps
  std::vector<std::vector<PredicateObjectMapInfo>> predicateObjectMapInfo_of_tripleMaps;

  // Parse rml rule in definied data structures
  parse_rml_rules(
      rml_triple,
      tripleMap_nodes,
      base_uri,
      logicalSourceInfo_of_tripleMaps,
      subjectMapInfo_of_tripleMaps,
      predicateMapInfo_of_tripleMaps,
      objectMapInfo_of_tripleMaps,
      predicateObjectMapInfo_of_tripleMaps);

  // Shrink to fit all vectors
  logicalSourceInfo_of_tripleMaps.shrink_to_fit();
  subjectMapInfo_of_tripleMaps.shrink_to_fit();
  predicateMapInfo_of_tripleMaps.shrink_to_fit();
  objectMapInfo_of_tripleMaps.shrink_to_fit();
  predicateObjectMapInfo_of_tripleMaps.shrink_to_fit();

  logln_debug("Finished parsing RML rules.");

  ///////////////////////////////////////////////////////
  //// STEP 3: Generate Mapping based on RML rules ////
  /////////////////////////////////////////////////////

  // Determine the number of threads to use
  const size_t numThreads = (num_threads == 0) ? std::thread::hardware_concurrency() : static_cast<size_t>(num_threads);
  log_debug("Using ");
  log_debug(numThreads);
  logln_debug(" threads.");
  std::vector<std::thread> threads;

  ThreadSafeQueue<NQuad> quadQueue;

  // Start the writer thread before the worker threads
  std::thread writer(writerThread, std::ref(outFile), std::ref(quadQueue), remove_duplicates);
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    threads.push_back(std::thread(
        process_triple_map,
        std::ref(subjectMapInfo_of_tripleMaps[i]),
        std::ref(predicateObjectMapInfo_of_tripleMaps[i]),
        std::ref(objectMapInfo_of_tripleMaps[i]),
        std::ref(predicateMapInfo_of_tripleMaps[i]),
        std::ref(logicalSourceInfo_of_tripleMaps[i]),
        std::ref(quadQueue)));

    if (threads.size() == numThreads || i == tripleMap_nodes.size() - 1) {
      for (std::thread& t : threads) {
        t.join();
      }
      quadQueue.mark_done();
      threads.clear();
    }
  }

  writer.join();
}

#endif