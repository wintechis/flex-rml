#include "simple_executor.h"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "definitions.h"
#include "utils.h"
#include "xxhash.h"

namespace fs = std::filesystem;

constexpr std::size_t kOutputBufferReserve = 64 * 1024;

struct TripleHash128 {
  XXH64_hash_t low = 0;
  XXH64_hash_t high = 0;

  bool operator==(const TripleHash128& other) const {
    return low == other.low && high == other.high;
  }
};

struct TripleHash128Hasher {
  using is_avalanching = void;

  auto operator()(const TripleHash128& key) const noexcept -> std::uint64_t {
    return key.low ^ (key.high + 0x9e3779b97f4a7c15ULL + (key.low << 6) + (key.low >> 2));
  }
};

using TripleHashSet = ankerl::unordered_dense::set<TripleHash128, TripleHash128Hasher>;

static TripleHash128 hash_triple(std::string_view triple) {
  const XXH128_hash_t hash = XXH3_128bits(triple.data(), triple.size());
  return TripleHash128{hash.low64, hash.high64};
}

static void create_parent_directories_if_needed(const fs::path& path) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

static const std::string kConstantTermMapType = "constant";

struct RuntimeTerm {
  const std::string* value;
  const std::string* map_type;
};

static RuntimeTerm resolve_runtime_term(const std::vector<std::string>& content,
                                        int line_count,
                                        const std::string& input_file_name,
                                        std::unordered_map<std::string, std::string>& row,
                                        std::string& function_value) {
  if (content[1] != "function") {
    return {&content[0], &content[1]};
  }

  function_value = handle_function_call(content[0], line_count, input_file_name, row);
  return {&function_value, &kConstantTermMapType};
}

static void initialize_row_map(std::unordered_map<std::string, std::string>& row,
                               const std::vector<std::string>& projected_header) {
  row.clear();
  row.reserve(projected_header.size());
  for (const auto& name : projected_header) {
    row.try_emplace(name);
  }
}

static void update_row_map(std::unordered_map<std::string, std::string>& row,
                           const std::vector<std::string>& projected_header,
                           const std::vector<std::string>& projected_row) {
  for (std::size_t i = 0; i < projected_row.size(); i++) {
    auto it = row.find(projected_header[i]);
    if (it != row.end()) {
      it->second = projected_row[i];
    }
  }
}

static void project_row_into(const std::vector<std::string>& split_line,
                             const std::vector<int>& projected_indices,
                             std::vector<std::string>& projected_row) {
  if (projected_row.size() < projected_indices.size()) {
    projected_row.resize(projected_indices.size());
  }
  for (std::size_t i = 0; i < projected_indices.size(); i++) {
    projected_row[i] = split_line[projected_indices[i]];
  }
  projected_row.resize(projected_indices.size());
}

///////////////////////////////////////////////////////////////
/// Data setup
///////////////////////////////////////////////////////////////
struct SetupData {
  TripleHashSet unique_triple_hashes;

  std::string line;
  std::vector<std::string> split_line;
  std::vector<std::string> projected_row;
  std::unordered_map<std::string, std::string> row;

  size_t triple_counter = 0;
  size_t write_cnt = 0;
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
  data.row.reserve(32);

  data.subject.reserve(512);
  data.predicate.reserve(512);
  data.object.reserve(512);
  data.graph.reserve(512);
  data.res.reserve(2048);
  data.buffered_res.reserve(kOutputBufferReserve);

  // Open output file
  create_parent_directories_if_needed(output_file_name);
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
  data.row.reserve(32);

  data.subject.reserve(512);
  data.predicate.reserve(512);
  data.object.reserve(512);
  data.graph.reserve(512);
  data.res.reserve(2048);
  data.buffered_res.reserve(kOutputBufferReserve);

  return data;
}

///////////////////////////////////////////////////////////////7
/// HELPER FUNCTIONS
//////////////////////////////////////////////////////////////
std::vector<int> get_attribute_index(std::istream& file, const std::vector<std::string>& header, const std::vector<std::string>& projected_attributes) {
  // Get indices for projected attributes
  std::vector<int> projected_indices;

  for (const auto& attr : projected_attributes) {
    if (attr == "") {
      continue;
    }

    auto it = std::find_if(header.begin(), header.end(), [&](std::string_view field) {
      return field == attr;
    });
    if (it != header.end()) {
      projected_indices.push_back(std::distance(header.begin(), it));
    } else {
      std::cerr << "Attribute not found: '" << attr << "'" << std::endl;
      std::exit(1);
    }
  }

  return projected_indices;
}

