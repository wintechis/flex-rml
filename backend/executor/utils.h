#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "definitions.h"
#include "xxhash.h"

namespace fs = std::filesystem;

std::vector<std::string> split_by_substring(const std::string& str, const std::string& delimiter);

std::string replace_substring(const std::string& original, const std::string& toReplace, const std::string& replacement);

uint64_t combinedHash(std::vector<std::string>& fields);

int get_index(const std::vector<std::string>& input_vector, std::string searched_element);

std::vector<std::string> split_csv_line(const std::string& str, char separator);

void handle_constant(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                     const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                     const fs::path& output_file_name);

void handle_constant_preformatted(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                  const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                  const fs::path& output_file_name);

std::unordered_set<std::string> handle_constant_dependent(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                                          const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                                          const fs::path& output_file_name, std::unordered_set<std::string>& unique_triple);

std::unordered_set<std::string> handle_constant_preformatted_dependent(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                                                       const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                                                       const fs::path& output_file_name, std::unordered_set<std::string>& unique_triple);

std::string create_operator(const std::string& term_map,
                            const std::string& term_map_type,
                            const std::string& term_type,
                            const std::string& lang_tag,
                            const std::string& data_type,
                            const std::string& base_uri,
                            std::unordered_map<std::string, std::string>& map);