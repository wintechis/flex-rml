#ifndef RML_URIS_H
#define RML_URIS_H

#include <string>

const std::string IRI_TERM_TYPE = "http://www.w3.org/ns/r2rml#IRI";
const std::string LITERAL_TERM_TYPE = "http://www.w3.org/ns/r2rml#Literal";
const std::string TRIPLES_MAP = "http://www.w3.org/ns/r2rml#TriplesMap";
const std::string CSV_REFERENCE_FORMULATION = "http://semweb.mmlab.be/ns/ql#CSV";
const std::string RDF_TYPE = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
const std::string RML_CONSTANT = "http://www.w3.org/ns/r2rml#constant";
const std::string RML_SUBJECT = "http://www.w3.org/ns/r2rml#subject";
const std::string RML_PREDICATE = "http://www.w3.org/ns/r2rml#predicate";
const std::string RML_OBJECT = "http://www.w3.org/ns/r2rml#object";
const std::string RML_GRAPH = "http://www.w3.org/ns/r2rml#graph";
const std::string RML_SUBJECT_MAP = "http://www.w3.org/ns/r2rml#subjectMap";
const std::string RML_PREDICATE_MAP = "http://www.w3.org/ns/r2rml#predicateMap";
const std::string RML_OBJECT_MAP = "http://www.w3.org/ns/r2rml#objectMap";
const std::string RML_GRAPH_MAP = "http://www.w3.org/ns/r2rml#graphMap";
const std::string RML_TEMPLATE = "http://www.w3.org/ns/r2rml#template";
const std::string RML_REFERENCE = "http://semweb.mmlab.be/ns/rml#reference";
const std::string RML_PARENT = "http://www.w3.org/ns/r2rml#parent";
const std::string RML_CHILD = "http://www.w3.org/ns/r2rml#child";
const std::string RML_LANGUAGE = "http://www.w3.org/ns/r2rml#language";
const std::string RML_LANGUAGE_MAP = "http://www.w3.org/ns/r2rml#languageMap";
const std::string RML_DATA_TYPE = "http://www.w3.org/ns/r2rml#datatype";
const std::string RML_DATA_TYPE_MAP = "http://www.w3.org/ns/r2rml#datatypeMap";
const std::string RML_TERM_TYPE = "http://www.w3.org/ns/r2rml#termType";
const std::string RML_LOGICAL_SOURCE = "http://semweb.mmlab.be/ns/rml#logicalSource";
const std::string RML_PREDICATE_OBJECT_MAP = "http://www.w3.org/ns/r2rml#predicateObjectMap";
const std::string RML_PARENT_TRIPLES_MAP = "http://www.w3.org/ns/r2rml#parentTriplesMap";
const std::string RML_JOIN_CONDITION = "http://www.w3.org/ns/r2rml#joinCondition";
const std::string SD_NAME = "https://w3id.org/okn/o/sd#name";
const std::string RML_CLASS = "http://www.w3.org/ns/r2rml#class";
const std::string RML_SOURCE = "http://semweb.mmlab.be/ns/rml#source";
const std::string RML_REFERENCE_FORMULATION = "http://semweb.mmlab.be/ns/rml#referenceFormulation";
const std::string DEFAULT_GRAPH = "http://www.w3.org/ns/r2rml#defaultGraph";

// Custom definitions
const std::string NO_GRAPH = "ng";
const std::string PARENT_SOURCE = "http://www.example.com#parentSource";
const std::string PARENT_REFERENCE_FORMULATION = "http://www.example.com#parentRef";
const std::string JOIN_REFERENCE_CONDITION = "http://www.example.com#joinReferenceCondition";
#endif