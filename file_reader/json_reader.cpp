#include "json_reader.h"

#include <iostream>

#include "../json_handling.h"

JsonReader::JsonReader(const std::string& source, std::string& json_path,
                       bool isFile)
    : isFile(isFile) {
  if (isFile) {
    fileStream.open(source);
    if (!fileStream.is_open()) {
      log("Error: Failed to open file");
      throw_error(source.c_str());
    }
  } else {
    stringStream.str(source);
  }

  json_path_tokens = split_json_path(json_path);
}

bool JsonReader::readNext(std::string& outData) {
  if (isFile) {
    outData = get_next_element_csv(fileStream, json_path_tokens);
    if (!outData.empty()) {
      return true;
    }
  }
  return false;
}

void JsonReader::reset() {}

void JsonReader::seekg(std::streampos pos) {}

void JsonReader::close() {
  if (isFile && fileStream.is_open()) {
    fileStream.close();
  }
}

JsonReader::~JsonReader() { close(); }

std::vector<std::string> JsonReader::split_json_path(const std::string& input) {
  std::vector<std::string> result;
  std::string temp;  // Temporary string to hold each token

  for (size_t i = 0; i < input.length(); ++i) {
    char ch = input[i];
    if (ch == '.') {
      if (!temp.empty()) {
        if (temp != "$") {
          temp = "\"" + temp + "\"";
        }
        result.push_back(temp);
        temp.clear();
      }
    } else if (ch == '[') {
      if (!temp.empty()) {
        temp = "\"" + temp + "\"";
        result.push_back(temp);
        temp.clear();
      }
      while (i < input.length() && input[i] != ']') {
        temp += input[i++];
      }
      if (i < input.length() && input[i] == ']') {
        temp += input[i];
      }
      result.push_back(temp);
      temp.clear();
    } else {
      temp += ch;
    }
  }

  if (!temp.empty()) {
    if (temp != "$") {
      temp = "\"" + temp + "\"";
    }
    result.push_back(temp);
  }

  return result;
}