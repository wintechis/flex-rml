#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "definitions.h"
#include "utils.h"

namespace fs = std::filesystem;
///////////////////////////////////////////////////////////////////////////////////////////////////////77
//// HELPER FUNCTIONS ////

// Function to join strings with a delimiter
std::string join(const std::vector<std::string>& vec, const std::string& delimiter) {
  std::string joined;
  for (size_t i = 0; i < vec.size(); ++i) {
    joined += vec[i];
    if (i < vec.size() - 1)
      joined += delimiter;
  }
  return joined;
}

// Build header mapping and return header vector and index map.
std::pair<std::vector<std::string>, std::unordered_map<std::string, int>> build_header(const std::string& header_line, const std::string& prefix) {
  auto original = split_csv_line(header_line, ',');

  std::vector<std::string> headers;
  std::unordered_map<std::string, int> header_idx;

  for (int i = 0; i < original.size(); ++i) {
    std::string full = prefix + "_" + original[i];
    headers.push_back(full);
    header_idx[full] = i;
  }

  return {headers, header_idx};
}

// Get projected column indices from a header mapping
std::vector<int> get_projected_indices(const std::vector<std::string>& proj_attrs, const std::string& prefix, const std::unordered_map<std::string, int>& header_idx) {
  std::vector<int> indices;

  for (const auto& attr : proj_attrs) {
    std::string full = prefix + "_" + attr;
    auto it = header_idx.find(full);  // Efficient lookup

    if (it == header_idx.end()) {
      std::cerr << "Error: " << full << " not found in header." << std::endl;
      std::exit(1);
    }

    indices.push_back(it->second);
  }

  return indices;
}

// Determine the join index within the projected attributes.
int get_join_index(const std::vector<std::string>& proj_attrs, const std::string& prefix, const std::string& join_attr) {
  for (size_t i = 0; i < proj_attrs.size(); ++i) {
    if (prefix + "_" + proj_attrs[i] == join_attr) {
      return static_cast<int>(i);  // Found index
    }
  }

  std::cerr << "Error: Join attribute " << join_attr << " not found in projected attributes." << std::endl;
  std::exit(1);
}

