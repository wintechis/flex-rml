#include <cstring>
#include <fstream>
#include <iostream>

#include "definitions.h"
#include "rdf_parser.h"

// used to store string result
static std::string g_result_str;

std::vector<std::string> find_matching_objects(const std::vector<NTriple> &triples, const std::string &subject, const std::string &predicate) {
  std::vector<std::string> results;

  for (const NTriple &triple : triples) {
    // Check subject if not empty
    if (!subject.empty() && triple.subject != subject) {
      continue;  // skip this triple if subjects don't match
    }

    // Check predicate if not empty
    if (!predicate.empty() && triple.predicate != predicate) {
      continue;  // skip this triple if predicates don't match
    }

    // If we reach here, both subject and predicate matched
    results.push_back(triple.object);
  }

  return results;  // returns the vector of matching objects
}

std::vector<std::string> find_matching_subjects(
    const std::vector<NTriple> &triples, const std::string &predicate,
    const std::string &object) {
  std::vector<std::string> results;

  for (const auto &triple : triples) {
    // Check predicate
    if (triple.predicate != predicate) {
      continue;  // skip this triple if predicates don't match
    }

    // Check object if not empty
    if (!object.empty() && triple.object != object) {
      continue;  // skip this triple if objects don't match
    }

    // If we reach here, predicate matched (and object, if provided)
    results.push_back(triple.subject);
  }

  return results;  // returns the vector of matching subjects
}

void validate_rml(const std::vector<NTriple> &rml_triple) {
  ///////////////////////
  // Graph must be IRI //
  ///////////////////////

  // Get graph map node
  std::vector<std::string> results = find_matching_objects(rml_triple, "", "http://w3id.org/rml/graphMap");

  // check all result types
  for (const auto &result : results) {
    std::vector<std::string> term_types = find_matching_objects(rml_triple, result, "http://w3id.org/rml/termType");
    if (term_types.size() == 1) {
      if (term_types[0] != "http://w3id.org/rml/IRI") {
        std::cout << "Error: Only term type IRI supported for graph! Got: " << term_types[0] << std::endl;
        std::exit(1);
      }
    }
  }

  // Check if subject map is constant and a term map type is given
  results = find_matching_subjects(rml_triple, "http://w3id.org/rml/constant", "");

  for (const auto &result : results) {
    std::vector<std::string> term_types = find_matching_objects(rml_triple, result, "http://w3id.org/rml/termType");
    if (term_types.size() != 0) {
        std::cout << "Error: rml:constant and rml:termType in combination not valid!" << std::endl;
        std::exit(1);
    }
  }

}

extern "C" {

// Function to process the RDF string and return the result
const char *parse_rdf(const char *rdf_mapping_string) {
  try {
    std::string rdf_mapping(rdf_mapping_string);
    // Parse the input RDF rule
    RDFParser parser;
    std::vector<NTriple> rml_triple = parser.parse(rdf_mapping);
    
    // Validate Graph
    validate_rml(rml_triple);
    // Clear the global result string
    g_result_str.clear();

    // Build a single string from the parsed triples
    for (const auto &el : rml_triple) {
      g_result_str += el.subject + "|||" + el.predicate + "|||" + el.object + "\n";
    }

    // Return result as a C-string
    return g_result_str.c_str();
  } catch (const std::runtime_error &e) {
    // Return the error message as a string
    static std::string error_msg = "Error: " + std::string(e.what());
    return error_msg.c_str();
  } catch (...) {
    static std::string error_msg = "Error: Unknown error occurred.";
    return error_msg.c_str();
  }
}
}