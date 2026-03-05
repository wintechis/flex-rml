#include "simple_executor.h"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

#include "definitions.h"
#include "utils.h"

namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////
/// Data setup
///////////////////////////////////////////////////////////////
struct SetupData {
  std::unordered_set<uint64_t> unique_hashes;

  std::string line;
  std::vector<std::string> split_line;
  std::vector<std::string> projected_row;

  size_t triple_counter = 0;
  size_t write_cnt = 0;
  uint64_t hash = 0;
  bool skip = false;
  size_t buffer_limit = 20000;

  std::string subject;
  std::string predicate;
  std::string object;
  std::string graph;
  std::string res;
  std::string buffered_res;

  std::ofstream outputFile;
};

SetupData initialize_setup(const fs::path& output_file_name) {
  SetupData data;

  // Reserve memory for strings and vectors
  data.line.reserve(512);
  data.split_line.reserve(32);
  data.projected_row.reserve(32);

  data.subject.reserve(512);
  data.predicate.reserve(512);
  data.object.reserve(512);
  data.graph.reserve(512);
  data.res.reserve(2048);
  data.buffered_res.reserve(204800);

  // Open output file
  fs::create_directories(output_file_name.parent_path());
  data.outputFile.open(output_file_name, std::ios::app);

  if (!data.outputFile) {
    std::cerr << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }

  return data;
}
///////////////

SetupData initialize_setup_dependent(const fs::path& output_file_name) {
  SetupData data;

  // Reserve memory for strings and vectors
  data.line.reserve(512);
  data.split_line.reserve(32);
  data.projected_row.reserve(32);

  data.subject.reserve(512);
  data.predicate.reserve(512);
  data.object.reserve(512);
  data.graph.reserve(512);
  data.res.reserve(2048);
  data.buffered_res.reserve(204800);

  return data;
}