std::unordered_multimap<std::string, std::vector<std::string>> build_hash_table(std::ifstream& input_file,
                                                                                const std::vector<int>& projected_indeces,
                                                                                int filtered_join_index) {
  // Reserve for our hash table and duplicate check set.
  std::unordered_multimap<std::string, std::vector<std::string>> hash_table;
  hash_table.reserve(1024 * 1024 * 2);
  std::unordered_set<uint64_t> unique_hashes;
  unique_hashes.reserve(1024 * 1024);

  const size_t batch_size = 1000;
  std::vector<std::string> batch_lines;
  batch_lines.reserve(batch_size);
  std::string line;

  // Read the file in batches.
  while (std::getline(input_file, line)) {
    batch_lines.push_back(std::move(line));
    if (batch_lines.size() == batch_size) {
      // Process the current batch.
      for (const auto& batch_line : batch_lines) {
        auto split_line = split_csv_line(batch_line, ',');

        // Build the projected row.
        std::vector<std::string> projected_row;
        projected_row.reserve(projected_indeces.size());
        for (int idx : projected_indeces) {
          projected_row.push_back(split_line[idx]);
        }

        // Check for unwanted values.
        bool skip = false;
        for (const auto& target : values_to_skip) {
          if (std::any_of(projected_row.begin(), projected_row.end(),
                          [&target](const std::string& s) { return s == target; })) {
            skip = true;
            break;
          }
        }
        if (skip) continue;

        // Eliminate duplicates.
        uint64_t hash = combinedHash(projected_row);
        if (!unique_hashes.insert(hash).second) {
          continue;
        }

        // Insert into hash table.
        std::string key = projected_row[filtered_join_index];
        hash_table.emplace(std::move(key), std::move(projected_row));
      }
      batch_lines.clear();
    }
  }

  // Process any remaining lines that didn't fill a full batch.
  for (const auto& batch_line : batch_lines) {
    auto split_line = split_csv_line(batch_line, ',');
    std::vector<std::string> projected_row;
    projected_row.reserve(projected_indeces.size());
    for (int idx : projected_indeces) {
      projected_row.push_back(split_line[idx]);
    }

    bool skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(projected_row.begin(), projected_row.end(),
                      [&target](const std::string& s) { return s == target; })) {
        skip = true;
        break;
      }
    }
    if (skip) continue;

    uint64_t hash = combinedHash(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    std::string key = projected_row[filtered_join_index];
    hash_table.emplace(std::move(key), std::move(projected_row));
  }

  return hash_table;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int execute_complex(const fs::path& output_file_name,
                     const std::string& left_path,
                     const std::string& right_path,
                     const std::string& left_name,
                     const std::string& right_name,
                     const std::string& left_join_attr,
                     const std::string& right_join_attr,
                     const std::string& base_uri,
                     const std::vector<std::string>& projected_attributes_left,
                     const std::vector<std::string>& projected_attributes_right,
                     const std::vector<std::string>& s_content,
                     const std::vector<std::string>& p_content,
                     const std::vector<std::string>& o_content) {
  // Setup
  std::string line;
  std::unordered_set<uint64_t> unique_hashes;
  unique_hashes.reserve(1024 * 1024);
  size_t triple_counter = 0;

  size_t write_cnt = 0;
  size_t buffer_limit = 20000;
  std::string buffered_res;

  // Open output file
  fs::create_directories(output_file_name.parent_path());
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cerr << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }

  //////////////////////////////////////////////////////////////////////

  // Open CSV files
  std::ifstream left_file(left_path), right_file(right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(left_file, left_header_line);
  std::getline(right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  int left_filtered_join_index = get_join_index(projected_attributes_left, left_name, left_join_attr);
  int right_filtered_join_index = get_join_index(projected_attributes_right, right_name, right_join_attr);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file (store only projected columns).
  auto hash_table = build_hash_table(left_file, left_proj_indices, left_filtered_join_index);

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }

  // Process right file
  while (getline(right_file, line)) {
    auto split_line = split_csv_line(line, ',');

    std::vector<std::string> projected_row;
    ////// PROJECTION //////
    for (int i : right_proj_indices) {
      projected_row.push_back(split_line[i]);
    }

    // Check for unwanted values
    bool skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(projected_row.begin(), projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combinedHash(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    std::string key = projected_row[right_filtered_join_index];
    auto range = hash_table.equal_range(key);

    for (auto it = range.first; it != range.second; ++it) {
      // Combine left and right filtered rows
      std::vector<std::string> joined_row;
      joined_row.insert(joined_row.end(), it->second.begin(), it->second.end());
      joined_row.insert(joined_row.end(), projected_row.begin(), projected_row.end());

      // Map headers to values
      std::unordered_map<std::string, std::string> row_map;
      for (size_t i = 0; i < joined_headers.size(); ++i)
        row_map[joined_headers[i]] = joined_row[i];

      // Generate triple
      std::string subject;
      std::string predicate;
      std::string object;

      ////// CREATE //////
      try {
        // SUBJECT
        if (s_content[1] == "preformatted") {
          subject = s_content[0];
        } else {
          subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row_map);
        }
        // PREDICATE
        if (p_content[1] == "preformatted") {
          predicate = p_content[0];
        } else {
          predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row_map);
        }
        // OBJECT
        if (o_content[1] == "preformatted") {
          object = o_content[0];
        } else {
          object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row_map);
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        } else {
          continue;
        }
      }

      std::string res = subject + " " + predicate + " " + object + " .\n";
      triple_counter++;

      buffered_res += res;
      write_cnt++;

      if (write_cnt == buffer_limit) {
        write_cnt = 0;
        ////// SERIALIZE //////
        outputFile << buffered_res;
        buffered_res = "";
      }
    }
  }

  ////// SERIALIZE //////
  outputFile << buffered_res;

  right_file.close();
  left_file.close();

  return triple_counter;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unordered_set<std::string> execute_complex_dependent(const fs::path& output_file_name,
                                                          const std::string& left_path,
                                                          const std::string& right_path,
                                                          const std::string& left_name,
                                                          const std::string& right_name,
                                                          const std::string& left_join_attr,
                                                          const std::string& right_join_attr,
                                                          const std::string& base_uri,
                                                          const std::vector<std::string>& projected_attributes_left,
                                                          const std::vector<std::string>& projected_attributes_right,
                                                          const std::vector<std::string>& s_content,
                                                          const std::vector<std::string>& p_content,
                                                          const std::vector<std::string>& o_content,
                                                          std::unordered_set<std::string>& unique_triple) {
  // Setup
  std::string line;
  std::unordered_set<uint64_t> unique_hashes;

  //////////////////////////////////////////////////////////////////////
  // Open CSV files
  std::ifstream left_file(left_path), right_file(right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(left_file, left_header_line);
  std::getline(right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  int left_filtered_join_index = get_join_index(projected_attributes_left, left_name, left_join_attr);
  int right_filtered_join_index = get_join_index(projected_attributes_right, right_name, right_join_attr);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file using the left join index
  auto hash_table = build_hash_table(left_file, left_proj_indices, left_filtered_join_index);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }

  // Process right file
  while (getline(right_file, line)) {
    auto split_line = split_csv_line(line, ',');

    std::vector<std::string> projected_row;
    ////// PROJECTION //////
    for (int i : right_proj_indices) {
      projected_row.push_back(split_line[i]);
    }

    // Check for unwanted values
    bool skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(projected_row.begin(), projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combinedHash(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    std::string key = projected_row[right_filtered_join_index];
    auto range = hash_table.equal_range(key);

    for (auto it = range.first; it != range.second; ++it) {
      // Combine left and right filtered rows
      std::vector<std::string> joined_row;
      joined_row.insert(joined_row.end(), it->second.begin(), it->second.end());
      joined_row.insert(joined_row.end(), projected_row.begin(), projected_row.end());

      // Map headers to values
      std::unordered_map<std::string, std::string> row_map;
      for (size_t i = 0; i < joined_headers.size(); ++i)
        row_map[joined_headers[i]] = joined_row[i];

      std::string subject;
      std::string predicate;
      std::string object;
      try {
        // SUBJECT
        if (s_content[1] == "preformatted") {
          subject = s_content[0];
        } else {
          subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row_map);
        }
        // PREDICATE
        if (p_content[1] == "preformatted") {
          predicate = p_content[0];
        } else {
          predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row_map);
        }
        // OBJECT
        if (o_content[1] == "preformatted") {
          object = o_content[0];
        } else {
          object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row_map);
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        } else {
          continue;
        }
      }

      std::string res = subject + " " + predicate + " " + object + " .\n";

      unique_triple.insert(res);
    }
  }
  return unique_triple;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int execute_complex_with_graph(const fs::path& output_file_name,
                               const std::string& left_path, const std::string& right_path,
                               const std::string& left_name,
                               const std::string& right_name,
                               const std::string& left_join_attr,
                               const std::string& right_join_attr,
                               const std::string& base_uri,
                               const std::vector<std::string>& projected_attributes_left,
                               const std::vector<std::string>& projected_attributes_right,
                               const std::vector<std::string>& s_content,
                               const std::vector<std::string>& p_content,
                               const std::vector<std::string>& o_content,
                               const std::vector<std::string>& g_content) {
  // Setup
  std::string line;
  std::unordered_set<uint64_t> unique_hashes;
  size_t triple_counter = 0;

  size_t write_cnt = 0;
  size_t buffer_limit = 20000;
  std::string buffered_res;

  // Open output file
  fs::create_directories(output_file_name.parent_path());
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cerr << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }

  //////////////////////////////////////////////////////////////////////
  // Open CSV files
  std::ifstream left_file(left_path), right_file(right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(left_file, left_header_line);
  std::getline(right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  int left_filtered_join_index = get_join_index(projected_attributes_left, left_name, left_join_attr);
  int right_filtered_join_index = get_join_index(projected_attributes_right, right_name, right_join_attr);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file (store only projected columns).
  auto hash_table = build_hash_table(left_file, left_proj_indices, left_filtered_join_index);

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }

  // Process right file
  while (getline(right_file, line)) {
    auto split_line = split_csv_line(line, ',');

    std::vector<std::string> projected_row;
    ////// PROJECTION //////
    for (int i : right_proj_indices) {
      projected_row.push_back(split_line[i]);
    }

    // Check for unwanted values
    bool skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(projected_row.begin(), projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combinedHash(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    std::string key = projected_row[right_filtered_join_index];
    auto range = hash_table.equal_range(key);

    for (auto it = range.first; it != range.second; ++it) {
      // Combine left and right filtered rows
      std::vector<std::string> joined_row;
      joined_row.insert(joined_row.end(), it->second.begin(), it->second.end());
      joined_row.insert(joined_row.end(), projected_row.begin(), projected_row.end());

      // Map headers to values
      std::unordered_map<std::string, std::string> row_map;
      for (size_t i = 0; i < joined_headers.size(); ++i)
        row_map[joined_headers[i]] = joined_row[i];

      ////// CREATE //////
      std::string subject;
      std::string predicate;
      std::string object;
      std::string graph;

      try {
        // SUBJECT
        if (s_content[1] == "preformatted") {
          subject = s_content[0];
        } else {
          subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row_map);
        }
        // PREDICATE
        if (p_content[1] == "preformatted") {
          predicate = p_content[0];
        } else {
          predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row_map);
        }
        // OBJECT
        if (o_content[1] == "preformatted") {
          object = o_content[0];
        } else {
          object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row_map);
        }
        // GRAPH
        if (g_content[1] == "preformatted") {
          graph = g_content[0];
        } else {
          graph = create_operator(g_content[0], g_content[1], g_content[2], "", "", base_uri, row_map);
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        } else {
          continue;
        }
      }

      std::string res = subject + " " + predicate + " " + object + " " + graph + " .\n";
      triple_counter++;

      buffered_res += res;
      write_cnt++;

      if (write_cnt == buffer_limit) {
        write_cnt = 0;
        ////// SERIALIZE //////
        outputFile << buffered_res;
        buffered_res = "";
      }
    }
  }
  ////// SERIALIZE //////
  outputFile << buffered_res;

  right_file.close();
  left_file.close();

  return triple_counter;
}

