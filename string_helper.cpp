#include "string_helper.h"

////////////////////////////////
/// String Helper Functions ///
///////////////////////////////

/**
 * @brief Extracts substrings enclosed in non-escaped curly braces {} from the input string.
 *
 * This function searches for substrings within the input string that are enclosed
 * in curly braces {}. The braces themselves are not included in the extracted substrings.
 * If a curly brace is preceded by a backslash, it is considered escaped, and it won't be
 * considered as the start of a substring.
 *
 * @param str The input string from which substrings are to be extracted.
 * @return std::vector<std::string> A vector containing the extracted substrings, not including the curly braces.
 *
 * @example Input: "http://example.com/Student/{ID}/\\{escaped}/{Name}"
 *          Output: {"ID", "Name"}
 */
std::vector<std::string> extract_substrings(const std::string &str) {
  std::vector<std::string> substrings;
  // Optional: substrings.reserve(estimated_number_of_substrings);

  size_t startPos = 0, endPos = 0;

  while ((startPos = str.find('{', startPos)) != std::string::npos) {
    if (startPos == 0 || str[startPos - 1] != '\\') {
      endPos = str.find('}', startPos);
      if (endPos != std::string::npos) {
        substrings.emplace_back(str, startPos + 1, endPos - startPos - 1);
        startPos = endPos + 1;
      } else {
        break;  // no matching closing brace found
      }
    } else {
      startPos++;
    }
  }
  return substrings;
}

/**
 * @brief Extracts substrings enclosed in curly braces {} from the input string.
 *
 * @param original The orignal string from which substrings should be replaced.
 * @param toReplace The contained substring to replace.
 * @param replacement The replacement substring.
 *
 * @return std::string A string containing the modified original string with reolacement.
 *
 */
std::string replace_substring(const std::string &original,
                              const std::string &toReplace,
                              const std::string &replacement) {
  std::string result = original;
  std::size_t pos = result.find(toReplace);
  if (pos != std::string::npos) {
    result.replace(pos, toReplace.length(), replacement);
  }
  return result;
}

/**
 * @brief Encloses strings in curly braces {}.
 *
 * @param inputStrings - The input vector containing string which should be enclosed.
 * @return std::vector<std::string> - A vector containing the enclosed strings.
 *
 * @example Input: ["ID", "Name"]
 *          Output: ["{ID}", "{Name}"]
 */
std::vector<std::string> enclose_in_braces(const std::vector<std::string> &inputStrings) {
  std::vector<std::string> enclosedStrings;
  enclosedStrings.reserve(inputStrings.size());

  for (const auto &str : inputStrings) {
    enclosedStrings.emplace_back("{" + str + "}");
  }

  return enclosedStrings;
}
std::vector<std::string> split_csv_line(const std::string &str, char separator) {
  std::vector<std::string> result;
  result.reserve(str.size());

  std::string token;
  token.reserve(str.size());
  bool insideQuotes = false;
  bool hasControlChar = false;

  for (char c : str) {
    if (c == '"') {
      if (insideQuotes && (token.back() == '"')) {
        token.pop_back();
      } else {
        insideQuotes = !insideQuotes;
      }
    } else if (c == separator && !insideQuotes) {
      if (hasControlChar) {
        // This logic replaces cleanToken
        token.erase(std::remove_if(token.begin(), token.end(), [](char c) {
                      return iscntrl(static_cast<unsigned char>(c));
                    }),
                    token.end());
        hasControlChar = false;
      }
      result.push_back(std::move(token));
      token.clear();
    } else {
      if (iscntrl(static_cast<unsigned char>(c))) {
        hasControlChar = true;
      }
      token.push_back(c);
    }
  }

  if (insideQuotes) {
    throw_error("Malformed CSV: unmatched quote");
  }

  if (hasControlChar) {
    // This logic replaces cleanToken
    token.erase(std::remove_if(token.begin(), token.end(), [](char c) {
                  return iscntrl(static_cast<unsigned char>(c));
                }),
                token.end());
  }
  result.push_back(std::move(token));

  return result;
}
