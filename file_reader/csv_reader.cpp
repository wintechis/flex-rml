#include "csv_reader.h"

CsvReader::CsvReader(const std::string& source, bool isFile)
    : isFile(isFile) {
  if (isFile) {
    fileStream.open(source);
    if (!fileStream.is_open()) {
      log("Error: Failed to open file ");
      throw_error(source.c_str());
    }
  } else {
    stringStream.str(source);
  }
}

bool CsvReader::readNext(std::string& outData) {
  if (isFile) {
    while (std::getline(fileStream, outData)) {
      if (!outData.empty()) {
        return true;
      }
    }
  } else {
    while (std::getline(stringStream, outData)) {
      if (!outData.empty()) {
        return true;
      }
    }
  }
  return false;
}

void CsvReader::reset() {
  if (isFile) {
    fileStream.clear();
    fileStream.seekg(0, std::ios::beg);
  } else {
    stringStream.clear();
    stringStream.seekg(0, std::ios::beg);
  }
}

void CsvReader::seekg(std::streampos pos) {
  if (isFile) {
    fileStream.seekg(pos);
  } else {
    stringStream.seekg(pos);
  }
}

CsvReader::~CsvReader() {
  close();
}

void CsvReader::close() {
  if (isFile && fileStream.is_open()) {
    fileStream.close();
  }
}