#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#include <string>
#include <unordered_set>
#include <vector>

// Struct to hold the subjectMap information
struct SubjectMapInfo {
  std::string name_triplesMap_node;  // Name of parent triplesMap
  std::string constant;              // constant value of subjectMap
  std::string reference;             // reference value of subjectMap
  std::string template_str;          // template of subjectMap
  std::string termType;              // termType of subjectMap
  std::string graph_template;        // Used to store subjectMap graph template
  std::string graph_constant;        // Used to store subjectMap graph constant value
  std::string graph_termType;        // Used to store subjectMap graph termType
  std::vector<std::string> classes;  // used to store class the subject map is in
  std::string base_uri;              // Store the base uri
};

// Struct to hold the predicateMap information
struct PredicateMapInfo {
  std::string constant;      // constant value of predicateMap
  std::string template_str;  // template of predicateMap
  std::string reference;     // reference of predicateMap
};

// Struct to hold the objectMap information
struct ObjectMapInfo {
  std::string constant;                            // constant value of objectMap
  std::string template_str;                        // template of objectMap
  std::string termType;                            // termType of objectMap
  std::string reference;                           // reference of objectMap
  std::string language;                            // language tag
  std::string parentSource;                        // source of parent TriplesMap
  std::string parent_in_memory_name;               // in memory name of parten TriplesMap
  std::string parentRef;                           // Reference formualtion of parent source
  std::string parent;                              // Store parent key
  std::string child;                               // Store child key
  std::string dataType;                            // Stores specified dataType
  std::string join_reference_condition_available;  // Stores if join can be performed using reference condition
};

// Struct to hold the objectMap information
struct PredicateObjectMapInfo {
  std::string graph_template;  // Used to store subjectMap graph template
  std::string graph_constant;  // Used to store subjectMap graph constant value
  std::string graph_termType;  // Used to store subjectMap graph termType
};

// Struct to hold the logicalSourceMap information
struct LogicalSourceInfo {
  std::string reference_formulation;
  std::string source_path;
  std::string logical_iterator;
  std::string in_memory_name;
};

// Strcut to hold a generated Triple
struct NTriple {
  std::string subject;
  std::string predicate;
  std::string object;
};

// Strcut to hold a generated Quad
struct NQuad {
  std::string subject;
  std::string predicate;
  std::string object;
  std::string graph;

  // Comparision is needed for unorderedSet
  bool operator==(const NQuad& other) const {
    return subject == other.subject && predicate == other.predicate &&
           object == other.object && graph == other.graph;
  }
};

struct Flags {
  std::string mapping_file = "";                     // default is an empty string
  std::string output_file = "output.nq";             // defualt output path
  bool check_duplicates = false;                     // check for duplicates?
  bool threading = false;                            // use threading?
  uint8_t thread_count = 0;                          // number of threads to use
  bool adaptive_hash_selection = false;              // use adaptive hash selection? or always 128 bit
  float sampling_probability = 0.2;                  // defines the sampling probability used when estimating result size
  uint8_t fixed_bit_size = 0;                        // Specife the bit size of the hash function manually, 0 means not fixed
  std::unordered_set<std::string> tokens_to_remove;  // Hash set of tokens when encountered triple should be skipped
  bool in_memory_mapping = false;                    // Map data completely in memory
};

// Starting Number when generating blank nodes
extern int blank_node_counter;

const int THRESHOLD_32BIT = 2073;
const int THRESHOLD_64BIT = 135835773;

#endif