#include "json_handling.h"

bool initialize = true;

std::string get_next_word(std::ifstream& file, bool reset = false) {
  // Static variables to maintain state between calls
  static std::string current_line = "";
  static size_t current_pos = 0;

  // Reset static variables if needed:
  if (reset == true) {
    current_line = "";
    current_pos = 0;
    return "";
  }

  std::string current_word = "";
  bool inside_quotes = false;

  while (true) {
    // If we've finished with the current line (or it's our first call), read a
    // new one
    if (current_pos >= current_line.size()) {
      if (!std::getline(file, current_line)) {
        return "";
      }
      current_pos = 0;  // Reset position for the new line
    }

    // Iterate over the line char by char from the current position
    for (; current_pos < current_line.size(); ++current_pos) {
      char current_symbol = current_line[current_pos];

      // Handle double quotes
      if (current_symbol == '"') {
        inside_quotes = !inside_quotes;  // Toggle the inside_quotes flag
      }

      // If not inside quotes, handle word extraction normally
      if (!inside_quotes) {
        if (current_symbol == ' ' && current_word.empty()) {
          continue;  // Skip leading spaces
        }

        // If the current symbol is a special character or space
        if (current_symbol == ' ' || current_symbol == '[' ||
            current_symbol == ']' || current_symbol == '{' ||
            current_symbol == '}' || current_symbol == ',' ||
            current_symbol == ':') {
          if (current_word.empty()) {
            current_pos++;  // Move past the special character or space for the
                            // next word
            return std::string(
                1, current_symbol);  // Return the special character as a word
          } else {
            return current_word;  // Return the accumulated word
          }
        }
      }

      current_word += current_symbol;

      // If we're ending a quoted word, break out and return it
      if (current_symbol == '"' && !inside_quotes) {
        current_pos++;  // Move past the closing quote for the next word
        return current_word;
      }
    }
  }
}

void move_using_json_path(std::ifstream& file,
                          std::vector<std::string>& json_path_tokens,
                          std::stack<char>& array_stack) {
  int cnt = 0;
  int max = json_path_tokens.size();

  if (json_path_tokens[cnt] != "$") {
    throw_error("JsonPath is not supported!");
  }
  cnt++;
  std::string current_token;
  // Get new token if available
  if (cnt < max) {
    current_token = json_path_tokens[cnt];
  } else {
    // Add opening bracket to stack
    array_stack.push('[');
    return;
  }

  // Init to random value; just dont be empty
  std::string res = ";";
  // Iterate over all words
  while (true) {
    res = get_next_word(file);
    // If empty file is empty
    if (res == "") {
      throw_error("JsonPath not found!");
      break;
    }

    // If it matches the whole string
    if (res == current_token) {
      logln_debug("Found 1");
      cnt++;
      if (cnt < max) {
        current_token = json_path_tokens[cnt];

      } else {
        return;
      }
    }

    // If it matches the [
    if (res[0] == '[' && current_token[0] == '[') {
      logln_debug("Found 2");
      array_stack.push('[');
      cnt++;
      if (cnt < max) {
        current_token = json_path_tokens[cnt];

      } else {
        return;
      }
    }
  }
}

std::string get_next_element(std::ifstream& file,
                             std::vector<std::string>& json_path_tokens,
                             bool& initialize) {
  static std::stack<char> array_stack;
  static std::stack<char> element_stack;

  // Move to correct position
  if (initialize) {
    move_using_json_path(file, json_path_tokens, array_stack);
  }

  // Generate next element
  // Init to random value; just dont be empty
  std::string res = ";";
  std::string current_element = "";
  while (true) {
    res = get_next_word(file);
    // If read file is empty
    if (res == "") {
      break;
    }

    // If \r is found -> ignore it
    if (res == "\r") {
      continue;
    }

    // change stack dependent on value
    if (res[0] == '[') {
      array_stack.push('[');
    }
    if (res[0] == ']') {
      array_stack.pop();
    }
    // If stack is empty -> not more data can be found
    if (array_stack.empty()) {
      logln_debug("stack empty.");
      return "";
    }

    // change stack dependent on value
    if (res[0] == '{') {
      element_stack.push('{');
    }
    if (res[0] == '}') {
      element_stack.pop();
    }

    if (element_stack.empty()) {
      current_element += res;
      if (current_element != ",") {
        return current_element;
      } else {
        current_element = "";
        continue;
      }
    }

    current_element = current_element + " " + res;
  }

  return "";
}

/**
 * @brief Recursive helper function to extract headers and values from a JSON
 * object.
 *
 * This function traverses the JSON structure and creates header strings
 * by concatenating keys with dots. The final header and corresponding values
 * are then stored in the provided vectors.
 *
 * @param j          The current JSON object or value being processed.
 * @param headers    The vector to store extracted header strings.
 * @param values     The vector to store corresponding values for the headers.
 * @param currentKey The current key path being constructed (used for
 * recursion).
 */
void json_to_csv_helper(JsonVariant variant, std::vector<std::string>& headers,
                        std::vector<std::string>& values,
                        const std::string& currentKey = "") {
  if (variant.is<JsonObject>()) {
    for (JsonPair keyValue : variant.as<JsonObject>()) {
      std::string newKey = currentKey.empty()
                               ? keyValue.key().c_str()
                               : currentKey + "." + keyValue.key().c_str();

      if (keyValue.value().is<JsonObject>()) {
        json_to_csv_helper(keyValue.value(), headers, values, newKey);
      } else {
        headers.push_back(newKey);

        if (keyValue.value().is<const char*>()) {
          values.push_back(
              "\"" + std::string(keyValue.value().as<const char*>()) + "\"");
        } else if (keyValue.value().is<int>()) {
          values.push_back(std::to_string(keyValue.value().as<int>()));
        } else if (keyValue.value().is<float>()) {
          values.push_back(std::to_string(keyValue.value().as<float>()));
        } else if (keyValue.value().is<bool>()) {
          values.push_back(keyValue.value().as<bool>() ? "true" : "false");
        } else if (keyValue.value().isNull()) {
          values.push_back("");
        } else {
          throw_error("Error: Unsupported json data type.");
        }
      }
    }
  }
}

/**
 * @brief Converts a JSON string to a CSV string.
 *
 * This function processes a given JSON string and produces a CSV representation
 * of it. The resulting CSV has the headers as the first row and the values as
 * the second row.
 *
 * @param jsonString The JSON string to be converted.
 * @return std::string The resulting CSV string.
 */
std::string json_to_csv(const std::string& jsonString) {
  StaticJsonDocument<2048> doc;  // Adjust the buffer size based on your needs
  deserializeJson(doc, jsonString.c_str());

  std::vector<std::string> headers;
  std::vector<std::string> values;

  json_to_csv_helper(doc.as<JsonVariant>(), headers, values);

  std::string csvString = "";
  for (const auto& header : headers) {
    csvString += header + ",";
  }
  csvString.pop_back();  // remove trailing comma
  csvString += "\n";
  for (const auto& value : values) {
    csvString += value + ",";
  }
  csvString.pop_back();  // remove trailing comma
  csvString += "\n";

  return csvString;
}

void reinit_json(std::ifstream& file) {
  logln_debug("Reset JSON parsing...");
  initialize = true;
  std::string res = get_next_word(file, true);
}

std::string get_next_element_csv(std::ifstream& file, std::vector<std::string>& json_path_tokens) {
  std::string val = get_next_element(file, json_path_tokens, initialize);
  initialize = false;

  if (val == "") {
    return "";
  }

  log_debug("Final generated json element: ");
  logln_debug(val.c_str());
  return json_to_csv(val);
}