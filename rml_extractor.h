#ifndef EXTRACT_RML_RULES_H
#define EXTRACT_RML_RULES_H

#include <string>
#include <unordered_set>
#include <vector>

#include "custom_io.h"
#include "definitions.h"
#include "rdf_vector_helper.h"

void parse_rml_rules(
    const std::vector<NTriple>& rml_triple,
    const std::vector<std::string>& tripleMap_nodes,
    const std::string& base_uri,
    std::vector<LogicalSourceInfo>& logicalSourceInfo_of_tripleMaps,
    std::vector<SubjectMapInfo>& subjectMapInfo_of_tripleMaps,
    std::vector<std::vector<PredicateMapInfo>>& predicateMapInfo_of_tripleMaps,
    std::vector<std::vector<ObjectMapInfo>>& objectMapInfo_of_tripleMaps,
    std::vector<std::vector<PredicateObjectMapInfo>>& predicateObjectMapInfo_of_tripleMaps);

#endif