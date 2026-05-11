#include "json_preprocessor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpath/jsonpath.hpp>

namespace {

using Row = std::map<std::string, std::string>;
using Json = jsoncons::ojson;

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Could not open JSON source: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string scalar_to_csv_value(const Json& value) {
  if (value.is_null()) {
    return "";
  }
  if (value.is_string()) {
    return value.as<std::string>();
  }
  if (value.is_bool()) {
    return value.as<bool>() ? "True" : "False";
  }
  return value.to_string();
}

std::vector<Row> expand_json_row(const Json& value,
                                 const std::string& parent_key = "") {
  if (value.is_object()) {
    std::vector<Row> rows = {Row{}};
    for (const auto& member : value.object_range()) {
      const std::string nested_key = parent_key.empty()
                                         ? member.key()
                                         : parent_key + "." + member.key();
      auto nested_rows = expand_json_row(member.value(), nested_key);
      std::vector<Row> combined_rows;
      for (const auto& base_row : rows) {
        for (const auto& nested_row : nested_rows) {
          Row combined = base_row;
          combined.insert(nested_row.begin(), nested_row.end());
          combined_rows.push_back(std::move(combined));
        }
      }
      rows = std::move(combined_rows);
    }
    return rows;
  }

  if (value.is_array()) {
    if (value.empty()) {
      return {};
    }

    const std::string list_key = parent_key.empty() ? "[*]" : parent_key + "[*]";
    std::vector<Row> rows;
    for (const auto& item : value.array_range()) {
      if (item.is_object() || item.is_array()) {
        auto nested_rows = expand_json_row(item, list_key);
        rows.insert(rows.end(), nested_rows.begin(), nested_rows.end());
      } else {
        rows.push_back(Row{{list_key, scalar_to_csv_value(item)}});
      }
    }
    return rows;
  }

  return {Row{{parent_key, scalar_to_csv_value(value)}}};
}

std::string csv_escape(const std::string& value) {
  bool must_quote = value.find_first_of(",\"\r\n") != std::string::npos;
  if (!must_quote) {
    return value;
  }

  std::string escaped = "\"";
  for (char ch : value) {
    if (ch == '"') {
      escaped += "\"\"";
    } else {
      escaped += ch;
    }
  }
  escaped += "\"";
  return escaped;
}

std::string write_csv_rows(const std::vector<Row>& rows) {
  if (rows.empty()) {
    return "";
  }

  std::set<std::string> field_set;
  for (const auto& row : rows) {
    for (const auto& [key, _] : row) {
      field_set.insert(key);
    }
  }

  std::vector<std::string> fields(field_set.begin(), field_set.end());
  std::ostringstream output;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0) {
      output << ",";
    }
    output << csv_escape(fields[i]);
  }
  output << "\r\n";

  for (const auto& row : rows) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
      if (i != 0) {
        output << ",";
      }
      auto value = row.find(fields[i]);
      if (value != row.end()) {
        output << csv_escape(value->second);
      }
    }
    output << "\r\n";
  }

  return output.str();
}

std::string write_scalar_csv(const Json& matches) {
  std::ostringstream output;
  output << "value\r\n";
  for (const auto& value : matches.array_range()) {
    output << csv_escape(scalar_to_csv_value(value)) << "\r\n";
  }
  return output.str();
}

}  // namespace

std::string preprocess_json_to_csv(const std::string& source_path,
                                   const std::string& iterator,
                                   const std::optional<std::string>& in_memory_json) {
  const std::string json_text = in_memory_json.has_value()
                                    ? *in_memory_json
                                    : read_text_file(source_path);
  Json document = Json::parse(json_text);
  Json matches = jsoncons::jsonpath::json_query(document, iterator);

  if (matches.empty()) {
    return "";
  }

  const auto& first = matches.at(0);
  if (!first.is_object()) {
    return write_scalar_csv(matches);
  }

  std::vector<Row> flat_rows;
  for (const auto& row : matches.array_range()) {
    auto expanded_rows = expand_json_row(row);
    flat_rows.insert(flat_rows.end(), expanded_rows.begin(), expanded_rows.end());
  }

  return write_csv_rows(flat_rows);
}