///////////////////////////////////////////////////////////////7
/// HELPER FUNCTIONS
//////////////////////////////////////////////////////////////
std::vector<int> get_attribute_index(std::istream& file, const std::vector<std::string>& header, const std::vector<std::string>& projected_attributes) {
  // Get indices for projected attributes
  std::vector<int> projected_indices;
  for (const auto& attr : projected_attributes) {
    auto it = std::find_if(header.begin(), header.end(), [&](std::string_view field) {
      return field == attr;
    });
    if (it != header.end()) {
      projected_indices.push_back(std::distance(header.begin(), it));
    } else {
      std::cerr << "Attribute not found: " << attr << std::endl;
      std::exit(1);
    }
  }

  return projected_indices;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

int execute_simple_with_graph(const std::string& input_file_name,
                              const fs::path& output_file_name,
                              const std::string& base_uri,
                              const std::vector<std::string>& projected_attributes,
                              const std::vector<std::string>& s_content,
                              const std::vector<std::string>& p_content,
                              const std::vector<std::string>& o_content,
                              const std::vector<std::string>& g_content) {
  // Setup
  SetupData setup_data = initialize_setup(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cout << "Error opening file!" << std::endl;
    std::exit(1);
  }

  // Get index of attributes in header
  // Read and split header
  std::getline(file, setup_data.line);
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices = get_attribute_index(file, header, projected_attributes);

  // Project Header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }

  // Iterate over file line by line
  while (std::getline(file, setup_data.line)) {
    setup_data.split_line = split_csv_line(setup_data.line, ',');

    ////// PROJECTION //////
    setup_data.projected_row.clear();
    for (int i : projected_indices) {
      setup_data.projected_row.push_back(setup_data.split_line[i]);
    }

    // Check for NULL values
    setup_data.skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(setup_data.projected_row.begin(), setup_data.projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        setup_data.skip = true;
        break;
      }
    }
    if (setup_data.skip) {
      continue;
    }

    // Eliminate duplicates
    setup_data.hash = combinedHash(setup_data.projected_row);
    if (!(setup_data.unique_hashes.insert(setup_data.hash).second)) {
      continue;
    }

    // Create map
    std::unordered_map<std::string, std::string> row;
    for (int i = 0; i < setup_data.projected_row.size(); i++) {
      row[projected_header[i]] = setup_data.projected_row[i];
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (s_content[1] == "preformatted") {
        setup_data.subject = s_content[0];
      } else {
        setup_data.subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row);
      }
      // PREDICATE
      if (p_content[1] == "preformatted") {
        setup_data.predicate = p_content[0];
      } else {
        setup_data.predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row);
      }
      // OBJECT
      if (o_content[1] == "preformatted") {
        setup_data.object = o_content[0];
      } else {
        setup_data.object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row);
      }
      // GRAPH
      if (g_content[1] == "preformatted") {
        setup_data.graph = g_content[0];
      } else {
        setup_data.graph = create_operator(g_content[0], g_content[1], g_content[2], "", "", base_uri, row);
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    setup_data.res = setup_data.subject + " " + setup_data.predicate + " " + setup_data.object + " " + setup_data.graph + " .\n";
    setup_data.triple_counter++;

    setup_data.buffered_res += setup_data.res;
    setup_data.write_cnt++;

    if (setup_data.write_cnt == setup_data.buffer_limit) {
      setup_data.write_cnt = 0;
      ////// SERIALIZE //////
      setup_data.outputFile << setup_data.buffered_res;
      setup_data.buffered_res = "";
    }
  }
  ////// SERIALIZE //////
  setup_data.outputFile << setup_data.buffered_res;

  file.close();

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
                                                                    std::unordered_set<std::string>& unique_triple) {
  // Setup
  SetupData setup_data = initialize_setup_dependent(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cout << "Error opening file!" << std::endl;
    std::exit(1);
  }

  // Get index of attributes in header
  // Read and split header
  std::getline(file, setup_data.line);
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices = get_attribute_index(file, header, projected_attributes);

  // Project Header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }

  // Iterate over file line by line
  while (std::getline(file, setup_data.line)) {
    setup_data.split_line = split_csv_line(setup_data.line, ',');

    ////// PROJECTION //////
    setup_data.projected_row.clear();
    for (int i : projected_indices) {
      setup_data.projected_row.push_back(setup_data.split_line[i]);
    }

    // Check for NULL values
    setup_data.skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(setup_data.projected_row.begin(), setup_data.projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        setup_data.skip = true;
        break;
      }
    }
    if (setup_data.skip) {
      continue;
    }

    // Eliminate duplicates
    setup_data.hash = combinedHash(setup_data.projected_row);
    if (!(setup_data.unique_hashes.insert(setup_data.hash).second)) {
      continue;
    }

    // Create map
    std::unordered_map<std::string, std::string> row;
    for (int i = 0; i < setup_data.projected_row.size(); i++) {
      row[projected_header[i]] = setup_data.projected_row[i];
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (s_content[1] == "preformatted") {
        setup_data.subject = s_content[0];
      } else {
        setup_data.subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row);
      }
      // PREDICATE
      if (p_content[1] == "preformatted") {
        setup_data.predicate = p_content[0];
      } else {
        setup_data.predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row);
      }
      // OBJECT
      if (o_content[1] == "preformatted") {
        setup_data.object = o_content[0];
      } else {
        setup_data.object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row);
      }
      // GRAPH
      if (g_content[1] == "preformatted") {
        setup_data.graph = g_content[0];
      } else {
        setup_data.graph = create_operator(g_content[0], g_content[1], g_content[2], "", "", base_uri, row);
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    setup_data.res = setup_data.subject + " " + setup_data.predicate + " " + setup_data.object + " " + setup_data.graph + " .\n";
    unique_triple.insert(setup_data.res);
    setup_data.triple_counter++;
  }

  file.close();

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
                    const std::vector<std::string>& o_content) {
  ///// Setup /////
  SetupData setup_data = initialize_setup(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cout << "Error opening file!" << std::endl;
    std::exit(1);
  }

  // Get index of attributes in header
  std::getline(file, setup_data.line);
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices = get_attribute_index(file, header, projected_attributes);

  // Project Header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }

  // Iterate over file line by line
  while (std::getline(file, setup_data.line)) {
    setup_data.split_line = split_csv_line(setup_data.line, ',');

    ////// PROJECTION //////
    setup_data.projected_row.clear();
    for (int i : projected_indices) {
      setup_data.projected_row.push_back(setup_data.split_line[i]);
    }

    // Check for NULL values
    setup_data.skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(setup_data.projected_row.begin(), setup_data.projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        setup_data.skip = true;
        break;
      }
    }
    if (setup_data.skip) {
      continue;
    }

    // Eliminate duplicates
    setup_data.hash = combinedHash(setup_data.projected_row);
    if (!(setup_data.unique_hashes.insert(setup_data.hash).second)) {
      continue;
    }

    // Create map of row
    std::unordered_map<std::string, std::string> row;
    for (int i = 0; i < setup_data.projected_row.size(); i++) {
      row[projected_header[i]] = setup_data.projected_row[i];
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (s_content[1] == "preformatted") {
        setup_data.subject = s_content[0];
      } else {
        setup_data.subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row);
      }
      // PREDICATE
      if (p_content[1] == "preformatted") {
        setup_data.predicate = p_content[0];
      } else {
        setup_data.predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row);
      }
      // OBJECT
      if (o_content[1] == "preformatted") {
        setup_data.object = o_content[0];
      } else {
        setup_data.object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row);
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    setup_data.res = setup_data.subject + " " + setup_data.predicate + " " + setup_data.object + " .\n";
    setup_data.triple_counter++;

    setup_data.buffered_res += setup_data.res;
    setup_data.write_cnt++;

    if (setup_data.write_cnt == setup_data.buffer_limit) {
      setup_data.write_cnt = 0;
      ////// SERIALIZE //////
      setup_data.outputFile << setup_data.buffered_res;
      setup_data.buffered_res = "";
    }
  }
  ////// SERIALIZE //////
  setup_data.outputFile << setup_data.buffered_res;

  file.close();

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
                                                         std::unordered_set<std::string>& unique_triple) {
  ///// Setup /////
  SetupData setup_data = initialize_setup_dependent(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  std::ifstream file(input_file_name);
  if (!file.is_open()) {
    std::cout << "Error opening file!" << std::endl;
    std::exit(1);
  }

  // Read and split header
  std::getline(file, setup_data.line);
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices = get_attribute_index(file, header, projected_attributes);

  // Project header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }

  // Iterate over file line by line
  while (std::getline(file, setup_data.line)) {
    setup_data.split_line = split_csv_line(setup_data.line, ',');

    ////// PROJECTION //////
    setup_data.projected_row.clear();
    for (int i : projected_indices) {
      setup_data.projected_row.push_back(setup_data.split_line[i]);
    }

    // Check for NULL values
    setup_data.skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(setup_data.projected_row.begin(), setup_data.projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        setup_data.skip = true;
        break;
      }
    }
    if (setup_data.skip) {
      continue;
    }

    // Eliminate duplicates
    setup_data.hash = combinedHash(setup_data.projected_row);
    if (!(setup_data.unique_hashes.insert(setup_data.hash).second)) {
      continue;
    }

    // Create map
    std::unordered_map<std::string, std::string> row;
    for (int i = 0; i < setup_data.projected_row.size(); i++) {
      row[projected_header[i]] = setup_data.projected_row[i];
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (s_content[1] == "preformatted") {
        setup_data.subject = s_content[0];
      } else {
        setup_data.subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row);
      }
      // PREDICATE
      if (p_content[1] == "preformatted") {
        setup_data.predicate = p_content[0];
      } else {
        setup_data.predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row);
      }
      // OBJECT
      if (o_content[1] == "preformatted") {
        setup_data.object = o_content[0];
      } else {
        setup_data.object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row);
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    setup_data.res = setup_data.subject + " " + setup_data.predicate + " " + setup_data.object + " .\n";
    setup_data.triple_counter++;

    unique_triple.insert(setup_data.res);
  }

  file.close();

  return unique_triple;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Helper to store and parse ra expression
