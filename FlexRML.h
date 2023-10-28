#ifndef FLEXRML_H
#define FLEXRML_H

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
#include <fstream>
#endif

// Number used when expanding maps
constexpr uint8_t initial_blank_node_nr = 91;

// djb2 hash algorithm
inline std::size_t djb2_hash(const std::string& str) {
  std::size_t hash = 5381;  // Initial prime value

  for (char c : str) {
    hash = ((hash << 5) + hash) + c;  // hash * 33 + c
  }

  return hash;
}

// Custom hash function for NQuad struct
namespace std {
template <>
struct hash<NQuad> {
  std::size_t operator()(const NQuad& t) const {
    std::size_t h1 = djb2_hash(t.subject);
    std::size_t h2 = djb2_hash(t.predicate);
    std::size_t h3 = djb2_hash(t.object);
    std::size_t h4 = djb2_hash(t.graph);

    // Combine hashes
    return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
  }
};
}  // namespace std

// Functions
std::unordered_set<NQuad> map_data(std::string& rml_rule, const std::string& input_data = "");
#ifndef ARDUINO
void map_data_to_file(std::string& rml_rule, std::ofstream& outFile, bool remove_duplicates);
#endif

#endif