#ifndef FLEXRML_H
#define FLEXRML_H

#include <cstdint>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "custom_io.h"
#include "definitions.h"
#include "file_reader/csv_reader.h"
#include "file_reader/file_reader.h"
#include "rdf_vector_helper.h"
#include "rml_extractor.h"
#include "rml_uris.h"
#include "string_helper.h"
#include "termtype_helper.h"

// For writing to files only available on PC
#ifndef ARDUINO
#include <condition_variable>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <random>
#include <thread>

#include "city.h"
#endif

// Number used when expanding maps
constexpr uint initial_blank_node_nr = 18914;

inline void hash_combine_64(uint64_t &seed, uint64_t value) {
  seed ^= value + 0x9ddfea08eb382d69ULL + (seed << 12) + (seed >> 4);
}

namespace std {
template <>
struct hash<NQuad> {
  std::size_t operator()(const NQuad &t) const {
    uint64_t h1 = CityHash64(t.subject.c_str(), t.subject.size());
    uint64_t h2 = CityHash64(t.predicate.c_str(), t.predicate.size());
    uint64_t h3 = CityHash64(t.object.c_str(), t.object.size());
    uint64_t h4 = CityHash64(t.graph.c_str(), t.graph.size());

    // Combine hashes using the hash_combine method
    uint64_t result = h1;
    hash_combine_64(result, h2);
    hash_combine_64(result, h3);
    hash_combine_64(result, h4);

    return result;
  }
};
}  // namespace std

// Functions
std::unordered_set<NQuad> map_data(std::string &rml_rule, std::map<std::string, std::string> &input_data);
#ifndef ARDUINO
void map_data_to_file_threading(std::string &rml_rule, std::ofstream &outFile, Flags &flags);
void map_data_to_file(std::string &rml_rule, std::ofstream &outFile, Flags &flags);
std::string generate_object_with_hash_join(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data,
    const std::vector<std::string> &split_header,
    CsvReader &reader,
    const std::unordered_map<std::string, std::streampos> &parent_file_index);
std::vector<std::string> generate_object_with_hash_join_full(
    const ObjectMapInfo &objectMapInfo,
    const std::vector<std::string> &split_data_child,
    const std::vector<std::string> &split_header_child,
    const std::vector<std::string> &split_header_parent,
    const std::unordered_map<std::string, std::vector<std::vector<std::string>>> &parent_file_index_full);
#endif

#endif