//////////////////////////////////////////////////////////////

std::unordered_set<std::string> execute_complex_with_graph_dependent(const fs::path& output_file_name,
                                                                     const std::string& left_path,
                                                                     const std::string& right_path,
                                                                     const std::string& left_name,
                                                                     const std::string& right_name,
                                                                     const std::string& left_join_attr,
                                                                     const std::string& right_join_attr,
                                                                     const std::string& base_uri,
                                                                     const std::vector<std::string>& projected_attributes_left,
                                                                     const std::vector<std::string>& projected_attributes_right,
                                                                     const std::vector<std::string>& s_content,
                                                                     const std::vector<std::string>& p_content,
                                                                     const std::vector<std::string>& o_content,
                                                                     const std::vector<std::string>& g_content,
                                                                     std::unordered_set<std::string>& unique_triple) {
  // Setup
  std::string line;
  std::unordered_set<uint64_t> unique_hashes;

  //////////////////////////////////////////////////////////////////////
  // Open CSV files
  std::ifstream left_file(left_path), right_file(right_path);
  if (!left_file || !right_file) {
    std::cerr << "Error opening input files." << std::endl;
    std::exit(1);
  }

  // Read header lines
  std::string left_header_line, right_header_line;
  std::getline(left_file, left_header_line);
  std::getline(right_file, right_header_line);

  // Build header mappings for left and right files.
  auto [left_headers, left_header_idx] = build_header(left_header_line, left_name);
  auto [right_headers, right_header_idx] = build_header(right_header_line, right_name);

  auto left_proj_indices = get_projected_indices(projected_attributes_left, left_name, left_header_idx);
  auto right_proj_indices = get_projected_indices(projected_attributes_right, right_name, right_header_idx);

  int left_filtered_join_index = get_join_index(projected_attributes_left, left_name, left_join_attr);
  int right_filtered_join_index = get_join_index(projected_attributes_right, right_name, right_join_attr);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Build hash table from left file using the left join index
  auto hash_table = build_hash_table(left_file, left_proj_indices, left_filtered_join_index);

  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Prepare joined headers for output.
  std::vector<std::string> joined_headers;
  for (const auto& attr : projected_attributes_left) {
    joined_headers.push_back(left_name + "_" + attr);
  }
  for (const auto& attr : projected_attributes_right) {
    joined_headers.push_back(right_name + "_" + attr);
  }

  // Process right file
  while (getline(right_file, line)) {
    auto split_line = split_csv_line(line, ',');

    std::vector<std::string> projected_row;
    ////// PROJECTION //////
    for (int i : right_proj_indices) {
      projected_row.push_back(split_line[i]);
    }

    // Check for unwanted values
    bool skip = false;
    for (const auto& target : values_to_skip) {
      if (std::any_of(projected_row.begin(), projected_row.end(), [&target](const std::string& s) { return s == target; })) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    // Eliminate duplicates using hash
    uint64_t hash = combinedHash(projected_row);
    if (!unique_hashes.insert(hash).second) {
      continue;
    }

    std::string key = projected_row[right_filtered_join_index];
    auto range = hash_table.equal_range(key);

    for (auto it = range.first; it != range.second; ++it) {
      // Combine left and right filtered rows
      std::vector<std::string> joined_row;
      joined_row.insert(joined_row.end(), it->second.begin(), it->second.end());
      joined_row.insert(joined_row.end(), projected_row.begin(), projected_row.end());

      // Map headers to values
      std::unordered_map<std::string, std::string> row_map;
      for (size_t i = 0; i < joined_headers.size(); ++i)
        row_map[joined_headers[i]] = joined_row[i];

      ////// CREATE //////
      std::string subject;
      std::string predicate;
      std::string object;
      std::string graph;
      try {
        // SUBJECT
        if (s_content[1] == "preformatted") {
          subject = s_content[0];
        } else {
          subject = create_operator(s_content[0], s_content[1], s_content[2], "", "", base_uri, row_map);
        }
        // PREDICATE
        if (p_content[1] == "preformatted") {
          predicate = p_content[0];
        } else {
          predicate = create_operator(p_content[0], p_content[1], p_content[2], "", "", base_uri, row_map);
        }
        // OBJECT
        if (o_content[1] == "preformatted") {
          object = o_content[0];
        } else {
          object = create_operator(o_content[0], o_content[1], o_content[2], o_content[3], o_content[4], base_uri, row_map);
        }
        // GRAPH
        if (g_content[1] == "preformatted") {
          graph = g_content[0];
        } else {
          graph = create_operator(g_content[0], g_content[1], g_content[2], "", "", base_uri, row_map);
        }
      } catch (const std::runtime_error& e) {
        if (continue_on_error == false) {
          std::cout << e.what() << std::endl;
          std::exit(1);
        } else {
          continue;
        }
      }

      std::string res = subject + " " + predicate + " " + object + " " + graph + " .\n";

      unique_triple.insert(res);
    }
  }
  return unique_triple;
}

//////////////////////////////////////////////////////////////
size_t standalone_complex_mapping(const std::string& information) {
  // Extract relevant parts
  std::vector<std::string> split_info = split_by_substring(information, "\n");
  if (split_info.size() != 7) {
    std::cout << "Plan is too long. Got size: " << split_info.size() << std::endl;
    std::exit(1);
  }
  fs::path output_file_name = split_info[4];
  std::string base_uri = split_info[5];

  std::vector<std::string> split_info_first = split_by_substring(split_info[0], "|||");
  std::vector<std::string> split_info_second = split_by_substring(split_info[1], "|||");
  std::vector<std::string> split_info_third = split_by_substring(split_info[2], "|||");
  std::vector<std::string> split_info_fourth = split_by_substring(split_info[3], "|||");

  std::string left_path = split_info_first[1];
  std::vector<std::string> projected_attributes_left = split_by_substring(split_info_first[2], "===");
  std::string left_name = split_info_first[3];

  std::string right_path = split_info_second[1];
  std::vector<std::string> projected_attributes_right = split_by_substring(split_info_second[2], "===");
  std::string right_name = split_info_second[3];

  std::vector<std::string> join_mapping = split_by_substring(split_info_third[1], "===");
  std::string left_join_attr = join_mapping[0];
  std::string right_join_attr = join_mapping[1];

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  size_t generated_triple = 0;

  std::vector<std::string> s_content = split_by_substring(split_info_fourth[1], "===");
  std::vector<std::string> p_content = split_by_substring(split_info_fourth[2], "===");
  std::vector<std::string> o_content = split_by_substring(split_info_fourth[3], "===");
  std::vector<std::string> g_content;
  try {
    if (split_info_fourth.size() == 4) {
      // handle without graph //
      // Check if all entrries are constant
      if (s_content[1] == "constant" && p_content[1] == "constant" && o_content[1] == "constant") {
        handle_constant(s_content, p_content, o_content, g_content, output_file_name);
        generated_triple = 1;

      } else if (s_content[1] == "preformatted" && p_content[1] == "preformatted" && o_content[1] == "preformatted") {
        handle_constant_preformatted(s_content, p_content, o_content, g_content, output_file_name);
        generated_triple = 1;
      } else {
        generated_triple = execute_complex(output_file_name, left_path, right_path, left_name, right_name, left_join_attr, right_join_attr, base_uri,
                                           projected_attributes_left, projected_attributes_right, s_content, p_content, o_content);
      }
    } else {
      // Handle with graph
      g_content = split_by_substring(split_info_fourth[4], "===");

      if (s_content[1] == "constant" && p_content[1] == "constant" && o_content[1] == "constant" && g_content[1] == "constant") {
        handle_constant(s_content, p_content, o_content, g_content, output_file_name);
        generated_triple = 1;
      } else if (s_content[1] == "preformatted" && p_content[1] == "preformatted" && o_content[1] == "preformatted" && g_content[1] == "preformatted") {
        handle_constant_preformatted(s_content, p_content, o_content, g_content, output_file_name);
        generated_triple = 1;
      } else {
        // If not constant handle normal
        generated_triple = execute_complex_with_graph(output_file_name, left_path, right_path, left_name, right_name, left_join_attr, right_join_attr, base_uri,
                                                      projected_attributes_left, projected_attributes_right, s_content, p_content, o_content, g_content);
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

  return generated_triple;
}

std::unordered_set<std::string> dependent_complex_mapping(const std::string& information, std::unordered_set<std::string>& unique_triple) {
  // Extract relevant parts
  std::vector<std::string> split_info = split_by_substring(information, "\n");
  if (split_info.size() != 7) {
    std::cout << "Plan is too long. Got size: " << split_info.size() << std::endl;
    std::exit(1);
  }

  fs::path output_file_name = split_info[4];
  std::string base_uri = split_info[5];

  std::vector<std::string> split_info_first = split_by_substring(split_info[0], "|||");
  std::vector<std::string> split_info_second = split_by_substring(split_info[1], "|||");
  std::vector<std::string> split_info_third = split_by_substring(split_info[2], "|||");
  std::vector<std::string> split_info_fourth = split_by_substring(split_info[3], "|||");

  std::string left_path = split_info_first[1];
  std::vector<std::string> projected_attributes_left = split_by_substring(split_info_first[2], "===");
  std::string left_name = split_info_first[3];

  std::string right_path = split_info_second[1];
  std::vector<std::string> projected_attributes_right = split_by_substring(split_info_second[2], "===");
  std::string right_name = split_info_second[3];

  std::vector<std::string> join_mapping = split_by_substring(split_info_third[1], "===");
  std::string left_join_attr = join_mapping[0];
  std::string right_join_attr = join_mapping[1];

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  size_t generated_triple = 0;

  std::vector<std::string> s_content = split_by_substring(split_info_fourth[1], "===");
  std::vector<std::string> p_content = split_by_substring(split_info_fourth[2], "===");
  std::vector<std::string> o_content = split_by_substring(split_info_fourth[3], "===");
  std::vector<std::string> g_content;
  try {
    if (split_info_fourth.size() == 4) {
      // handle without graph //
      // Check if all entrries are constant
      if (s_content[1] == "constant" && p_content[1] == "constant" && o_content[1] == "constant") {
        handle_constant(s_content, p_content, o_content, g_content, output_file_name);
        generated_triple = 1;
      } else if (s_content[1] == "preformatted" && p_content[1] == "preformatted" && o_content[1] == "preformatted") {
        handle_constant_preformatted_dependent(s_content, p_content, o_content, g_content, output_file_name, unique_triple);
        generated_triple = 1;
      } else {
        unique_triple = execute_complex_dependent(output_file_name, left_path, right_path, left_name, right_name, left_join_attr, right_join_attr, base_uri,
                                                  projected_attributes_left, projected_attributes_right, s_content, p_content, o_content, unique_triple);
      }
    } else {
      // Handle with graph //
      g_content = split_by_substring(split_info_fourth[4], "===");
      if (s_content[1] == "constant" && p_content[1] == "constant" && o_content[1] == "constant" && g_content[1] == "constant") {
        handle_constant(s_content, p_content, o_content, g_content, output_file_name);
        generated_triple = 1;
      } else if (s_content[1] == "preformatted" && p_content[1] == "preformatted" && o_content[1] == "preformatted") {
        handle_constant_preformatted_dependent(s_content, p_content, o_content, g_content, output_file_name, unique_triple);
        generated_triple = 1;
      } else {
        unique_triple = execute_complex_with_graph_dependent(output_file_name, left_path, right_path, left_name, right_name, left_join_attr, right_join_attr, base_uri,
                                                             projected_attributes_left, projected_attributes_right, s_content, p_content, o_content, g_content, unique_triple);
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
