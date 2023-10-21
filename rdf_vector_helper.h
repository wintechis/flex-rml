#ifndef RDF_HELPER_H
#define RDF_HELPER_H

#include <algorithm>
#include <array>
#include <vector>

#include "custom_io.h"
#include "definitions.h"
  
#include "rdf_parser.h"
#include "rml_uris.h"
#include "string_helper.h"

void read_and_prepare_rml_triple(const std::string& rml_rule, std::vector<NTriple>& rml_triple, std::string& base_uri);
void remove_triple(std::vector<NTriple>& triples, const std::string& subject, const std::string& predicate, const std::string& object);
std::vector<std::string> find_matching_object(const std::vector<NTriple>& triples, const std::string& subject, const std::string& predicate);
std::vector<std::string> find_matching_subject(const std::vector<NTriple>& triples, const std::string& predicate, const std::string& object = "");

#endif