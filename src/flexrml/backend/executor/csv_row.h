#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

void project_row_into(const std::vector<std::string>& split_line,
                      const std::vector<int>& projected_indices,
                      std::vector<std::string>& projected_row);

void project_row_into(const std::vector<std::string_view>& split_line,
                      const std::vector<int>& projected_indices,
                      std::vector<std::string_view>& projected_row);

void project_row_into(std::span<const std::string_view> split_line,
                      const std::vector<int>& projected_indices,
                      std::vector<std::string_view>& projected_row);

void project_row_views_from_strings(const std::vector<std::string>& projected_row,
                                    std::vector<std::string_view>& projected_row_views);

void materialize_row_views(const std::vector<std::string_view>& projected_row_views,
                           std::vector<std::string>& projected_row);

bool row_has_skip_value(const std::vector<std::string_view>& projected_row);
