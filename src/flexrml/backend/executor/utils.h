#pragma once

#include <filesystem>
#include <string>
#include <string_view>
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
void split_csv_line_into(const std::string& str, char separator, std::vector<std::string>& result);
bool split_csv_line_views_into(const std::string& str, char separator, std::vector<std::string_view>& result);
bool is_default_graph_marker(const std::string& graph);
std::string format_statement(const std::string& subject, const std::string& predicate, const std::string& object);
std::string format_statement(const std::string& subject, const std::string& predicate, const std::string& object, const std::string& graph);
void format_statement_into(const std::string& subject, const std::string& predicate, const std::string& object, std::string& out);
void format_statement_into(const std::string& subject, const std::string& predicate, const std::string& object, const std::string& graph, std::string& out);
std::string make_safe_iri(std::string_view node, bool encode_non_ascii = true);
void append_safe_iri(std::string_view node, bool encode_non_ascii, std::string& out);
bool has_invalid_iri_char(std::string_view value);
std::string_view infer_literal_datatype(std::string_view rdf_term,
                                        std::string_view lang_tag,
                                        std::string_view data_type);
void handle_term_type_into(const std::string& term_type,
                           std::string_view rdf_term,
                           const std::string& lang_tag,
                           std::string_view data_type,
                           std::string& out);

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
void create_operator_into(const std::string& term_map,
                          const std::string& term_map_type,
                          const std::string& term_type,
                          const std::string& lang_tag,
                          const std::string& data_type,
                          const std::string& base_uri,
                          std::unordered_map<std::string, std::string>& map,
                          std::string& out);
void create_operator_into(const std::string& term_map,
                          const std::string& term_map_type,
                          const std::string& term_type,
                          const std::string& lang_tag,
                          const std::string& data_type,
                          const std::string& base_uri,
                          std::unordered_map<std::string, std::string>& map,
                          std::string& out,
                          std::string& scratch);

std::string get_local_now_iso8601();
std::string get_current_date_time_string();
std::string generate_random_string(std::size_t length);
std::string handle_function_call(std::string function_signature,
                                 int line_count,
                                 std::string realation_name,
                                 std::unordered_map<std::string, std::string>& row);
