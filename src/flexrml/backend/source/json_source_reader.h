#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <jsoncons/json.hpp>

#include "source_reader.h"

class JsonSourceReader final : public SourceReader {
 public:
  JsonSourceReader(const std::filesystem::path& source_path,
                   const std::string& iterator);

  const std::vector<std::string>& header() const override;
  bool next(RowView& row) override;
  std::optional<std::size_t> row_count_hint() const override;

  static bool supports_iterator(const std::string& iterator);

 private:
  using Json = jsoncons::ojson;
  using Row = std::vector<std::string>;
  struct BuildRow {
    Row values;
    std::vector<unsigned char> present;
  };
  struct Data {
    std::vector<std::string> header;
    std::vector<Row> rows;
    std::vector<std::string_view> row_views;
    std::size_t column_count = 0;
  };

  static std::string read_text_file(const std::filesystem::path& path);
  static std::shared_ptr<const Data> load_data(const std::filesystem::path& source_path,
                                               const std::string& iterator);
  static std::vector<std::string> parse_simple_array_iterator(const std::string& iterator);
  static bool is_root_array_iterator(const std::string& iterator);
  static std::string scalar_to_string(const Json& value);
  static bool is_scalar_value(const Json& value);
  static bool try_load_flat_root_array(const std::filesystem::path& source_path,
                                       Data& data);
  static bool parse_flat_object_line(std::string_view line,
                                     std::vector<std::pair<std::string, std::string>>& fields);
  static bool is_flat_object_array(const Json& container);
  static void load_flat_object_array(const Json& container,
                                     Data& data);
  static void build_row_views(Data& data);
  static void collect_header(const Json& value,
                             const std::string& parent_key,
                             std::vector<std::string>& header);
  static std::vector<BuildRow> expand_json_row(const Json& value,
                                               const std::string& parent_key,
                                               const std::unordered_map<std::string, std::size_t>& header_index,
                                               std::size_t column_count);
  static const Json* resolve_path(const Json& document,
                                  const std::vector<std::string>& path);

  std::shared_ptr<const Data> data_;
  std::size_t next_row_ = 0;
};