// Open File or from map
static std::unique_ptr<std::istream> open_from_map_or_file(
    const std::unordered_map<std::string, std::string>& mem,
    const std::string& path) {
  if (auto it = mem.find(path); it != mem.end()) {
    return std::make_unique<std::istringstream>(it->second);
  }

  auto f = std::make_unique<std::ifstream>(path);
  if (!f->is_open()) {
    throw std::runtime_error("Could not open logical source: " + path);
  }
  return f;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
int execute_simple_with_graph(const std::string& input_file_name,
                              const fs::path& output_file_name,
                              const std::string& base_uri,
                              const std::vector<std::string>& projected_attributes,
                              const std::vector<std::string>& s_content,
                              const std::vector<std::string>& p_content,
                              const std::vector<std::string>& o_content,
                              const std::vector<std::string>& g_content,
                              const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  SetupData setup_data = initialize_setup(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  auto file = open_from_map_or_file(data_map, input_file_name);

  // Get index of attributes in header
  // Read and split header
  if (!std::getline(*file, setup_data.line)) {
    return 0;
  }
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices;
  if (!(projected_attributes.size() == 1 && projected_attributes[0] == "")) {
    projected_indices = get_attribute_index(*file, header, projected_attributes);
  }

  // Project Header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }
  initialize_row_map(setup_data.row, projected_header);

  // Iterate over file line by line
  int line_count = 0;
  // Iterate over file line by line
  while (std::getline(*file, setup_data.line)) {
    split_csv_line_into(setup_data.line, ',', setup_data.split_line);

    ////// PROJECTION //////
    project_row_into(setup_data.split_line, projected_indices, setup_data.projected_row);

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

    auto& row = setup_data.row;
    update_row_map(row, projected_header, setup_data.projected_row);

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    std::string g_function_value;
    const RuntimeTerm s_term = resolve_runtime_term(s_content, line_count, input_file_name, row, s_function_value);
    const RuntimeTerm p_term = resolve_runtime_term(p_content, line_count, input_file_name, row, p_function_value);
    const RuntimeTerm o_term = resolve_runtime_term(o_content, line_count, input_file_name, row, o_function_value);
    const RuntimeTerm g_term = resolve_runtime_term(g_content, line_count, input_file_name, row, g_function_value);
    if (*s_term.value == "NULL" || *p_term.value == "NULL" || *o_term.value == "NULL" || *g_term.value == "NULL") {
      continue;
    }
    if (o_content.size() > 5 && o_content[5] != "None" &&
        handle_function_call(o_content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (*s_term.map_type == "preformatted") {
        setup_data.subject = *s_term.value;
      } else {
        create_operator_into(*s_term.value, *s_term.map_type, s_content[2], "", "", base_uri, row, setup_data.subject);
      }
      // PREDICATE
      if (*p_term.map_type == "preformatted") {
        setup_data.predicate = *p_term.value;
      } else {
        create_operator_into(*p_term.value, *p_term.map_type, p_content[2], "", "", base_uri, row, setup_data.predicate);
      }
      // OBJECT
      if (*o_term.map_type == "preformatted") {
        setup_data.object = *o_term.value;
      } else {
        create_operator_into(*o_term.value, *o_term.map_type, o_content[2], o_content[3], o_content[4], base_uri, row, setup_data.object);
      }
      // GRAPH
      if (*g_term.map_type == "preformatted") {
        setup_data.graph = *g_term.value;
      } else {
        create_operator_into(*g_term.value, *g_term.map_type, g_content[2], "", "", base_uri, row, setup_data.graph);
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    setup_data.res = format_statement(setup_data.subject, setup_data.predicate, setup_data.object, setup_data.graph);
    if (!setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second) {
      continue;
    }
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
                                                                    std::unordered_set<std::string>& unique_triple,
                                                                    const std::unordered_map<std::string, std::string>& data_map) {
  // Setup
  SetupData setup_data = initialize_setup_dependent(output_file_name);

  //////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////
  // Open input
  auto file = open_from_map_or_file(data_map, input_file_name);

  //////////////////////////////////////////////////////////////////////

  // Get index of attributes in header
  // Read and split header
  if (!std::getline(*file, setup_data.line)) {
    return unique_triple;
  }
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices;
  if (!(projected_attributes.size() == 1 && projected_attributes[0] == "")) {
    projected_indices = get_attribute_index(*file, header, projected_attributes);
  }

  // Project Header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }
  initialize_row_map(setup_data.row, projected_header);


  // Iterate over file line by line
  int line_count = 0;
  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_csv_line_into(setup_data.line, ',', setup_data.split_line);

    ////// PROJECTION //////
    project_row_into(setup_data.split_line, projected_indices, setup_data.projected_row);

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

    auto& row = setup_data.row;
    update_row_map(row, projected_header, setup_data.projected_row);

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    std::string g_function_value;
    const RuntimeTerm s_term = resolve_runtime_term(s_content, line_count, input_file_name, row, s_function_value);
    const RuntimeTerm p_term = resolve_runtime_term(p_content, line_count, input_file_name, row, p_function_value);
    const RuntimeTerm o_term = resolve_runtime_term(o_content, line_count, input_file_name, row, o_function_value);
    const RuntimeTerm g_term = resolve_runtime_term(g_content, line_count, input_file_name, row, g_function_value);
    if (*s_term.value == "NULL" || *p_term.value == "NULL" || *o_term.value == "NULL" || *g_term.value == "NULL") {
      continue;
    }
    if (o_content.size() > 5 && o_content[5] != "None" &&
        handle_function_call(o_content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (*s_term.map_type == "preformatted") {
        setup_data.subject = *s_term.value;
      } else {
        create_operator_into(*s_term.value, *s_term.map_type, s_content[2], "", "", base_uri, row, setup_data.subject);
      }
      // PREDICATE
      if (*p_term.map_type == "preformatted") {
        setup_data.predicate = *p_term.value;
      } else {
        create_operator_into(*p_term.value, *p_term.map_type, p_content[2], "", "", base_uri, row, setup_data.predicate);
      }
      // OBJECT
      if (*o_term.map_type == "preformatted") {
        setup_data.object = *o_term.value;
      } else {
        create_operator_into(*o_term.value, *o_term.map_type, o_content[2], o_content[3], o_content[4], base_uri, row, setup_data.object);
      }
      // GRAPH
      if (*g_term.map_type == "preformatted") {
        setup_data.graph = *g_term.value;
      } else {
        create_operator_into(*g_term.value, *g_term.map_type, g_content[2], "", "", base_uri, row, setup_data.graph);
      }
    } catch (const std::runtime_error& e) {
      if (continue_on_error == false) {
        std::cout << e.what() << std::endl;
        std::exit(1);
      } else {
        continue;
      }
    }

    setup_data.res = format_statement(setup_data.subject, setup_data.predicate, setup_data.object, setup_data.graph);
    if (!unique_triple.insert(setup_data.res).second) {
      continue;
    }
    setup_data.triple_counter++;
  }

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
                   const std::vector<std::string>& o_content,
                   const std::unordered_map<std::string, std::string>& data_map) {
  ///// Setup /////
  SetupData setup_data = initialize_setup(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input file
  auto file = open_from_map_or_file(data_map, input_file_name);

  // Get index of attributes in header
  if (!std::getline(*file, setup_data.line)) {
    return 0;
  }
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');
  std::vector<int> projected_indices;
  if (!(projected_attributes.size() == 1 && projected_attributes[0] == "")) {
    projected_indices = get_attribute_index(*file, header, projected_attributes);
  }

  // Project Header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }
  initialize_row_map(setup_data.row, projected_header);

  int line_count = 0;
  // Iterate over file line by line
  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_csv_line_into(setup_data.line, ',', setup_data.split_line);

    ////// PROJECTION //////
    project_row_into(setup_data.split_line, projected_indices, setup_data.projected_row);

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

    auto& row = setup_data.row;
    update_row_map(row, projected_header, setup_data.projected_row);

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    const RuntimeTerm s_term = resolve_runtime_term(s_content, line_count, input_file_name, row, s_function_value);
    const RuntimeTerm p_term = resolve_runtime_term(p_content, line_count, input_file_name, row, p_function_value);
    const RuntimeTerm o_term = resolve_runtime_term(o_content, line_count, input_file_name, row, o_function_value);
    if (*s_term.value == "NULL" || *p_term.value == "NULL" || *o_term.value == "NULL") {
      continue;
    }
    if (o_content.size() > 5 && o_content[5] != "None" &&
        handle_function_call(o_content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (*s_term.map_type == "preformatted") {
        setup_data.subject = *s_term.value;
      } else {
        create_operator_into(*s_term.value, *s_term.map_type, s_content[2], "", "", base_uri, row, setup_data.subject);
      }
      // PREDICATE
      if (*p_term.map_type == "preformatted") {
        setup_data.predicate = *p_term.value;
      } else {
        create_operator_into(*p_term.value, *p_term.map_type, p_content[2], "", "", base_uri, row, setup_data.predicate);
      }
      // OBJECT
      if (*o_term.map_type == "preformatted") {
        setup_data.object = *o_term.value;
      } else {
        create_operator_into(*o_term.value, *o_term.map_type, o_content[2], o_content[3], o_content[4], base_uri, row, setup_data.object);
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
    if (!setup_data.unique_triple_hashes.insert(hash_triple(setup_data.res)).second) {
      continue;
    }
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
                                                         std::unordered_set<std::string>& unique_triple,
                                                         const std::unordered_map<std::string, std::string>& data_map) {
  ///// Setup /////
  SetupData setup_data = initialize_setup_dependent(output_file_name);

  //////////////////////////////////////////////////////////////////////
  // Open input
  auto file = open_from_map_or_file(data_map, input_file_name);

  //////////////////////////////////////////////////////////////////////
  // Read and split header
  if (!std::getline(*file, setup_data.line)) {
    return unique_triple;
  }
  std::vector<std::string> header = split_csv_line(setup_data.line, ',');

  std::vector<int> projected_indices;
  if (!(projected_attributes.size() == 1 && projected_attributes[0] == "")) {
    projected_indices = get_attribute_index(*file, header, projected_attributes);
  }

  // Project header
  std::vector<std::string> projected_header;
  for (int i : projected_indices) {
    projected_header.push_back(header[i]);
  }
  initialize_row_map(setup_data.row, projected_header);

  // Iterate over file line by line
  int line_count = 0;

  // Check if funciton is needed
  bool function_called = (s_content[1] == "function") || (p_content[1] == "function") || (o_content[1] == "function");

  while (std::getline(*file, setup_data.line)) {
    line_count++;

    split_csv_line_into(setup_data.line, ',', setup_data.split_line);

    ////// PROJECTION //////
    project_row_into(setup_data.split_line, projected_indices, setup_data.projected_row);

    // Check for NULL values
    setup_data.skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(setup_data.projected_row.begin(), setup_data.projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        setup_data.skip = true;
        break;
      }
    }

    if (setup_data.skip && !function_called) {
      continue;
    }

    auto& row = setup_data.row;
    update_row_map(row, projected_header, setup_data.projected_row);

    std::string s_function_value;
    std::string p_function_value;
    std::string o_function_value;
    const RuntimeTerm s_term = resolve_runtime_term(s_content, line_count, input_file_name, row, s_function_value);
    const RuntimeTerm p_term = resolve_runtime_term(p_content, line_count, input_file_name, row, p_function_value);
    const RuntimeTerm o_term = resolve_runtime_term(o_content, line_count, input_file_name, row, o_function_value);
    if (*s_term.value == "NULL" || *p_term.value == "NULL" || *o_term.value == "NULL") {
      continue;
    }
    if (o_content.size() > 5 && o_content[5] != "None" &&
        handle_function_call(o_content[5], line_count, input_file_name, row) != "true") {
      continue;
    }

    ////// CREATE //////
    try {
      // SUBJECT
      if (*s_term.map_type == "preformatted") {
        setup_data.subject = *s_term.value;
      } else {
        create_operator_into(*s_term.value, *s_term.map_type, s_content[2], "", "", base_uri, row, setup_data.subject);
      }
      // PREDICATE
      if (*p_term.map_type == "preformatted") {
        setup_data.predicate = *p_term.value;
      } else {
        create_operator_into(*p_term.value, *p_term.map_type, p_content[2], "", "", base_uri, row, setup_data.predicate);
      }
      // OBJECT
      if (*o_term.map_type == "preformatted") {
        setup_data.object = *o_term.value;
      } else {
        create_operator_into(*o_term.value, *o_term.map_type, o_content[2], o_content[3], o_content[4], base_uri, row, setup_data.object);
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
    if (!unique_triple.insert(setup_data.res).second) {
      continue;
    }
    setup_data.triple_counter++;
  }

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

size_t standalone_simple_mapping(const std::string& information, const std::unordered_map<std::string, std::string>& data_map) {
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
                                               info.projected_attributes, info.s_content, info.p_content, info.o_content, data_map);
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
                                                          info.projected_attributes, info.s_content, info.p_content, info.o_content, info.g_content, data_map);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      throw;
    }
  } catch (...) {
    throw std::runtime_error("Unknown exception caught while executing simple mapping.");
  }

  return info.generated_triple;
}

std::unordered_set<std::string> dependent_simple_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple, const std::unordered_map<std::string, std::string>& data_map) {
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
                                                 info.s_content, info.p_content, info.o_content, unique_triple, data_map);
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
                                                            info.s_content, info.p_content, info.o_content, info.g_content, unique_triple, data_map);
      }
    }
  } catch (const std::runtime_error& e) {
    if (continue_on_error == false) {
      throw;
    }
  } catch (...) {
    throw std::runtime_error("Unknown exception caught while executing dependent simple mapping.");
  }

  return unique_triple;
}
