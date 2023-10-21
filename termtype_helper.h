#ifndef TERMTYPE_HELPER_H
#define TERMTYPE_HELPER_H

#include <string>
#include <unordered_set>

#include "custom_io.h"
  
#include "rml_uris.h"

std::string handle_term_type(const std::string& term_type, const std::string& node);
std::string url_encode(const std::string& node);
#endif