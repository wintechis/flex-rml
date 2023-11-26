#include "FlexRML.h"

#define BATCH_SIZE 100

float gloabl_sampling_probability = 0.05;

// Counter for generated blank nodes
int blank_node_counter = 18956;

std::unordered_set<std::string> global_tokens_to_remove;

//////////////////////////////////////
///// HASH FUNCTIONS /////////////////
//////////////////////////////////////
#ifndef ARDUINO
struct uint128Hash {
  std::size_t operator()(const uint64_t &value) const {
    return std::hash<uint64_t>()(value);
  }
};

inline void hash_combine_128(uint128 &seed, uint128 value) {
  seed.first ^= value.first + (seed.second << 12) + (seed.second >> 4);
  seed.second ^= value.second + (seed.first << 12) + (seed.first >> 4);
}

inline void hash_combine_32(uint32_t &seed, uint32_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

uint128 performCity128Hash(const NQuad &quad) {
  uint128 h1 = CityHash128(quad.subject.c_str(), quad.subject.size());
  uint128 h2 = CityHash128(quad.predicate.c_str(), quad.predicate.size());
  uint128 h3 = CityHash128(quad.object.c_str(), quad.object.size());
  uint128 h4 = CityHash128(quad.graph.c_str(), quad.graph.size());

  // Combine hashes using the hash_combine method
  uint128 result = h1;
  hash_combine_128(result, h2);
  hash_combine_128(result, h3);
  hash_combine_128(result, h4);

  return result;
}

uint64_t performCity64Hash(const NQuad &quad) {
  uint64_t h1 = CityHash64(quad.subject.c_str(), quad.subject.size());
  uint64_t h2 = CityHash64(quad.predicate.c_str(), quad.predicate.size());
  uint64_t h3 = CityHash64(quad.object.c_str(), quad.object.size());
  uint64_t h4 = CityHash64(quad.graph.c_str(), quad.graph.size());

  // Combine hashes using the hash_combine method
  uint64_t result = h1;
  hash_combine_64(result, h2);
  hash_combine_64(result, h3);
  hash_combine_64(result, h4);

  return result;
}

uint32_t performCity32Hash(const NQuad &quad) {
  uint32_t h1 = CityHash32(quad.subject.c_str(), quad.subject.size());
  uint32_t h2 = CityHash32(quad.predicate.c_str(), quad.predicate.size());
  uint32_t h3 = CityHash32(quad.object.c_str(), quad.object.size());
  uint32_t h4 = CityHash32(quad.graph.c_str(), quad.graph.size());

  // Combine hashes using the hash_combine method
  uint32_t result = h1;
  hash_combine_32(result, h2);
  hash_combine_32(result, h3);
  hash_combine_32(result, h4);

  return result;
}
#endif

bool get_index_of_element(const std::vector<std::string> &vec, const std::string &value, uint &index) {
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
std::string fill_in_template(
    const std::string &template_str,
    const std::vector<std::string> &split_data,
    const std::vector<std::string> &split_header,
    const std::string &term_type = "") {
  std::vector<std::string> query_strings = extract_substrings(template_str);
  std::vector<std::string> query_strings_in_braces = enclose_in_braces(query_strings);

  std::string filled_template_str = template_str;
  std::string generated_value_last;

  for (size_t i = 0; i < query_strings.size(); ++i) {
    uint index = 0;
    if (!get_index_of_element(split_header, query_strings[i], index)) {
      throw_error("Error: Element not found in csv.");
    }

    generated_value_last = split_data[index];
    if (generated_value_last.empty()) {
      return "";
    }

    // Check if value is tokens_to_remove
    if (global_tokens_to_remove.find(generated_value_last) != global_tokens_to_remove.end()) {
      return "";
    }

    if (term_type == IRI_TERM_TYPE) {
      generated_value_last = url_encode(generated_value_last);
    }

    size_t pos = filled_template_str.find(query_strings_in_braces[i]);
    while (pos != std::string::npos) {
      filled_template_str.replace(pos, query_strings_in_braces[i].length(), generated_value_last);
      pos = filled_template_str.find(query_strings_in_braces[i], pos + generated_value_last.length());
    }
  }

  return filled_template_str;
}

/////////////////////////////////
/////// INDEX GENERATION ///////
/////////////////////////////////
#ifndef ARDUINO
/**
 * @brief Creates an index map where each key is a value from the specified column in a CSV file and the corresponding value is the stream position of that line in the file.
 *
 * @param filePath The path to the CSV file.
 * @param columnIndex The 0-based index of the column from which the keys for the map will be extracted.
 *
 * @return Returns an unordered map with the values from the specified column as keys and the corresponding stream positions as values.
 */
std::unordered_map<std::string, std::streampos> create_index(const std::string &file_path, const uint &column_index) {
  std::unordered_map<std::string, std::streampos> index_map;
  std::ifstream csv_file(file_path, std::ios::in | std::ios::binary);

  std::string line;
  std::streampos position = csv_file.tellg();

  while (std::getline(csv_file, line)) {
    std::vector<std::string> split_data = split_csv_line(line, ',');
    if (split_data.size() > column_index) {
      index_map[split_data[column_index]] = position;
    }
    position = csv_file.tellg();
  }

  csv_file.close();
  return index_map;
}

// Generats index of each column index in element indices not only of the column_index
std::unordered_map<std::string, std::vector<std::vector<std::string>>> create_index_full(
    const std::string &file_path,
    const uint &column_index,
    const std::vector<size_t> &element_indices) {
  std::unordered_map<std::string, std::vector<std::vector<std::string>>> indexMap;
  std::ifstream csv_file(file_path, std::ios::in | std::ios::binary);

  std::string line;
  while (std::getline(csv_file, line)) {
    std::vector<std::string> split_data = split_csv_line(line, ',');
    if (split_data.size() > column_index) {
      std::vector<std::string> selected_elements;
      for (auto index : element_indices) {
        if (index < split_data.size()) {
          selected_elements.push_back(split_data[index]);
        }
      }
      indexMap[split_data[column_index]].push_back(selected_elements);
    }
  }
  csv_file.close();
  return indexMap;
}

std::unordered_map<std::string, std::streampos> createIndexFromCSVString(const std::string &csvString, const uint &columnIndex) {
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

std::string generate_subsample(const std::string &file_path, const float &p) {
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

int estimate_join_size(
    const std::string &source_file_path,
    const std::string &parent_source_file_path,
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &subjectNames,
    const std::vector<std::string> &predicateNames,
    std::unordered_set<uint64_t> &seen_objects_triple_map) {
  //// Generate Objects ////
  // Create a random number generator
  std::random_device rd;
  std::mt19937 mt(rd());

  // Create a distribution from 0 to 1
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Sampling probabilities
  float p1 = gloabl_sampling_probability;
  float p2 = 1.0;

  // Store unique generated elements.
  std::unordered_set<uint64_t> seen_objects;

  // Step 1 : Create Bernoulli samples S1 and S2 from tables T1 and T2
  // std::string sampled_table_child = generate_subsample(source_file_path, p1);
  // std::string sampled_table_parent = generate_subsample(parent_source_file_path, p2);

  // Step 2 : Compute the join size J' of the two samples

  // Load child data
  CsvReader reader_child(source_file_path);

  // Get header of child
  std::string header_child;
  reader_child.readNext(header_child);
  std::vector<std::string> split_header_child = split_csv_line(header_child, ',');

  // Load data of parent source
  CsvReader reader_parent(parent_source_file_path);

  // Get Header
  std::string header_parent;
  reader_parent.readNext(header_parent);

  // Split header
  std::vector<std::string> split_header_parent = split_csv_line(header_parent, ',');

  // Get index of element in header
  uint index = 0;
  if (!get_index_of_element(split_header_parent, objectMapInfo.parent, index)) {
    throw_error("Element not found -> Join not working!");
  }

  std::unordered_map<std::string, std::streampos> parent_file_index;
  std::unordered_map<std::string, std::vector<std::vector<std::string>>> parent_file_index_full;
  std::vector<std::string> split_header_index;
  if (objectMapInfo.join_reference_condition_available == "true") {
    // Create index for hash join
    parent_file_index = create_index(parent_source_file_path, index);
  } else {
    // Get template
    std::vector<std::string> query_strings = extract_substrings(objectMapInfo.template_str);
    // Get index
    std::vector<size_t> element_index;
    for (size_t i = 0; i < query_strings.size(); ++i) {
      uint index = 0;
      if (!get_index_of_element(split_header_parent, query_strings[i], index)) {
        throw_error("Error: Element not found in csv.");
      }
      split_header_index.push_back(query_strings[i]);
      element_index.push_back(index);
    }
    parent_file_index_full = create_index_full(parent_source_file_path, index, element_index);
  }

  //// Generate Subject ////
  // Get index of elements in header for subject
  std::vector<uint> indices_subject;
  for (auto &element : subjectNames) {
    if (element[0] == '{') {
      break;
    }
    uint index;
    if (!get_index_of_element(split_header_child, element, index)) {
      throw_error("Element not found -> Estimation not working!");
    }
    indices_subject.push_back(index);
  }

  // Get index of elements in header for predicate
  std::vector<uint> indices_predicates;
  for (auto &element : predicateNames) {
    // Means that constant is used
    if (element[0] == '{') {
      break;
    }
    uint index;
    if (!get_index_of_element(split_header_child, element, index)) {
      throw_error("Element not found -> Estimation not working!");
    }
    indices_predicates.push_back(index);
  }

  std::string next_element;
  while (reader_child.readNext(next_element)) {
    // Generate a random float
    float randomFloat = dist(mt);
    if (randomFloat > p1) {
      continue;
    }
    std::vector<std::string> split_data = split_csv_line(next_element, ',');

    // Generated Data
    std::string data = "";
    // Add subject
    if (indices_subject.size() == 0) {
      data += subjectNames[0];
    } else {
      for (auto &index : indices_subject) {
        data += split_data[index];
        data += ",";
      }
    }

    // Add predicate
    if (indices_predicates.size() == 0) {
      data += predicateNames[0];
    } else {
      for (auto &index : indices_predicates) {
        data += split_data[index];
        data += ",";
      }
    }
    // Generate object
    std::vector<std::string> join_results;
    if (objectMapInfo.join_reference_condition_available == "true") {
      std::string join_result = generate_object_with_hash_join(objectMapInfo, split_data, split_header_child, reader_parent, parent_file_index);
      join_results.push_back(join_result);
    } else {
      join_results = generate_object_with_hash_join_full(objectMapInfo, split_data, split_header_child, split_header_index, parent_file_index_full);
    }

    std::string data_new;
    data_new.reserve(1000);
    for (const auto &join_result : join_results) {
      if (join_result == "") {
        continue;
      }
      data_new = data + join_result;

      // Hash data
      uint64_t hashed_data = CityHash64(data_new.c_str(), data_new.size());

      // Add only if not yet seen
      auto triple_map_result = seen_objects_triple_map.insert(hashed_data);
      if (triple_map_result.second) {
        // Item was newly inserted
        seen_objects.insert(hashed_data);
      }
    }
  }
  reader_parent.close();
  reader_child.close();

  // Calculate scaling factor
  float scaling_factor = 1 / (p1 * p2);

  // Scale unique items
  float j_hat = seen_objects.size() * scaling_factor;

  int j_hat_int = j_hat;
  return j_hat_int;
}

int estimate_unique_elements_in_file(const float &p, const std::string &source, const std::string &referenceFormulation) {
  // Create a random number generator
  std::random_device rd;
  std::mt19937 mt(rd());

  // Create a distribution from 0 to 1
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  if (referenceFormulation == CSV_REFERENCE_FORMULATION) {
    CsvReader reader(source);
    std::string next_element;

    std::unordered_set<uint64_t> uniqueLines;

    while (reader.readNext(next_element)) {
      // Generate a random float
      float randomFloat = dist(mt);
      if (randomFloat > p) {
        continue;
      }
      // Hash data
      uint64_t hashed_data = CityHash64(next_element.c_str(), next_element.size());
      uniqueLines.insert(hashed_data);
    }
    reader.close();

    // Update element count; -1 beacause of header in csv
    float U_hat = (uniqueLines.size() / p) - 1;

    if (uniqueLines.empty() || U_hat <= 0) {
      return 0;
    } else {
      return static_cast<int>(U_hat);
    }
  } else {
    throw_error("Error: Unsupported encoding!");
    return 0;
  }
}

std::pair<std::vector<std::string>, std::string> extractSubjectNamesAndTemplate(const SubjectMapInfo &subjectMapInfo) {
  std::vector<std::string> names;
  std::string templateString;

  if (!subjectMapInfo.template_str.empty()) {
    names = extract_substrings(subjectMapInfo.template_str);
    templateString = subjectMapInfo.template_str;
  } else if (!subjectMapInfo.reference.empty()) {
    names.push_back(subjectMapInfo.reference);
  } else if (!subjectMapInfo.constant.empty()) {
    std::string constantData = "{" + subjectMapInfo.constant + ",";
    names.push_back(constantData);
  }
  return {names, templateString};
}

void estimateCSVData(
    const std::string &source,
    const std::vector<std::string> &subjectNames,
    const std::vector<std::string> &predicateNames,
    const std::vector<std::string> &objectNames,
    const std::string &template_string_ext_subject,
    const std::string &template_string_ext_predicate,
    const std::string &template_string_ext_object,
    std::unordered_set<uint64_t> &seen_elements,
    std::unordered_set<uint64_t> &seen_objects_triple_map_wo_join) {
  /// Get number of duplicates in sample ///

  float p = gloabl_sampling_probability;

  // Create a random number generator
  std::random_device rd;
  std::mt19937 mt(rd());

  // Create a distribution from 0 to 1
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Load data of parent source
  CsvReader reader(source);

  // Get Header
  std::string header;
  reader.readNext(header);

  // Split header
  std::vector<std::string> split_header = split_csv_line(header, ',');

  // Get index of elements in header
  std::vector<uint> indices_subject;
  for (auto &element : subjectNames) {
    if (element[0] == '{') {
      break;
    }
    uint index;
    if (!get_index_of_element(split_header, element, index)) {
      throw_error("Element not found -> Estimation not working!");
    }
    indices_subject.push_back(index);
  }

  // handle predicates
  std::vector<uint> indices_predicates;
  for (auto &element : predicateNames) {
    // Means that constant is used
    if (element[0] == '{') {
      break;
    }
    uint index;
    if (!get_index_of_element(split_header, element, index)) {
      throw_error("Element not found -> Estimation not working!");
    }
    indices_predicates.push_back(index);
  }

  std::vector<uint> indices_objects;
  for (auto &element : objectNames) {
    if (element[0] == '{') {
      break;
    }
    uint index;
    if (!get_index_of_element(split_header, element, index)) {
      throw_error("Element not found -> Estimation not working!");
    }
    indices_objects.push_back(index);
  }

  // Read lines and check for duplicates
  std::string next_element;
  while (reader.readNext(next_element)) {
    // Generate a random float
    float randomFloat = dist(mt);
    if (randomFloat > p) {
      continue;
    }
    bool skip_entry = false;
    std::vector<std::string> split_data = split_csv_line(next_element, ',');

    std::string data = "";
    // Add subject
    // Add template string
    data += template_string_ext_subject + ",";
    data += template_string_ext_predicate + ",";
    data += template_string_ext_object + ",";

    if (indices_subject.size() == 0) {
      data += subjectNames[0];
    } else {
      for (auto &index : indices_subject) {
        std::string subject_data = split_data[index];
        if (subject_data == "") {
          skip_entry = true;
          break;
        }

        data += subject_data;
        data += ",";
      }
    }

    // Add predicate
    if (indices_predicates.size() == 0) {
      data += predicateNames[0];
    } else {
      for (auto &index : indices_predicates) {
        std::string predicate_data = split_data[index];
        if (predicate_data == "") {
          skip_entry = true;
          break;
        }
        data += split_data[index];
        data += ",";
      }
    }

    // Add objects
    if (indices_objects.size() == 0) {
      data += objectNames[0];
    } else {
      for (auto &index : indices_objects) {
        std::string object_data = split_data[index];
        if (object_data == "") {
          skip_entry = true;
          break;
        }

        data += object_data;
        data += ",";
      }
    }

    // If entry is not complete skip it.
    if (skip_entry) {
      continue;
    }
    // Hash data
    uint64_t hashed_data = CityHash64(data.c_str(), data.size());
    // Add only if not yet seen on tripleMap level
    auto triple_map_result = seen_objects_triple_map_wo_join.insert(hashed_data);
    if (triple_map_result.second) {
      // Item was newly inserted
      seen_elements.insert(hashed_data);
    }
  }
}

int estimate_generated_triple(
    const std::vector<std::string> &tripleMap_nodes,
    const std::vector<LogicalSourceInfo> &logicalSourceInfo_of_tripleMaps,
    const std::vector<SubjectMapInfo> &subjectMapInfo_of_tripleMaps,
    const std::vector<std::vector<PredicateMapInfo>> &predicateMapInfo_of_tripleMaps,
    const std::vector<std::vector<ObjectMapInfo>> &objectMapInfo_of_tripleMaps) {
  // Counter for estimated result size
  int result = 0;

  log("Using a sampling probability of: ");
  logln(std::to_string(gloabl_sampling_probability).substr(0, 5).c_str());

  // Store generated elements with join
  std::unordered_set<uint64_t> seen_elements_triple_map;
  // Store generated elements without join
  std::unordered_set<uint64_t> seen_objects_triple_map_wo_join;

  // Iterate over all tripleMaps
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get source path
    std::string source = logicalSourceInfo_of_tripleMaps[i].source_path;
    // Get reference formulation
    std::string referenceFormulation = logicalSourceInfo_of_tripleMaps[i].reference_formulation;

    // Generate sample to estimate duplicate rate:
    float p = gloabl_sampling_probability;

    ////// Handle classes //////
    // If data is constant -> nr of classes * 1, since subject stays the same
    if (subjectMapInfo_of_tripleMaps[i].constant != "") {
      result += subjectMapInfo_of_tripleMaps[i].classes.size() * 1;
    } else {
      // Count number of elements in source
      int element_count = estimate_unique_elements_in_file(p, source, referenceFormulation);
      result += subjectMapInfo_of_tripleMaps[i].classes.size() * element_count;
    }
    ////// Handle Subject Information //////
    // Get queries or iterators of subject
    auto [subjectNames, template_string_ext_subject] = extractSubjectNamesAndTemplate(subjectMapInfo_of_tripleMaps[i]);

    // Iterate over all found predicateObjectMap_uris and extract predicate
    for (size_t j = 0; j < predicateMapInfo_of_tripleMaps[i].size(); j++) {
      //// Handle Predicate Data ////
      PredicateMapInfo predicateMapInfo = predicateMapInfo_of_tripleMaps[i][j];

      std::vector<std::string> predicateNames;
      std::string template_string_ext_predicate = "";

      if (predicateMapInfo.template_str != "") {
        predicateNames = extract_substrings(predicateMapInfo.template_str);
        template_string_ext_predicate = predicateMapInfo.template_str;
      } else if (predicateMapInfo.reference != "") {
        // Get reference
        predicateNames.push_back(predicateMapInfo.reference);
      } else if (predicateMapInfo.constant != "") {
        // Handle constants
        predicateNames.push_back("{" + predicateMapInfo.constant + ",");
      }

      //// Handle Object Data ////
      ObjectMapInfo objectMapInfo = objectMapInfo_of_tripleMaps[i][j];

      if (objectMapInfo.parentSource.size() == 0) {
        // No join required

        // Get names of elements beeing mapped
        std::vector<std::string> objectNames;
        std::string template_string_ext_object = "";
        // Get Template
        if (objectMapInfo.template_str != "") {
          template_string_ext_object = objectMapInfo.template_str;
          std::vector<std::string> query_strings = extract_substrings(objectMapInfo.template_str);
          for (auto &element : query_strings) {
            objectNames.push_back(element);
          }
        } else if (objectMapInfo.reference != "") {
          // Get reference
          objectNames.push_back(objectMapInfo.reference);
        } else if (objectMapInfo.constant != "") {
          objectNames.push_back("{" + objectMapInfo.constant + ",");
        }

        std::unordered_set<uint64_t> seen_elements;

        if (referenceFormulation == CSV_REFERENCE_FORMULATION) {
          estimateCSVData(
              source,
              subjectNames,
              predicateNames,
              objectNames,
              template_string_ext_subject,
              template_string_ext_predicate,
              template_string_ext_object,
              seen_elements,
              seen_objects_triple_map_wo_join);
        }

        // U' = unique generated triples
        // U_hat = U' / p
        float U_hat = seen_elements.size() / p;

        // Estimate number of triples
        result += U_hat;
      } else {
        // Join required
        result += estimate_join_size(source, objectMapInfo.parentSource, objectMapInfo, subjectNames, predicateNames, seen_elements_triple_map);
      }

      // Check result size if it is above 128 bit threshold
      if (result >= THRESHOLD_64BIT) {
        return result;
      } else if (result < 0) {
        return THRESHOLD_64BIT;
      }
    }
  }
  return result;
}

uint8 detect_hash_method(
    const std::vector<std::string> &tripleMap_nodes,
    const std::vector<LogicalSourceInfo> &logicalSourceInfo_of_tripleMaps,
    const std::vector<SubjectMapInfo> &subjectMapInfo_of_tripleMaps,
    const std::vector<std::vector<PredicateMapInfo>> &predicateMapInfo_of_tripleMaps,
    const std::vector<std::vector<ObjectMapInfo>> &objectMapInfo_of_tripleMaps) {
  uint8 hash_method;
  auto start = std::chrono::high_resolution_clock::now();
  int res = estimate_generated_triple(
      tripleMap_nodes,
      logicalSourceInfo_of_tripleMaps,
      subjectMapInfo_of_tripleMaps,
      predicateMapInfo_of_tripleMaps,
      objectMapInfo_of_tripleMaps);
  auto end = std::chrono::high_resolution_clock::now();
  int duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  log("Estimation took: ");
  log(duration);
  logln(" milliseconds");
  log("Estimated number of unique elements: ");
  logln(res);

  // Decide on Hash method
  // 0 -> 32bit, 1 -> 64bit, 2 -> 128bit
  // Threshold is estimated using birthday problem with a probability of 0.05% to get a collision

  if (res < 2073) {
    // 32bit
    hash_method = 0;
  } else if (res < 135835773) {
    // 64bit
    hash_method = 1;
  } else {
    // 128bit
    hash_method = 2;
  }

  return hash_method;
}
#endif
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
    const std::string &graph_termType,
    const std::string &graph_template,
    const std::string &graph_constant,
    const std::vector<std::string> &split_data,
    const std::vector<std::string> &split_header) {
  // Ensure the provided termType is supported (only 'IRI' is supported).
  if (graph_termType != IRI_TERM_TYPE) {
    throw_error("Error: Unsupported graph termType - termType of graph can only be 'IRI'.");
  }

  // If a graph template is provided, process and return the graph based on the template.
  if (!graph_template.empty()) {
    logln_debug("Generating graph based on template...");

    std::string current_graph = fill_in_template(graph_template, split_data, split_header, IRI_TERM_TYPE);

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
std::string generate_graph(const SubjectMapInfo &subjectMapInfo, const std::vector<std::string> &split_data, const std::vector<std::string> &split_header) {
  return generate_graph_logic(subjectMapInfo.graph_termType, subjectMapInfo.graph_template, subjectMapInfo.graph_constant, split_data, split_header);
}

// Calls generate graph if input is PredicateObjectMapInfo
std::string generate_graph(const PredicateObjectMapInfo &predicateObjectMapInfo, const std::vector<std::string> &split_data, const std::vector<std::string> &split_header) {
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
std::string generate_subject(const SubjectMapInfo &subjectMapInfo, const std::vector<std::string> &split_data, const std::vector<std::string> &split_header) {
  std::string current_generated_subject = "";

  // Check if term type is supported on subject -> only blank node and IRI
  if (subjectMapInfo.termType != IRI_TERM_TYPE && subjectMapInfo.termType != "http://www.w3.org/ns/r2rml#BlankNode") {
    throw_error("Error: termType 'literal' is not supported in subject position.");
  }

  // Check if template is available
  if (subjectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating subject based on template...");
    current_generated_subject = fill_in_template(subjectMapInfo.template_str, split_data, split_header, IRI_TERM_TYPE);
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
    current_generated_subject = fill_in_template(temp_template, split_data, split_header);
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
std::string generate_predicate(const PredicateMapInfo &predicateMapInfo, const std::vector<std::string> &split_data, const std::vector<std::string> &split_header) {
  std::string current_predicate = "";
  // Check if template is available
  if (predicateMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating predicate based on template...");
    current_predicate = fill_in_template(predicateMapInfo.template_str, split_data, split_header);
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

std::string generate_object_with_nested_loop_join(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data_child,
    const std::vector<std::string> &split_header_child,
    CsvReader &reader_parent) {
  std::string generated_object = "";
  // Check if template is available
  if (objectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating object based on template...");

    generated_object = fill_in_template(objectMapInfo.template_str, split_data_child, split_header_child);

    // Reset file
    reader_parent.reset();

    // Get Header
    std::string header_parent;
    reader_parent.readNext(header_parent);

    // Split header
    std::vector<std::string> split_header_parent = split_csv_line(header_parent, ',');

    // Get index of element in header
    uint index = 0;
    if (!get_index_of_element(split_header_parent, objectMapInfo.parent, index)) {
      throw_error("Element not found -> Join not working!");
    }

    // Generate value
    uint index_child = 0;
    if (!get_index_of_element(split_header_child, objectMapInfo.child, index_child)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string next_element;
    // Flag to indicate if the element was found
    bool found_element = false;
    while (reader_parent.readNext(next_element)) {
      std::vector<std::string> split_data_ref = split_csv_line(next_element, ',');
      if (split_data_ref[index] == split_data_child[index_child]) {
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

std::vector<std::string> generate_object_with_nested_loop_join_full(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data_child,
    const std::vector<std::string> &split_header_child,
    CsvReader &reader_parent) {
  std::vector<std::string> generated_objects;
  if (!objectMapInfo.template_str.empty()) {
    uint index_child = 0;
    if (!get_index_of_element(split_header_child, objectMapInfo.child, index_child)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string child_value = split_data_child[index_child];
    if (child_value == "") {
      generated_objects.push_back("");
      return generated_objects;
    }

    // Reset file
    reader_parent.reset();

    // Get Header
    std::string header_parent;
    reader_parent.readNext(header_parent);

    // Split header
    std::vector<std::string> split_header_parent = split_csv_line(header_parent, ',');

    // Get index of element in header
    uint index_parent = 0;
    if (!get_index_of_element(split_header_parent, objectMapInfo.parent, index_parent)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string result;
    result.reserve(1000);
    std::string next_element;
    while (reader_parent.readNext(next_element)) {
      std::vector<std::string> split_data_parent = split_csv_line(next_element, ',');
      if (split_data_parent[index_parent] == split_data_child[index_child]) {
        result = fill_in_template(objectMapInfo.template_str, split_data_parent, split_header_parent);
        if (!result.empty()) {
          generated_objects.push_back(result);
        }
      }
    }
  }

  for (auto &generated_object : generated_objects) {
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
  }
  return generated_objects;
}

std::vector<std::string> generate_object_with_hash_join_full(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data_child,
    const std::vector<std::string> &split_header_child,
    const std::vector<std::string> &split_header_parent,
    const std::unordered_map<std::string, std::vector<std::vector<std::string>>> &parent_file_index_full) {
  std::vector<std::string> generated_objects;
  if (!objectMapInfo.template_str.empty()) {
    uint index_child = 0;
    if (!get_index_of_element(split_header_child, objectMapInfo.child, index_child)) {
      throw_error("Element not found -> Join not working!");
    }

    std::string child_value = split_data_child[index_child];
    if (child_value == "") {
      generated_objects.push_back("");
      return generated_objects;
    }

    std::string result;
    result.reserve(1000);
    if (parent_file_index_full.find(child_value) != parent_file_index_full.end()) {
      for (const auto &data_row : parent_file_index_full.at(child_value)) {
        result = fill_in_template(objectMapInfo.template_str, data_row, split_header_parent);
        if (!result.empty()) {
          generated_objects.push_back(result);
        }
      }
    }
  }

  for (auto &generated_object : generated_objects) {
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
  }
  return generated_objects;
}

std::string generate_object_with_hash_join(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data,
    const std::vector<std::string> &split_header,
    CsvReader &reader,
    const std::unordered_map<std::string, std::streampos> &parent_file_index) {
  std::string generated_object = "";

  // Fill in template and store it the generate value
  logln_debug("Generating object based on template...");

  generated_object = fill_in_template(objectMapInfo.template_str, split_data, split_header);

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
    } catch (const std::out_of_range &) {
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
std::string generate_object_wo_join(const ObjectMapInfo &objectMapInfo, const std::vector<std::string> &split_data, const std::vector<std::string> &split_header) {
  std::string generated_object = "";
  // Check if template is available
  if (objectMapInfo.template_str != "") {
    // Fill in template and store it the generate value
    logln_debug("Generating object based on template...");
    generated_object = fill_in_template(objectMapInfo.template_str, split_data, split_header);
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
    generated_object = fill_in_template(temp_template, split_data, split_header);
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
std::vector<std::string> generate_object(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data_child,
    const std::vector<std::string> &split_header_child,
    const std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> &parent_file_index,
    const std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::vector<std::string>>>> &parent_file_index_full,
    const std::unordered_map<std::string, std::vector<std::string>> &new_split_header_indices,
    std::map<std::string, std::string> &input_data) {
  // Check if a join is required
  if (objectMapInfo.parentSource != "") {
    std::vector<std::string> res;
    CsvReader reader_parent(objectMapInfo.parentSource);

    // Check type of join
    if (objectMapInfo.join_reference_condition_available == "true") {
      std::string result = generate_object_with_hash_join(objectMapInfo, split_data_child, split_header_child, reader_parent, parent_file_index.at(objectMapInfo.parentSource));

      res.push_back(result);
    } else {
      // Perform normal join
      res = generate_object_with_hash_join_full(objectMapInfo, split_data_child, split_header_child, new_split_header_indices.at(objectMapInfo.parentSource), parent_file_index_full.at(objectMapInfo.parentSource));
    }
    reader_parent.close();
    return res;
  }
  // Generate object in-memory
  else if (objectMapInfo.parent_in_memory_name != "") {
    std::vector<std::string> res;

    CsvReader reader_parent(input_data[objectMapInfo.parent_in_memory_name], false);

    if (objectMapInfo.join_reference_condition_available == "true") {
      std::string result = generate_object_with_nested_loop_join(objectMapInfo, split_data_child, split_header_child, reader_parent);
      res.push_back(result);
      return res;
    } else {
      res = generate_object_with_nested_loop_join_full(objectMapInfo, split_data_child, split_header_child, reader_parent);
      return res;
    }

  } else {
    // No join required
    std::string result = generate_object_wo_join(objectMapInfo, split_data_child, split_header_child);
    std::vector<std::string> res;
    res.push_back(result);
    return res;
  }
}

////////////////////////////////
////// MAPPING FUNCTIONS ///////
////////////////////////////////

void generate_quads(
    std::unordered_set<NQuad> &generated_quads,
    const SubjectMapInfo &subjectMapInfo,
    const std::vector<PredicateObjectMapInfo> &predicateObjectMapInfo_vec,
    const std::vector<PredicateMapInfo> &predicateMapInfo_vec,
    const std::vector<ObjectMapInfo> &objectMapInfo_vec,
    const std::string &line_data,
    const std::vector<std::string> &split_header,
    const std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> &parent_file_indices,
    const std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::vector<std::string>>>> &parent_file_indices_full,
    const std::unordered_map<std::string, std::vector<std::string>> &new_split_header_indices,
    std::map<std::string, std::string> &input_data) {
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
  for (const std::string &subject_class : subjectMapInfo.classes) {
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
      // Generation finished
      continue;
    }

    //////////////////////
    // Generate Object //
    /////////////////////

    std::vector<std::string> objects = generate_object(
        objectMapInfo_vec[k],
        split_data, split_header,
        parent_file_indices,
        parent_file_indices_full,
        new_split_header_indices,
        input_data);
    // If current_graph is "" -> No value in input data
    for (const auto &obj : objects) {
      if (obj == "") {
        continue;
      }

      // Set object
      temp_quad.object = obj;

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
}

// Function to map data in memory
std::unordered_set<NQuad> map_data(std::string &rml_rule, std::map<std::string, std::string> &input_data) {
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

  // Generate mapping for each tripleMap
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    // Initialize current tripleMap_node
    std::string tripleMap_node = tripleMap_nodes[i];

    // Get specified format of source file
    std::string current_logical_source_format = logicalSourceInfo_of_tripleMaps[i].reference_formulation;

    // Get path of specified source file
    std::string file_path = logicalSourceInfo_of_tripleMaps[i].source_path;

    std::string in_memory_name = logicalSourceInfo_of_tripleMaps[i].in_memory_name;

    // Handle CSV
    if (current_logical_source_format == CSV_REFERENCE_FORMULATION) {
      // Create dummy data structure
      std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> parent_file_indices;
      std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::vector<std::string>>>> parent_file_indices_full;
      std::unordered_map<std::string, std::vector<std::string>> new_split_header_indices;

      // Load data
      CsvReader reader(input_data[in_memory_name], false);

      // Get Header and split it
      std::string header;
      reader.readNext(header);
      std::vector<std::string> split_header = split_csv_line(header, ',');

      std::string next_element;
      while (reader.readNext(next_element)) {
        generate_quads(
            generated_quads,
            subjectMapInfo_of_tripleMaps[i],
            predicateObjectMapInfo_of_tripleMaps[i],
            predicateMapInfo_of_tripleMaps[i],
            objectMapInfo_of_tripleMaps[i],
            next_element,
            split_header,
            parent_file_indices,
            parent_file_indices_full,
            new_split_header_indices,
            input_data);
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

void map_data_to_file(std::string &rml_rule, std::ofstream &out_file, Flags &flags) {
  bool remove_duplicates = flags.check_duplicates;
  bool adaptive_hash_selection = flags.adaptive_hash_selection;
  uint8 fixed_bit_size = flags.fixed_bit_size;
  global_tokens_to_remove = flags.tokens_to_remove;

  // Set sampling probability
  gloabl_sampling_probability = flags.sampling_probability;

  // Set dummy value for input data
  std::map<std::string, std::string> input_data;

  //////////////////////////////////
  //// STEP 1: Read RDF triple ////
  /////////////////////////////////

  // Vector to store all provided rml_triples
  std::vector<NTriple> rml_triple;
  std::string base_uri;
  read_and_prepare_rml_triple(rml_rule, rml_triple, base_uri);
  // Create data structures to store hashes
  // Used to store 128 bit hashes
  std::unordered_map<uint64_t, uint128, uint128Hash> nquad_hashes_128;

  // Used to store 64 bit hashes
  std::unordered_set<uint64_t> nquad_hashes_64;

  // Used to store 32 bit hashes
  std::unordered_set<uint32_t> nquad_hashes_32;

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

  // Decide on hash size
  uint8 hash_method;

  // Handle fixed bit size
  if (fixed_bit_size == 128) {
    hash_method = 2;
    adaptive_hash_selection = false;
  } else if (fixed_bit_size == 64) {
    hash_method = 1;
    adaptive_hash_selection = false;
  } else if (fixed_bit_size == 32) {
    hash_method = 0;
    adaptive_hash_selection = false;
  } else {
    hash_method = 2;
  }

  // Check if adaptive selection is needed
  if (adaptive_hash_selection == true) {
    hash_method = detect_hash_method(
        tripleMap_nodes,
        logicalSourceInfo_of_tripleMaps,
        subjectMapInfo_of_tripleMaps,
        predicateMapInfo_of_tripleMaps,
        objectMapInfo_of_tripleMaps);
  }

  log("Using a ");
  if (hash_method == 0) {
    log(32);
  } else if (hash_method == 1) {
    log(64);
  } else if (hash_method == 2) {
    log(128);
  }
  logln(" bit hash function.");

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
      std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> parent_file_indices;
      std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::vector<std::string>>>> parent_file_indices_full;
      std::unordered_map<std::string, std::vector<std::string>> new_split_header_indices;
      for (const auto &objectMap : objectMapInfo_of_tripleMaps[i]) {
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
        uint index = 0;
        if (!get_index_of_element(split_header_ref, objectMap.parent, index)) {
          throw_error("Element not found -> Join not working!");
        }

        // Create Index for full join
        if (objectMap.join_reference_condition_available == "false") {
          // Create vector to store indecies
          std::vector<std::string> new_split_header;
          // Get template
          std::vector<std::string> query_strings = extract_substrings(objectMap.template_str);
          // Get index
          std::vector<size_t> element_index;
          for (size_t i = 0; i < query_strings.size(); ++i) {
            uint index = 0;
            if (!get_index_of_element(split_header_ref, query_strings[i], index)) {
              throw_error("Error: Element not found in csv.");
            }
            element_index.push_back(index);
            new_split_header.push_back(query_strings[i]);
          }
          parent_file_indices_full[objectMap.parentSource] = create_index_full(objectMap.parentSource, index, element_index);
          new_split_header_indices[objectMap.parentSource] = new_split_header;
        } else {
          // Create Index for use with join_reference_condition
          parent_file_indices[objectMap.parentSource] = create_index(objectMap.parentSource, index);
        }

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
            subjectMapInfo_of_tripleMaps[i],
            predicateObjectMapInfo_of_tripleMaps[i],
            predicateMapInfo_of_tripleMaps[i],
            objectMapInfo_of_tripleMaps[i],
            next_element,
            split_header,
            parent_file_indices,
            parent_file_indices_full,
            new_split_header_indices,
            input_data);

        // Write to file
        for (const NQuad &quad : generated_quads) {
          bool add_triple = true;

          // Check if value is a duplicate
          if (remove_duplicates && hash_method == 2) {
            uint128 hash_of_quad = performCity128Hash(quad);

            // Attempt to find the first part of the 128-bit hash in the map
            auto it = nquad_hashes_128.find(hash_of_quad.first);

            // Check if the first part of the hash was found in the map
            if (it != nquad_hashes_128.end() && it->second == hash_of_quad) {
              // If found, check if the second part of the hash also matches
              // If both parts match, we have found a complete match
              add_triple = false;
            } else {
              // If the first part of the hash is not found, or the second part does not match
              // This means the hash is not found in the map
              nquad_hashes_128[hash_of_quad.first] = hash_of_quad;
            }
          } else if (remove_duplicates && hash_method == 1) {
            uint64_t hash_of_quad = performCity64Hash(quad);
            // If not found
            if (nquad_hashes_64.find(hash_of_quad) == nquad_hashes_64.end()) {
              // Add to hashes
              nquad_hashes_64.insert(hash_of_quad);
            } else {
              // Otherwise dont add triple
              add_triple = false;
            }
          } else if (remove_duplicates && hash_method == 0) {
            uint32_t hash_of_quad = performCity32Hash(quad);
            // If not found
            if (nquad_hashes_32.find(hash_of_quad) == nquad_hashes_32.end()) {
              // Add to hashes
              nquad_hashes_32.insert(hash_of_quad);
            } else {
              // Otherwise dont add triple
              add_triple = false;
            }
          }

          if (add_triple) {
            out_file << quad.subject << " "
                     << quad.predicate << " "
                     << quad.object << " ";

            if (quad.graph != "") {
              out_file << quad.graph << " .\n";
            } else {
              out_file << ".\n";
            }
          }
        }

        generated_quads.clear();
      }
    } else {
      throw_error("Error: Found unsupported encoding.");
    }
  }

  log("Number of generated triples: ");
  if (hash_method == 0) {
    logln(std::to_string(nquad_hashes_32.size()).c_str());
  } else if (hash_method == 1) {
    logln(std::to_string(nquad_hashes_64.size()).c_str());
  } else if (hash_method == 2) {
    logln(std::to_string(nquad_hashes_128.size()).c_str());
  }
}

/////////////////////////////////////////////
//// MAP DATA TO FILE - MULTIPLE THREAD ////
///////////////////////////////////////////

template <typename T>
class ThreadSafeQueue {
 private:
  std::queue<T> queue;
  std::mutex mtx;
  std::condition_variable not_empty_cv;
  bool done = false;
  size_t max_size;

 public:
  explicit ThreadSafeQueue(size_t max_size = 0) : max_size(max_size) {}

  void push(const std::vector<T> &items) {
    std::unique_lock<std::mutex> lock(mtx);
    for (const auto &item : items) {
      queue.push(item);
    }
    not_empty_cv.notify_one();  // Notify one waiting thread
  }

  size_t pop(std::vector<T> &items, size_t max_items_to_pop) {
    std::unique_lock<std::mutex> lock(mtx);
    while (queue.empty() && !done) {
      not_empty_cv.wait(lock);
    }
    if (queue.empty() && done)
      return 0;

    size_t popped_count = 0;
    while (!queue.empty() && popped_count < max_items_to_pop) {
      items.push_back(queue.front());
      queue.pop();
      ++popped_count;
    }
    return popped_count;
  }

  void mark_done() {
    std::unique_lock<std::mutex> lock(mtx);
    done = true;
    not_empty_cv.notify_all();
  }

  bool isEmpty() {
    std::unique_lock<std::mutex> lock(mtx);
    return queue.empty();
  }

  bool isDone() {
    std::unique_lock<std::mutex> lock(mtx);
    return done;
  }
};

void process_triple_map(
    const SubjectMapInfo &subjectMapInfo_of_tripleMap,
    const std::vector<PredicateObjectMapInfo> &predicateObjectMapInfo_of_tripleMap,
    const std::vector<ObjectMapInfo> &objectMapInfo_of_tripleMap,
    const std::vector<PredicateMapInfo> &predicateMapInfo_of_tripleMap,
    const LogicalSourceInfo &logicalSourceInfo_of_tripleMap,
    ThreadSafeQueue<NQuad> &quadQueue) {
  // Dummy for input data
  std::map<std::string, std::string> input_data;

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
    std::unordered_map<std::string, std::unordered_map<std::string, std::streampos>> parent_file_indices;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::vector<std::string>>>> parent_file_indices_full;
    std::unordered_map<std::string, std::vector<std::string>> new_split_header_indices;

    for (const auto &objectMap : objectMapInfo_of_tripleMap) {
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
      uint index = 0;
      if (!get_index_of_element(split_header_ref, objectMap.parent, index)) {
        throw_error("Element not found -> Join not working!");
      }

      // Create Index for full join
      if (objectMap.join_reference_condition_available == "false") {
        // Create vector to store indecies
        std::vector<std::string> new_split_header;
        // Get template
        std::vector<std::string> query_strings = extract_substrings(objectMap.template_str);
        // Get index
        std::vector<size_t> element_index;
        for (size_t i = 0; i < query_strings.size(); ++i) {
          uint index = 0;
          if (!get_index_of_element(split_header_ref, query_strings[i], index)) {
            throw_error("Error: Element not found in csv.");
          }
          element_index.push_back(index);
          new_split_header.push_back(query_strings[i]);
        }
        parent_file_indices_full[objectMap.parentSource] = create_index_full(objectMap.parentSource, index, element_index);
        new_split_header_indices[objectMap.parentSource] = new_split_header;
      } else {
        // Create Index for use with join_reference_condition
        parent_file_indices[objectMap.parentSource] = create_index(objectMap.parentSource, index);
      }

      reader.close();
    }

    // Load data
    CsvReader reader(file_path);

    // Get Header and split it
    std::string header;
    reader.readNext(header);
    std::vector<std::string> split_header = split_csv_line(header, ',');

    std::string next_element;
    std::vector<NQuad> quad_batch;
    while (reader.readNext(next_element)) {
      generate_quads(
          generated_quads,
          subjectMapInfo_of_tripleMap,
          predicateObjectMapInfo_of_tripleMap,
          predicateMapInfo_of_tripleMap,
          objectMapInfo_of_tripleMap,
          next_element,
          split_header,
          parent_file_indices,
          parent_file_indices_full,
          new_split_header_indices,
          input_data);

      // Add generated quads to the batch
      for (const auto &quad : generated_quads) {
        quad_batch.push_back(quad);
        if (quad_batch.size() >= BATCH_SIZE) {
          quadQueue.push(quad_batch);
          quad_batch.clear();
        }
      }

      generated_quads.clear();
    }
    if (!quad_batch.empty()) {
      quadQueue.push(quad_batch);
    }
    reader.close();
  } else {
    throw_error("Error: Found unsupported encoding.");
  }
}

void writerThread(std::ofstream &out_file, ThreadSafeQueue<NQuad> &quadQueue, bool remove_duplicates, const uint8_t &hash_method) {
  NQuad quad;

  std::ostringstream outStream;

  // Create data structures to store hashes
  // Used to store 128 bit hashes
  std::unordered_map<uint64_t, uint128, uint128Hash> nquad_hashes_128;

  // Used to store 64 bit hashes
  std::unordered_set<uint64_t> nquad_hashes_64;

  // Used to store 32 bit hashes
  std::unordered_set<uint32_t> nquad_hashes_32;

  std::vector<NQuad> quad_batch;
  while (true) {
    size_t count = quadQueue.pop(quad_batch, BATCH_SIZE);
    if (count == 0) {
      break;
    }
    for (const NQuad &quad : quad_batch) {
      bool add_triple = true;

      // Check if value is a duplicate
      if (remove_duplicates && hash_method == 2) {
        uint128 hash_of_quad = performCity128Hash(quad);

        // Attempt to find the first part of the 128-bit hash in the map
        auto it = nquad_hashes_128.find(hash_of_quad.first);

        // Check if the first part of the hash was found in the map
        if (it != nquad_hashes_128.end() && it->second == hash_of_quad) {
          // If found, check if the second part of the hash also matches
          // If both parts match, we have found a complete match
          add_triple = false;
        } else {
          // If the first part of the hash is not found, or the second part does not match
          // This means the hash is not found in the map
          nquad_hashes_128[hash_of_quad.first] = hash_of_quad;
        }
      } else if (remove_duplicates && hash_method == 1) {
        uint64_t hash_of_quad = performCity64Hash(quad);
        // If not found
        if (nquad_hashes_64.find(hash_of_quad) == nquad_hashes_64.end()) {
          // Add to hashes
          nquad_hashes_64.insert(hash_of_quad);
        } else {
          // Otherwise dont add triple
          add_triple = false;
        }
      } else if (remove_duplicates && hash_method == 0) {
        uint32_t hash_of_quad = performCity32Hash(quad);
        // If not found
        if (nquad_hashes_32.find(hash_of_quad) == nquad_hashes_32.end()) {
          // Add to hashes
          nquad_hashes_32.insert(hash_of_quad);
        } else {
          // Otherwise dont add triple
          add_triple = false;
        }
      }

      if (add_triple) {
        // Append to the buffer instead of writing directly to the file
        outStream << quad.subject << " " << quad.predicate << " " << quad.object << " ";
        if (quad.graph != "") {
          outStream << quad.graph << " .\n";
        } else {
          outStream << ".\n";
        }
      }
    }
    quad_batch.clear();

    // Write the buffered data to the file
    out_file << outStream.str();
    outStream.str("");  // Clear the buffer
    outStream.clear();  // Clear any error flags
  }

  log("Number of generated triples: ");
  if (hash_method == 0) {
    logln(std::to_string(nquad_hashes_32.size()).c_str());
  } else if (hash_method == 1) {
    logln(std::to_string(nquad_hashes_64.size()).c_str());
  } else if (hash_method == 2) {
    logln(std::to_string(nquad_hashes_128.size()).c_str());
  }
}

void map_data_to_file_threading(std::string &rml_rule, std::ofstream &out_file, Flags &flags) {
  bool remove_duplicates = flags.check_duplicates;
  bool adaptive_hash_selection = flags.adaptive_hash_selection;
  uint8_t num_threads = flags.thread_count;
  float sampling_probability = flags.sampling_probability;
  int fixed_bit_size = flags.fixed_bit_size;
  global_tokens_to_remove = flags.tokens_to_remove;

  // Set sampling probability
  gloabl_sampling_probability = sampling_probability;

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
  std::vector<std::string> tripleMap_nodes = find_matching_subject(rml_triple, RDF_TYPE, TRIPLES_MAP);

  // If only one tripleMap is found use single threading, to reduce overhead sicne there is no advantage
  if (tripleMap_nodes.size() == 1) {
    map_data_to_file(rml_rule, out_file, flags);
    return;
  }

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

  logln_debug("Finished parsing RML rules.");

  // Decide on hash size
  uint8 hash_method;

  // Handle fixed bit size
  if (fixed_bit_size == 128) {
    hash_method = 2;
    adaptive_hash_selection = false;
  } else if (fixed_bit_size == 64) {
    hash_method = 1;
    adaptive_hash_selection = false;
  } else if (fixed_bit_size == 32) {
    hash_method = 0;
    adaptive_hash_selection = false;
  } else {
    hash_method = 2;
  }

  if (adaptive_hash_selection == true) {
    hash_method = detect_hash_method(
        tripleMap_nodes,
        logicalSourceInfo_of_tripleMaps,
        subjectMapInfo_of_tripleMaps,
        predicateMapInfo_of_tripleMaps,
        objectMapInfo_of_tripleMaps);
  }

  log("Using a ");
  if (hash_method == 0) {
    log(32);
  } else if (hash_method == 1) {
    log(64);
  } else if (hash_method == 2) {
    log(128);
  }
  logln(" bit hash function.");

  ///////////////////////////////////////////////////////
  //// STEP 3: Generate Mapping based on RML rules ////
  /////////////////////////////////////////////////////

  // Determine the number of threads to use
  const size_t numThreads = (num_threads == 0) ? std::thread::hardware_concurrency() : static_cast<size_t>(num_threads);
  log_debug("Using ");
  log_debug(numThreads);
  logln_debug(" threads.");
  std::vector<std::thread> threads;

  ThreadSafeQueue<NQuad> quadQueue(1000);

  // Start the writer thread before the worker threads
  std::thread writer(writerThread, std::ref(out_file), std::ref(quadQueue), remove_duplicates, std::ref(hash_method));
  for (size_t i = 0; i < tripleMap_nodes.size(); i++) {
    threads.push_back(std::thread(
        process_triple_map,
        std::ref(subjectMapInfo_of_tripleMaps[i]),
        std::ref(predicateObjectMapInfo_of_tripleMaps[i]),
        std::ref(objectMapInfo_of_tripleMaps[i]),
        std::ref(predicateMapInfo_of_tripleMaps[i]),
        std::ref(logicalSourceInfo_of_tripleMaps[i]),
        std::ref(quadQueue)));

    if (threads.size() == numThreads - 1 || i == tripleMap_nodes.size() - 1) {
      for (std::thread &t : threads) {
        t.join();
      }
      threads.clear();
    }
  }

  quadQueue.mark_done();

  writer.join();
}

#endif