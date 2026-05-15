#pragma once

#include <optional>
#include <string>

std::string preprocess_json_to_csv(const std::string& source_path,
                                   const std::string& iterator,
                                   const std::optional<std::string>& in_memory_json = std::nullopt);
