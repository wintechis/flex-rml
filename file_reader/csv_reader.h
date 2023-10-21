#ifndef CSV_READER_H
#define CSV_READER_H

#include <fstream>

#include "custom_io.h"
#include "file_reader.h"
  

class CsvReader : public FileReader {
 private:
  std::ifstream file;

 public:
  explicit CsvReader(const std::string& filename);
  bool readNext(std::string& outData) override;
  void reset() override;
};

#endif