struct ParsedContent {
  fs::path output_file_name;
  std::string base_uri;
  std::string input_file_name;
  std::vector<std::string> projected_attributes;
  size_t generated_triple;
  std::vector<std::string> s_content;
  std::vector<std::string> p_content;
  std::vector<std::string> o_content;
  std::vector<std::string> g_content;
  bool generate_graph;
};

ParsedContent parse_information(const std::string& information) {
  ParsedContent data;

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

  data.generated_triple = 0;

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

size_t standalone_simple_mapping(const std::string& information) {
  ParsedContent info = parse_information(information);
  ////////////////////////////////////////////////////////////
  // Execute
  try {
    if (info.generate_graph == false) {
      // handle without graph //
      // Check if all entrries are constant
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant") {
        handle_constant(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);  // Graph is dummy
        info.generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        handle_constant_preformatted(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        info.generated_triple = 1;
      } else {
        info.generated_triple = execute_simple(info.input_file_name, info.output_file_name, info.base_uri,
                                               info.projected_attributes, info.s_content, info.p_content, info.o_content);
      } 
    } else {
      // Handle with graph //
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant" && info.g_content[1] == "constant") {
        handle_constant(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        info.generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted" && info.g_content[1] == "preformatted") {
        handle_constant_preformatted(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name);
        info.generated_triple = 1;
      } else {
        // If not constant handle normal
        info.generated_triple = execute_simple_with_graph(info.input_file_name, info.output_file_name, info.base_uri,
                                                          info.projected_attributes, info.s_content, info.p_content, info.o_content, info.g_content);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      std::cout << e.what() << std::endl;
      std::exit(1);
    }
  } catch (...) {
    std::cout << "Unknown exception caught!" << std::endl;
    std::exit(1);
  }

  return info.generated_triple;
}

std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple) {
  // Extract relevant parts
  ParsedContent info = parse_information(information);

  ////////////////////////////////////////////////////////////
  // Execute

  try {
    if (info.generate_graph == false) {
      // handle without graph //

      // Check if all entrries are constant
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant") {
        std::vector<std::string> g_content;
        unique_triple = handle_constant_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
        info.generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted") {
        unique_triple = handle_constant_preformatted_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
        info.generated_triple = 1;
      } else {
        unique_triple = execute_simple_dependent(info.input_file_name, info.output_file_name, info.base_uri, info.projected_attributes,
                                                 info.s_content, info.p_content, info.o_content, unique_triple);
      }
    } else {
      // Handle with graph
      if (info.s_content[1] == "constant" && info.p_content[1] == "constant" && info.o_content[1] == "constant" && info.g_content[1] == "constant") {
        unique_triple = handle_constant_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
        info.generated_triple = 1;
      } else if (info.s_content[1] == "preformatted" && info.p_content[1] == "preformatted" && info.o_content[1] == "preformatted" && info.g_content[1] == "preformatted") {
        unique_triple = handle_constant_preformatted_dependent(info.s_content, info.p_content, info.o_content, info.g_content, info.output_file_name, unique_triple);
        info.generated_triple = 1;
      } else {
        // If not constant handle normal
        unique_triple = execute_simple_with_graph_dependent(info.input_file_name, info.output_file_name, info.base_uri, info.projected_attributes,
                                                            info.s_content, info.p_content, info.o_content, info.g_content, unique_triple);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      std::cout << e.what() << std::endl;
      std::exit(1);
    }
  } catch (...) {
    std::cout << "Unknown exception caught!" << std::endl;
    std::exit(1);
  }

  return unique_triple;
}
