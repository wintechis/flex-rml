#ifndef FLEXRML_H
#define FLEXRML_H

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
#include <fstream>
#endif

// Number used when expanding maps
constexpr uint initial_blank_node_nr = 18914;

// MurmurHash3 32-bit finalization mix - forces all bits of a hash block to avalanche
inline uint32_t fmix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

inline uint32_t murmur3_32(const char* key, uint32_t len, uint32_t seed = 0) {
  const uint8_t* data = (const uint8_t*)key;
  const int nblocks = len / 4;
  uint32_t h1 = seed;
  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  // Body
  const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);
  for (int i = -nblocks; i; i++) {
    uint32_t k1 = blocks[i];

    k1 *= c1;
    k1 = (k1 << 15) | (k1 >> (32 - 15));
    k1 *= c2;

    h1 ^= k1;
    h1 = (h1 << 13) | (h1 >> (32 - 13));
    h1 = h1 * 5 + 0xe6546b64;
  }

  // Tail
  const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);
  uint32_t k1 = 0;
  switch (len & 3) {
    case 3:
      k1 ^= tail[2] << 16;
      // fallthrough
    case 2:
      k1 ^= tail[1] << 8;
      // fallthrough
    case 1:
      k1 ^= tail[0];
      k1 *= c1;
      k1 = (k1 << 15) | (k1 >> (32 - 15));
      k1 *= c2;
      h1 ^= k1;
  };

  // Finalization
  h1 ^= len;
  return fmix32(h1);
}

// Based on boost::hash_combine
inline void hash_combine(std::size_t& seed, std::size_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std {
template <>
struct hash<NQuad> {
  std::size_t operator()(const NQuad& t) const {
    std::size_t h1 = murmur3_32(t.subject.c_str(), t.subject.size());
    std::size_t h2 = murmur3_32(t.predicate.c_str(), t.predicate.size());
    std::size_t h3 = murmur3_32(t.object.c_str(), t.object.size());
    std::size_t h4 = murmur3_32(t.graph.c_str(), t.graph.size());

    // Combine hashes using the hash_combine method
    std::size_t result = h1;
    hash_combine(result, h2);
    hash_combine(result, h3);
    hash_combine(result, h4);

    return result;
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