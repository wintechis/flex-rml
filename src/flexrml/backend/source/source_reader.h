#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

struct RowView {
  std::span<const std::string_view> fields;
};

class SourceReader {
 public:
  virtual ~SourceReader() = default;

  virtual const std::vector<std::string>& header() const = 0;
  virtual bool next(RowView& row) = 0;
};
