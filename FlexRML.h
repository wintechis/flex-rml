#ifndef FLEXRML_H
#define FLEXRML_H

#include <openssl/evp.h>

#include <cstdint>
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
#include <openssl/md5.h>

#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#endif

// Number used when expanding maps
constexpr uint initial_blank_node_nr = 18914;

namespace std {
template <>
struct hash<NQuad> {
  uint64_t operator()(const NQuad& t) const {
    std::string concatenated = t.subject + t.predicate + t.object + t.graph;

    unsigned char md5_result[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(concatenated.c_str()), concatenated.size(), md5_result);

    // Since MD5 produces a 128-bit hash and we need a 64-bit result,
    // we take the first 8 bytes of the MD5 hash to produce our 64-bit hash.
    uint64_t hash_result;
    memcpy(&hash_result, md5_result, sizeof(hash_result));
    return hash_result;
  }
};
}  // namespace std

// Functions
std::unordered_set<NQuad> map_data(std::string& rml_rule, const std::string& input_data = "");
#ifndef ARDUINO
void map_data_to_file_threading(std::string& rml_rule, std::ofstream& outFile, bool remove_duplicates, uint8_t num_threads);
void map_data_to_file(std::string& rml_rule, std::ofstream& outFile, bool remove_duplicates);
#endif

#endif