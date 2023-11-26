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

#ifndef ARDUINO
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
#else
inline uint64_t fmix64(uint64_t k) {
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return k;
}

inline uint64_t murmur3_64(const char* key, size_t len, uint64_t seed = 0) {
  const uint8_t* data = (const uint8_t*)key;
  const int nblocks = len / 8;
  uint64_t h1 = seed;

  const uint64_t c1 = 0x87c37b91114253d5ULL;
  const uint64_t c2 = 0x4cf5ad432745937fULL;

  // Body
  const uint64_t* blocks = (const uint64_t*)(data);
  for (int i = 0; i < nblocks; i++) {
    uint64_t k1 = blocks[i];

    k1 *= c1;
    k1 = (k1 << 31) | (k1 >> (64 - 31));
    k1 *= c2;

    h1 ^= k1;
    h1 = (h1 << 27) | (h1 >> (64 - 27));
    h1 = h1 * 5 + 0x52dce729;
  }

  // Tail
  const uint8_t* tail = (const uint8_t*)(data + nblocks * 8);
  uint64_t k1 = 0;
  switch (len & 7) {
    case 7:
      k1 ^= ((uint64_t)tail[6]) << 48;
      // fallthrough
    case 6:
      k1 ^= ((uint64_t)tail[5]) << 40;
      // fallthrough
    case 5:
      k1 ^= ((uint64_t)tail[4]) << 32;
      // fallthrough
    case 4:
      k1 ^= ((uint64_t)tail[3]) << 24;
      // fallthrough
    case 3:
      k1 ^= ((uint64_t)tail[2]) << 16;
      // fallthrough
    case 2:
      k1 ^= ((uint64_t)tail[1]) << 8;
      // fallthrough
    case 1:
      k1 ^= ((uint64_t)tail[0]);
      k1 *= c1;
      k1 = (k1 << 31) | (k1 >> (64 - 31));
      k1 *= c2;
      h1 ^= k1;
  };

  // Finalization
  h1 ^= len;
  h1 = fmix64(h1);
  return h1;
}

// Based on boost::hash_combine
inline void hash_combine(uint64_t& seed, uint64_t value) {
  seed ^= value + 0x9ddfea08eb382d69ULL + (seed << 12) + (seed >> 4);
}

namespace std {
template <>
struct hash<NQuad> {
  std::size_t operator()(const NQuad& t) const {
    uint64_t h1 = murmur3_64(t.subject.c_str(), t.subject.size());
    uint64_t h2 = murmur3_64(t.predicate.c_str(), t.predicate.size());
    uint64_t h3 = murmur3_64(t.object.c_str(), t.object.size());
    uint64_t h4 = murmur3_64(t.graph.c_str(), t.graph.size());

    // Combine hashes using the hash_combine method
    uint64_t result = h1;
    hash_combine(result, h2);
    hash_combine(result, h3);
    hash_combine(result, h4);

    return result;
  }
};
}  // namespace std
#endif

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