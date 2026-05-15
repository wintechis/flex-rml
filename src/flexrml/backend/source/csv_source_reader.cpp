#include "csv_source_reader.h"

#include <utility>

#include "utils.h"

CsvSourceReader::CsvSourceReader(std::unique_ptr<std::istream> input)
    : input_(std::move(input)) {
  line_.reserve(512);
  fields_.reserve(32);
  field_views_.reserve(32);

  if (input_ != nullptr && std::getline(*input_, line_)) {
    header_ = split_csv_line(line_, ',');
  }
}

const std::vector<std::string>& CsvSourceReader::header() const {
  return header_;
}

bool CsvSourceReader::next(RowView& row) {
  if (input_ == nullptr || !std::getline(*input_, line_)) {
    return false;
  }

  if (split_csv_line_views_into(line_, ',', field_views_)) {
    row.fields = field_views_;
    return true;
  }

  split_csv_line_into(line_, ',', fields_);
  if (field_views_.size() < fields_.size()) {
    field_views_.resize(fields_.size());
  }
  for (std::size_t i = 0; i < fields_.size(); ++i) {
    field_views_[i] = fields_[i];
  }
  field_views_.resize(fields_.size());
  row.fields = field_views_;
  return true;
}
