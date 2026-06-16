#include "json_source_reader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

JsonSourceReader::JsonSourceReader(const std::filesystem::path& source_path,
                                   const std::string& iterator) {
  data_ = load_data(source_path, iterator);
}

JsonSourceReader::JsonSourceReader(const std::string& source_name,
                                   const std::string& iterator,
                                   const std::string& in_memory_json) {
  data_ = load_data(source_name, iterator, in_memory_json);
}

const std::vector<std::string>& JsonSourceReader::header() const {
  static const std::vector<std::string> empty_header;
  return data_ == nullptr ? empty_header : data_->header;
}

bool JsonSourceReader::next(RowView& row) {
  if (data_ == nullptr || next_row_ >= data_->rows.size()) {
    return false;
  }

  if (data_->column_count == 0) {
    row.fields = {};
    ++next_row_;
    return true;
  }

  const std::size_t offset = next_row_++ * data_->column_count;
  row.fields = std::span<const std::string_view>(data_->row_views.data() + offset, data_->column_count);
  return true;
}

std::optional<std::size_t> JsonSourceReader::row_count_hint() const {
  if (data_ == nullptr) {
    return std::nullopt;
  }
  return data_->rows.size();
}

bool JsonSourceReader::supports_iterator(const std::string& iterator) {
  return !parse_simple_array_iterator(iterator).empty() || iterator == "$[*]" || iterator == "$.[*]";
}

std::string JsonSourceReader::read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Could not open JSON source: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::shared_ptr<const JsonSourceReader::Data> JsonSourceReader::load_data(const std::filesystem::path& source_path,
                                                                          const std::string& iterator,
                                                                          const std::optional<std::string>& in_memory_json) {
  static std::mutex cache_mutex;
  static std::unordered_map<std::string, std::shared_ptr<const Data>> cache;

  const std::string cache_key = source_path.string() + "\n" + iterator + "\n" +
                                (in_memory_json.has_value() ? *in_memory_json : "");
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (auto found = cache.find(cache_key); found != cache.end()) {
      return found->second;
    }
  }

  auto data = std::make_shared<Data>();
  if (!in_memory_json.has_value() && is_root_array_iterator(iterator)) {
    Data flat_data;
    if (try_load_flat_root_array(source_path, flat_data)) {
      build_row_views(flat_data);
      *data = std::move(flat_data);
      {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[cache_key] = data;
      }
      return data;
    }
  }

  const std::string json_text = in_memory_json.has_value() ? *in_memory_json : read_text_file(source_path);
  Json document = Json::parse(json_text);
  const auto path = parse_simple_array_iterator(iterator);
  const Json* container = resolve_path(document, path);
  if (container == nullptr || !container->is_array()) {
    return std::make_shared<Data>();
  }

  if (is_flat_object_array(*container)) {
    load_flat_object_array(*container, *data);
    build_row_views(*data);
    {
      std::lock_guard<std::mutex> lock(cache_mutex);
      cache[cache_key] = data;
    }
    return data;
  }

  for (const auto& item : container->array_range()) {
    collect_header(item, "", data->header);
  }
  std::sort(data->header.begin(), data->header.end());
  data->header.erase(std::unique(data->header.begin(), data->header.end()), data->header.end());

  std::unordered_map<std::string, std::size_t> header_index;
  header_index.reserve(data->header.size());
  for (std::size_t i = 0; i < data->header.size(); ++i) {
    header_index.emplace(data->header[i], i);
  }

  for (const auto& item : container->array_range()) {
    auto expanded_rows = expand_json_row(item, "", header_index, data->header.size());
    data->rows.reserve(data->rows.size() + expanded_rows.size());
    for (auto& expanded_row : expanded_rows) {
      data->rows.push_back(std::move(expanded_row.values));
    }
  }
  build_row_views(*data);

  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache[cache_key] = data;
  }
  return data;
}

