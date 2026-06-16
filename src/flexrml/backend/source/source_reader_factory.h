#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "source_reader.h"

inline constexpr const char* kJsonSourceConfigPrefix = "FLEXRML_SOURCE_JSON\n";
inline constexpr const char* kXmlSourceConfigPrefix = "FLEXRML_SOURCE_XML\n";
inline constexpr const char* kCsvSourceConfigPrefix = "FLEXRML_SOURCE_CSV\n";

std::string encode_json_source_config(const std::string& iterator);
std::string encode_json_source_data(const std::string& iterator, const std::string& data);
std::string encode_xml_source_config(const std::string& iterator);
std::string encode_xml_source_data(const std::string& iterator, const std::string& data);
std::string encode_csv_source_config(const std::string& data);

std::unique_ptr<SourceReader> create_source_reader(
    const std::unordered_map<std::string, std::string>& in_memory_sources,
    const std::string& source_path);
