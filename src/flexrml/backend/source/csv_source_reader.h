#pragma once

#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "source_reader.h"

class CsvSourceReader final : public SourceReader {
 public:
  explicit CsvSourceReader(std::unique_ptr<std::istream> input);

  const std::vector<std::string>& header() const override;
  bool next(RowView& row) override;

 private:
  std::unique_ptr<std::istream> input_;
  std::string line_;
  std::vector<std::string> header_;
  std::vector<std::string> fields_;
  std::vector<std::string_view> field_views_;
};
