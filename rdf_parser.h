#ifndef RML_PARSER_H
#define RML_PARSER_H

#include <sstream>
#include <string>
#include <vector>

#include "definitions.h"
#include "rdf_parser/serd/serd.h"

// Serd needs different data type for init
#ifdef ARDUINO
#ifdef ESP32
// Data type specific for ESP32 when using Arduino IDE
using flag_data_type = unsigned int;
#else
// Data type specific for Arduino (other than ESP32)
using flag_data_type = long unsigned int;
#endif
#elif defined(ESP_PLATFORM)
// Data type for ESP32 when using ESP-IDF or other than Arduino IDE
using flag_data_type = unsigned int;
#else
// Data type for PC or other platforms
using flag_data_type = unsigned int;
#endif

class RDFParser {
 private:
  std::vector<NTriple> rml_triples;
  SerdEnv* env;

  std::string extract_base_URI(const std::string& str);
  SerdStatus handle_error(void* handle, const SerdError* error);
  SerdStatus capture_prefix(const SerdNode* name, const SerdNode* uri);
  static SerdStatus static_handle_error(void* handle, const SerdError* error);
  static SerdStatus static_capture_prefix(void* handle, const SerdNode* name, const SerdNode* uri);
  SerdStatus handle_triple(void* handle, flag_data_type flags, const SerdNode* graph, const SerdNode* subject, const SerdNode* predicate, const SerdNode* object, const SerdNode* datatype, const SerdNode* lang);
  static SerdStatus static_handle_triple(void* handle, flag_data_type flags, const SerdNode* graph, const SerdNode* subject, const SerdNode* predicate, const SerdNode* object, const SerdNode* datatype, const SerdNode* lang);
  SerdNode expand_node(const SerdNode* node);
  void handle_rdf_parsing(const std::string& rdf_data);

 public:
  RDFParser();
  ~RDFParser();

  std::vector<NTriple> parse(const std::string& rml_rule);

  std::string extracted_base_uri;
};

#endif