std::vector<std::string> JsonSourceReader::parse_simple_array_iterator(const std::string& iterator) {
  if (iterator == "$[*]" || iterator == "$.[*]") {
    return {};
  }
  if (iterator.size() < 5 || iterator.rfind("$.", 0) != 0 || iterator.substr(iterator.size() - 3) != "[*]") {
    return {};
  }

  const std::string path_text = iterator.substr(2, iterator.size() - 5);
  if (path_text.empty()) {
    return {};
  }

  std::vector<std::string> path;
  std::size_t start = 0;
  while (start <= path_text.size()) {
    const std::size_t pos = path_text.find('.', start);
    const std::string segment = path_text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
    if (segment.empty() || segment.find_first_of("[]*?'\"") != std::string::npos) {
      return {};
    }
    path.push_back(segment);
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  return path;
}

bool JsonSourceReader::is_root_array_iterator(const std::string& iterator) {
  return iterator == "$[*]" || iterator == "$.[*]";
}

std::string JsonSourceReader::scalar_to_string(const Json& value) {
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

bool JsonSourceReader::is_scalar_value(const Json& value) {
  return value.is_null() || value.is_string() || value.is_bool() ||
         value.is_number();
}

namespace {

void trim_ascii(std::string& value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();
  if (first >= last) {
    value.clear();
    return;
  }
  const auto first_index = static_cast<std::size_t>(first - value.begin());
  const auto last_index = static_cast<std::size_t>(last - value.begin());
  value.erase(last_index);
  value.erase(0, first_index);
}

bool normalize_flat_array_line(std::string& line, bool& is_object) {
  trim_ascii(line);
  is_object = false;
  if (line.empty() || line == "[" || line == "]") {
    return true;
  }
  if (!line.empty() && line.back() == ',') {
    line.pop_back();
    trim_ascii(line);
  }
  if (line.empty() || line == "[" || line == "]") {
    return true;
  }
  if (line.front() != '{' || line.back() != '}') {
    return false;
  }
  is_object = true;
  return true;
}

void skip_json_ws(std::string_view text, std::size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
}

int hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

bool parse_hex_codepoint(std::string_view text, std::size_t& pos, std::uint32_t& codepoint) {
  if (pos + 4 > text.size()) {
    return false;
  }
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    const int digit = hex_digit(text[pos++]);
    if (digit < 0) {
      return false;
    }
    value = (value << 4) | static_cast<std::uint32_t>(digit);
  }
  codepoint = value;
  return true;
}

void append_utf8(std::uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool parse_json_string_token(std::string_view text, std::size_t& pos, std::string& out) {
  if (pos >= text.size() || text[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  const std::size_t plain_start = pos;
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '"') {
      out.assign(text.substr(plain_start, pos - plain_start - 1));
      return true;
    }
    if (static_cast<unsigned char>(ch) < 0x20) {
      return false;
    }
    if (ch != '\\') {
      continue;
    }
    out.assign(text.substr(plain_start, pos - plain_start - 1));
    if (pos >= text.size()) {
      return false;
    }
    while (pos < text.size()) {
      const char escaped = text[pos++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          out.push_back(escaped);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t codepoint = 0;
          if (!parse_hex_codepoint(text, pos, codepoint)) {
            return false;
          }
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (pos + 6 > text.size() || text[pos] != '\\' || text[pos + 1] != 'u') {
              return false;
            }
            pos += 2;
            std::uint32_t low = 0;
            if (!parse_hex_codepoint(text, pos, low) || low < 0xDC00 || low > 0xDFFF) {
              return false;
            }
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            return false;
          }
          append_utf8(codepoint, out);
          break;
        }
        default:
          return false;
      }
      const std::size_t chunk_start = pos;
      while (pos < text.size() && text[pos] != '"' && text[pos] != '\\') {
        if (static_cast<unsigned char>(text[pos]) < 0x20) {
          return false;
        }
        ++pos;
      }
      out.append(text.substr(chunk_start, pos - chunk_start));
      if (pos >= text.size()) {
        return false;
      }
      if (text[pos] == '"') {
        ++pos;
        return true;
      }
      ++pos;
    }
    return false;
  }
  return false;
}

bool parse_json_number_token(std::string_view text, std::size_t& pos, std::string& out) {
  const std::size_t start = pos;
  if (pos < text.size() && text[pos] == '-') {
    ++pos;
  }
  if (pos >= text.size()) {
    return false;
  }
  if (text[pos] == '0') {
    ++pos;
  } else if (text[pos] >= '1' && text[pos] <= '9') {
    do {
      ++pos;
    } while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0);
  } else {
    return false;
  }
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    const std::size_t fraction_start = pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      ++pos;
    }
    if (pos == fraction_start) {
      return false;
    }
  }
  if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
    ++pos;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
      ++pos;
    }
    const std::size_t exponent_start = pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      ++pos;
    }
    if (pos == exponent_start) {
      return false;
    }
  }
  out.assign(text.substr(start, pos - start));
  return true;
}

