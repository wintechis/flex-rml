#include "csv_row.h"

#include <algorithm>

#include "definitions.h"

void project_row_into(const std::vector<std::string>& split_line,
                      const std::vector<int>& projected_indices,
                      std::vector<std::string>& projected_row) {
  if (projected_row.size() < projected_indices.size()) {
    projected_row.resize(projected_indices.size());
  }
  for (std::size_t i = 0; i < projected_indices.size(); ++i) {
    projected_row[i] = split_line[projected_indices[i]];
  }
  projected_row.resize(projected_indices.size());
}

void project_row_into(const std::vector<std::string_view>& split_line,
                      const std::vector<int>& projected_indices,
                      std::vector<std::string_view>& projected_row) {
  if (projected_row.size() < projected_indices.size()) {
    projected_row.resize(projected_indices.size());
  }
  for (std::size_t i = 0; i < projected_indices.size(); ++i) {
    projected_row[i] = split_line[projected_indices[i]];
  }
  projected_row.resize(projected_indices.size());
}

void project_row_views_from_strings(const std::vector<std::string>& projected_row,
                                    std::vector<std::string_view>& projected_row_views) {
  if (projected_row_views.size() < projected_row.size()) {
    projected_row_views.resize(projected_row.size());
  }
  for (std::size_t i = 0; i < projected_row.size(); ++i) {
    projected_row_views[i] = projected_row[i];
  }
  projected_row_views.resize(projected_row.size());
}

void materialize_row_views(const std::vector<std::string_view>& projected_row_views,
                           std::vector<std::string>& projected_row) {
  if (projected_row.size() < projected_row_views.size()) {
    projected_row.resize(projected_row_views.size());
  }
  for (std::size_t i = 0; i < projected_row_views.size(); ++i) {
    projected_row[i].assign(projected_row_views[i]);
  }
  projected_row.resize(projected_row_views.size());
}

bool row_has_skip_value(const std::vector<std::string_view>& projected_row) {
  for (const auto& target : values_to_skip) {
    if (std::any_of(projected_row.begin(), projected_row.end(), [&target](std::string_view value) { return value == target; })) {
      return true;
    }
  }
  return false;
}
