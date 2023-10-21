#ifndef STRING_HELPER_H
#define STRING_HELPER_H

#include <algorithm>
#include <string>
#include <vector>

#include "custom_io.h"

// New ones
std::vector<std::string> split_csv_line(const std::string& str, char separator);

// Old ones
std::vector<std::string> extract_substrings(const std::string& str);
std::string replace_substring(const std::string& original, const std::string& toReplace, const std::string& replacement);
std::vector<std::string> enclose_in_braces(const std::vector<std::string>& inputStrings);

#endif