bool consume_literal(std::string_view text, std::size_t& pos, std::string_view literal) {
  if (text.substr(pos, literal.size()) != literal) {
    return false;
  }
  pos += literal.size();
  return true;
}

bool parse_json_scalar_token(std::string_view text, std::size_t& pos, std::string& out) {
  if (pos >= text.size()) {
    return false;
  }
  if (text[pos] == '"') {
    return parse_json_string_token(text, pos, out);
  }
  if (text[pos] == '-' || std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
    return parse_json_number_token(text, pos, out);
  }
  if (consume_literal(text, pos, "true")) {
    out = "True";
    return true;
  }
  if (consume_literal(text, pos, "false")) {
    out = "False";
    return true;
  }
  if (consume_literal(text, pos, "null")) {
    out.clear();
    return true;
  }
  return false;
}

}  // namespace

bool JsonSourceReader::try_load_flat_root_array(const std::filesystem::path& source_path,
                                                Data& data) {
  std::ifstream input(source_path);
  if (!input) {
    throw std::runtime_error("Could not open JSON source: " + source_path.string());
  }

  std::unordered_map<std::string, std::size_t> header_index;
  std::vector<std::pair<std::string, std::string>> fields;
  std::string line;
  while (std::getline(input, line)) {
    bool is_object = false;
    if (!normalize_flat_array_line(line, is_object)) {
      return false;
    }
    if (!is_object) {
      continue;
    }
    if (!parse_flat_object_line(line, fields)) {
      return false;
    }
    for (const auto& field : fields) {
      if (header_index.find(field.first) == header_index.end()) {
        const std::size_t index = data.header.size();
        data.header.push_back(field.first);
        header_index.emplace(data.header.back(), index);
        for (auto& existing_row : data.rows) {
          existing_row.emplace_back();
        }
      }
    }

    Row row(data.header.size());
    for (auto& field : fields) {
      if (auto found = header_index.find(field.first); found != header_index.end()) {
        row[found->second] = std::move(field.second);
      }
    }
    data.rows.push_back(std::move(row));
  }

  return true;
}

bool JsonSourceReader::parse_flat_object_line(std::string_view line,
                                              std::vector<std::pair<std::string, std::string>>& fields) {
  fields.clear();
  std::size_t pos = 0;
  skip_json_ws(line, pos);
  if (pos >= line.size() || line[pos] != '{') {
    return false;
  }
  ++pos;
  skip_json_ws(line, pos);
  if (pos < line.size() && line[pos] == '}') {
    ++pos;
    skip_json_ws(line, pos);
    return pos == line.size();
  }

  while (pos < line.size()) {
    std::string key;
    std::string value;
    if (!parse_json_string_token(line, pos, key)) {
      return false;
    }
    skip_json_ws(line, pos);
    if (pos >= line.size() || line[pos] != ':') {
      return false;
    }
    ++pos;
    skip_json_ws(line, pos);
    if (!parse_json_scalar_token(line, pos, value)) {
      return false;
    }
    fields.emplace_back(std::move(key), std::move(value));
    skip_json_ws(line, pos);
    if (pos >= line.size()) {
      return false;
    }
    if (line[pos] == ',') {
      ++pos;
      skip_json_ws(line, pos);
      continue;
    }
    if (line[pos] == '}') {
      ++pos;
      skip_json_ws(line, pos);
      return pos == line.size();
    }
    return false;
  }
  return false;
}

bool JsonSourceReader::is_flat_object_array(const Json& container) {
  if (!container.is_array()) {
    return false;
  }
  for (const auto& item : container.array_range()) {
    if (!item.is_object()) {
      return false;
    }
    for (const auto& member : item.object_range()) {
      if (!is_scalar_value(member.value())) {
        return false;
      }
    }
  }
  return true;
}

