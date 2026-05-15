#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "source_reader.h"

inline constexpr const char* kJsonSourceConfigPrefix = "FLEXRML_SOURCE_JSON\n";
inline constexpr const char* kXmlSourceConfigPrefix = "FLEXRML_SOURCE_XML\n";

std::string encode_json_source_config(const std::string& iterator);
std::string encode_xml_source_config(const std::string& iterator);

std::unique_ptr<SourceReader> create_source_reader(
    const std::unordered_map<std::string, std::string>& in_memory_sources,
    const std::string& source_path);
