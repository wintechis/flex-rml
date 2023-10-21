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
std::vector<std::string> extract_substrings(const std::string& str) {
  std::vector<std::string> substrings;
  size_t startPos = 0;

  while ((startPos = str.find('{', startPos)) != std::string::npos) {
    // Check if the '{' character is not escaped
    if (startPos == 0 || str[startPos - 1] != '\\') {
      size_t endPos = str.find('}', startPos);
      if (endPos != std::string::npos) {
        std::string substring = str.substr(startPos + 1, endPos - startPos - 1);
        substrings.push_back(substring);
        startPos = endPos + 1;
      } else {
        break;  // no matching closing brace found
      }
    } else {
      // If the '{' character is escaped, continue searching from the next character
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
std::string replace_substring(const std::string& original,
                              const std::string& toReplace,
                              const std::string& replacement) {
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
std::vector<std::string> enclose_in_braces(const std::vector<std::string>& inputStrings) {
  std::vector<std::string> enclosedStrings;
  // Reserve memory to avoid reallocations
  enclosedStrings.reserve(inputStrings.size());
  for (const auto& str : inputStrings) {
    enclosedStrings.push_back("{" + str + "}");
  }
  return enclosedStrings;
}

std::string cleanToken(std::string& token) {
  std::string::iterator it = std::remove_if(token.begin(), token.end(), [](char c) {
    return iscntrl(static_cast<unsigned char>(c));
  });
  if (it != token.end()) {
    token.erase(it, token.end());
  }
  return token;
}

std::vector<std::string> split_csv_line(const std::string& str, char separator) {
  // heuristic reserve
  std::vector<std::string> result;
  result.reserve(str.size() / 10);  // you can adjust the divisor as per your average token size

  std::string token;
  token.reserve(str.size() / 10);
  bool insideQuotes = false;

  for (char c : str) {
    if (c == '"') {
      if (insideQuotes && (token.back() == '"')) {
        token.pop_back();
      } else {
        insideQuotes = !insideQuotes;
      }
    } else if (c == separator && !insideQuotes) {
      if (std::any_of(token.begin(), token.end(), [](char c) {
            return iscntrl(static_cast<unsigned char>(c));
          })) {
        cleanToken(token);
      }
      result.push_back(std::move(token));
      token.clear();
      token.reserve(str.size() / 10);
    } else {
      token += c;
    }
  }

  if (insideQuotes) {
    throw_error("Malformed CSV: unmatched quote");
  }

  if (std::any_of(token.begin(), token.end(), [](char c) {
        return iscntrl(static_cast<unsigned char>(c));
      })) {
    cleanToken(token);
  }
  result.push_back(std::move(token));

  return result;
}