void JsonSourceReader::load_flat_object_array(const Json& container,
                                             Data& data) {
  std::unordered_map<std::string, std::size_t> header_index;
  for (const auto& item : container.array_range()) {
    for (const auto& member : item.object_range()) {
      if (header_index.find(member.key()) == header_index.end()) {
        const std::size_t index = data.header.size();
        data.header.push_back(member.key());
        header_index.emplace(data.header.back(), index);
      }
    }
  }

  data.rows.reserve(container.size());
  for (const auto& item : container.array_range()) {
    Row row(data.header.size());
    for (const auto& member : item.object_range()) {
      if (auto found = header_index.find(member.key()); found != header_index.end()) {
        row[found->second] = scalar_to_string(member.value());
      }
    }
    data.rows.push_back(std::move(row));
  }
}

void JsonSourceReader::build_row_views(Data& data) {
  data.row_views.clear();
  data.column_count = data.header.size();
  data.row_views.reserve(data.rows.size() * data.column_count);
  for (const auto& row : data.rows) {
    for (const auto& value : row) {
      data.row_views.emplace_back(value);
    }
  }
}

void JsonSourceReader::collect_header(const Json& value,
                                      const std::string& parent_key,
                                      std::vector<std::string>& header) {
  if (value.is_object()) {
    for (const auto& member : value.object_range()) {
      const std::string nested_key = parent_key.empty() ? member.key() : parent_key + "." + member.key();
      collect_header(member.value(), nested_key, header);
    }
    return;
  }

  if (value.is_array()) {
    if (value.empty()) {
      return;
    }

    const std::string list_key = parent_key.empty() ? "[*]" : parent_key + "[*]";
    for (const auto& item : value.array_range()) {
      if (item.is_object() || item.is_array()) {
        collect_header(item, list_key, header);
      } else {
        header.push_back(list_key);
      }
    }
    return;
  }

  header.push_back(parent_key);
}

std::vector<JsonSourceReader::BuildRow> JsonSourceReader::expand_json_row(
    const Json& value,
    const std::string& parent_key,
    const std::unordered_map<std::string, std::size_t>& header_index,
    std::size_t column_count) {
  if (value.is_object()) {
    std::vector<BuildRow> rows = {BuildRow{Row(column_count), std::vector<unsigned char>(column_count)}};
    for (const auto& member : value.object_range()) {
      const std::string nested_key = parent_key.empty() ? member.key() : parent_key + "." + member.key();
      auto nested_rows = expand_json_row(member.value(), nested_key, header_index, column_count);
      std::vector<BuildRow> combined_rows;
      combined_rows.reserve(rows.size() * nested_rows.size());
      for (const auto& base_row : rows) {
        for (const auto& nested_row : nested_rows) {
          BuildRow combined = base_row;
          for (std::size_t i = 0; i < column_count; ++i) {
            if (nested_row.present[i] != 0 && combined.present[i] == 0) {
              combined.values[i] = nested_row.values[i];
              combined.present[i] = 1;
            }
          }
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
    std::vector<BuildRow> rows;
    for (const auto& item : value.array_range()) {
      if (item.is_object() || item.is_array()) {
        auto nested_rows = expand_json_row(item, list_key, header_index, column_count);
        rows.reserve(rows.size() + nested_rows.size());
        for (auto& nested_row : nested_rows) {
          rows.push_back(std::move(nested_row));
        }
      } else {
        BuildRow row{Row(column_count), std::vector<unsigned char>(column_count)};
        if (auto found = header_index.find(list_key); found != header_index.end()) {
          row.values[found->second] = scalar_to_string(item);
          row.present[found->second] = 1;
        }
        rows.push_back(std::move(row));
      }
    }
    return rows;
  }

  BuildRow row{Row(column_count), std::vector<unsigned char>(column_count)};
  if (auto found = header_index.find(parent_key); found != header_index.end()) {
    row.values[found->second] = scalar_to_string(value);
    row.present[found->second] = 1;
  }
  return {std::move(row)};
}

const JsonSourceReader::Json* JsonSourceReader::resolve_path(const Json& document,
                                                             const std::vector<std::string>& path) {
  const Json* current = &document;
  for (const auto& segment : path) {
    if (!current->is_object() || !current->contains(segment)) {
      return nullptr;
    }
    current = &current->at(segment);
  }
  return current;
}
