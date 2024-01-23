#ifndef JSON_READER_H
#define JSON_READER_H

#include <fstream>
#include <sstream>

#include "file_reader.h"

class JsonReader : public FileReader {
 private:
  std::ifstream fileStream;
  std::istringstream stringStream;
  bool isFile;
  std::vector<std::string> json_path_tokens;
  std::vector<std::string> split_json_path(const std::string& input);

 public:
  explicit JsonReader(const std::string& source, std::string& json_path,
                      bool isFile = true);
  ~JsonReader() override;

  bool readNext(std::string& outData) override;
  void reset() override;
  void seekg(std::streampos pos) override;
  void close() override;
};

#endif
