#ifndef FILE_READER_H
#define FILE_READER_H

#include <string>

class FileReader {
 public:
  virtual ~FileReader() = default;

  virtual bool readNext(std::string& outData) = 0;
  virtual void reset() = 0;
  virtual void seekg(std::streampos pos) = 0;
  virtual void close() = 0;
};

#endif