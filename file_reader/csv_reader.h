#ifndef CSV_READER_H
#define CSV_READER_H

#include <fstream>
#include <sstream>

#include "custom_io.h"
#include "file_reader.h"

class CsvReader : public FileReader {
 private:
  std::ifstream fileStream;
  std::istringstream stringStream;
  bool isFile;

 public:
  explicit CsvReader(const std::string& source, bool isFile = true);
  bool readNext(std::string& outData) override;
  void reset() override;
  void seekg(std::streampos pos) override;
  void close() override;
  ~CsvReader();
};

#endif