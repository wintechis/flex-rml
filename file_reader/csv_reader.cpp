#include "csv_reader.h"

#include <sstream>

CsvReader::CsvReader(const std::string& filename) : file(filename.c_str()) {
   
  if (!file.is_open()) {
    log("Error: Failed to open file ");
    throw_error(filename.c_str());
  }
}

bool CsvReader::readNext(std::string& outData) {
   
  // Keep reading lines from the file until the end of the file is reached
  while (std::getline(file, outData)) {
    // If the current line is not empty, return true
    if (!outData.empty()) {
      return true;
    }
  }

  return false;
}

void CsvReader::reset() {
   
  file.clear();                  // Clear the EOF flag.
  file.seekg(0, std::ios::beg);  // Move to the beginning.
}