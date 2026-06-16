#include "source_reader_factory.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>

#include "csv_source_reader.h"
#include "json_source_reader.h"
#include "xml_source_reader.h"

namespace {

std::unique_ptr<std::istream> open_csv_stream(
    const std::unordered_map<std::string, std::string>& in_memory_sources,
    const std::string& source_path) {
  if (auto it = in_memory_sources.find(source_path); it != in_memory_sources.end()) {
    return std::make_unique<std::istringstream>(it->second);
  }

  auto file = std::make_unique<std::ifstream>(source_path);
  if (!file->is_open()) {
    throw std::runtime_error("Could not open logical source: " + source_path);
  }
  return file;
}

}  // namespace

std::string encode_json_source_config(const std::string& iterator) {
  return std::string(kJsonSourceConfigPrefix) + iterator;
}

std::string encode_json_source_data(const std::string& iterator, const std::string& data) {
  return std::string(kJsonSourceConfigPrefix) + iterator + "\n" + data;
}

std::string encode_xml_source_config(const std::string& iterator) {
  return std::string(kXmlSourceConfigPrefix) + iterator;
}

std::string encode_xml_source_data(const std::string& iterator, const std::string& data) {
  return std::string(kXmlSourceConfigPrefix) + iterator + "\n" + data;
}

std::string encode_csv_source_config(const std::string& data) {
  return std::string(kCsvSourceConfigPrefix) + data;
}

std::tuple<std::string, std::string, bool> split_iterator_and_data(const std::string& value,
                                                                   std::size_t prefix_size) {
  const auto body = value.substr(prefix_size);
  const auto pos = body.find('\n');
  if (pos == std::string::npos) {
    return {body, "", false};
  }
  return {body.substr(0, pos), body.substr(pos + 1), true};
}

std::unique_ptr<SourceReader> create_source_reader(
    const std::unordered_map<std::string, std::string>& in_memory_sources,
    const std::string& source_path) {
  if (auto it = in_memory_sources.find(source_path); it != in_memory_sources.end()) {
    const std::string& value = it->second;
    if (value.rfind(kCsvSourceConfigPrefix, 0) == 0) {
      return std::make_unique<CsvSourceReader>(
          std::make_unique<std::istringstream>(
              value.substr(std::string_view(kCsvSourceConfigPrefix).size())));
    }
    if (value.rfind(kJsonSourceConfigPrefix, 0) == 0) {
      auto [iterator, data, has_data] = split_iterator_and_data(
          value, std::string_view(kJsonSourceConfigPrefix).size());
      if (!has_data) {
        return std::make_unique<JsonSourceReader>(source_path, iterator);
      }
      return std::make_unique<JsonSourceReader>(
          source_path,
          iterator,
          data);
    }
    if (value.rfind(kXmlSourceConfigPrefix, 0) == 0) {
      auto [iterator, data, has_data] = split_iterator_and_data(
          value, std::string_view(kXmlSourceConfigPrefix).size());
      if (!has_data) {
        return std::make_unique<XmlSourceReader>(source_path, iterator);
      }
      return std::make_unique<XmlSourceReader>(
          source_path,
          iterator,
          data);
    }
  }

  return std::make_unique<CsvSourceReader>(open_csv_stream(in_memory_sources, source_path));
}
