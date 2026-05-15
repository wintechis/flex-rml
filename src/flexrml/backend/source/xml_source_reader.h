#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <pugixml.hpp>

#include "source_reader.h"

class XmlSourceReader final : public SourceReader {
 public:
  XmlSourceReader(const std::filesystem::path& source_path,
                  const std::string& iterator);

  const std::vector<std::string>& header() const override;
  bool next(RowView& row) override;
  std::optional<std::size_t> row_count_hint() const override;

 private:
  using Row = std::vector<std::string>;
  struct Data {
    std::vector<std::string> header;
    std::vector<Row> rows;
    std::vector<std::string_view> row_views;
    std::size_t column_count = 0;
  };

  static std::shared_ptr<const Data> load_data(const std::filesystem::path& source_path,
                                               const std::string& iterator);
  static void add_header(std::vector<std::string>& header,
                         const std::string& name);
  static void collect_row_headers(const pugi::xml_node& node,
                                  std::vector<std::string>& header);
  static std::string value_for_reference(const pugi::xml_node& node,
                                         const std::string& reference);
  static void build_row_views(Data& data);

  std::shared_ptr<const Data> data_;
  std::size_t next_row_ = 0;
};
