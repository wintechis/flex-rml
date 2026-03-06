#ifndef RML_PARSER_H
#define RML_PARSER_H

#include <string>
#include <vector>

#include "definitions.h"
#include "serd/serd.h"

class RDFParser {
 private:
  std::vector<NTriple> rml_triples;
  SerdEnv* env;

  std::string extract_base_URI(const std::string& str);
  SerdStatus handle_error(void* handle, const SerdError* error);
  SerdStatus capture_prefix(const SerdNode* name, const SerdNode* uri);
  static SerdStatus static_handle_error(void* handle, const SerdError* error);
  static SerdStatus static_capture_prefix(void* handle, const SerdNode* name, const SerdNode* uri);
  SerdStatus handle_triple(void* handle, unsigned int flags, const SerdNode* graph, const SerdNode* subject, const SerdNode* predicate, const SerdNode* object, const SerdNode* datatype, const SerdNode* lang);
  static SerdStatus static_handle_triple(void* handle, unsigned int flags, const SerdNode* graph, const SerdNode* subject, const SerdNode* predicate, const SerdNode* object, const SerdNode* datatype, const SerdNode* lang);
  SerdNode expand_node(const SerdNode* node);
  void handle_rdf_parsing(const std::string& rdf_data);

 public:
  RDFParser();
  ~RDFParser();

  std::vector<NTriple> parse(const std::string& rml_rule);
};

#endif