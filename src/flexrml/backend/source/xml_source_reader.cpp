#include "xml_source_reader.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

XmlSourceReader::XmlSourceReader(const std::filesystem::path& source_path,
                                 const std::string& iterator) {
  data_ = load_data(source_path, iterator);
}

const std::vector<std::string>& XmlSourceReader::header() const {
  static const std::vector<std::string> empty_header;
  return data_ == nullptr ? empty_header : data_->header;
}

bool XmlSourceReader::next(RowView& row) {
  if (data_ == nullptr || next_row_ >= data_->rows.size()) {
    return false;
  }

  if (data_->column_count == 0) {
    row.fields = {};
    ++next_row_;
    return true;
  }

  const std::size_t offset = next_row_++ * data_->column_count;
  row.fields = std::span<const std::string_view>(data_->row_views.data() + offset,
                                                 data_->column_count);
  return true;
}

std::shared_ptr<const XmlSourceReader::Data> XmlSourceReader::load_data(
    const std::filesystem::path& source_path,
    const std::string& iterator) {
  static std::mutex cache_mutex;
  static std::unordered_map<std::string, std::shared_ptr<const Data>> cache;

  const std::string cache_key = source_path.string() + "\n" + iterator;
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (auto found = cache.find(cache_key); found != cache.end()) {
      return found->second;
    }
  }

  pugi::xml_document document;
  const pugi::xml_parse_result result = document.load_file(source_path.string().c_str());
  if (!result) {
    throw std::runtime_error("Could not parse XML source '" + source_path.string() +
                             "': " + result.description());
  }

  pugi::xpath_node_set matches;
  try {
    matches = document.select_nodes(iterator.c_str());
  } catch (const pugi::xpath_exception& exc) {
    throw std::runtime_error("Invalid XML iterator XPath '" + iterator +
                             "' for source '" + source_path.string() + "': " + exc.what());
  }

  auto data = std::make_shared<Data>();
  std::vector<pugi::xml_node> row_nodes;
  row_nodes.reserve(matches.size());
  for (const auto& match : matches) {
    if (auto node = match.node()) {
      row_nodes.push_back(node);
      collect_row_headers(node, data->header);
    }
  }

  data->column_count = data->header.size();
  data->rows.reserve(row_nodes.size());
  for (const auto& node : row_nodes) {
    Row row(data->column_count);
    for (std::size_t i = 0; i < data->header.size(); ++i) {
      row[i] = value_for_reference(node, data->header[i]);
    }
    data->rows.push_back(std::move(row));
  }
  build_row_views(*data);

  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache[cache_key] = data;
  }
  return data;
}

void XmlSourceReader::add_header(std::vector<std::string>& header,
                                 const std::string& name) {
  if (std::find(header.begin(), header.end(), name) == header.end()) {
    header.push_back(name);
  }
}

void XmlSourceReader::collect_row_headers(const pugi::xml_node& node,
                                          std::vector<std::string>& header) {
  for (const auto& attribute : node.attributes()) {
    add_header(header, "@" + std::string(attribute.name()));
  }
  for (const auto& child : node.children()) {
    if (child.type() == pugi::node_element) {
      add_header(header, child.name());
    }
  }
}

std::string XmlSourceReader::value_for_reference(const pugi::xml_node& node,
                                                 const std::string& reference) {
  if (reference.empty()) {
    return "";
  }

  if (reference[0] == '@') {
    return node.attribute(reference.c_str() + 1).value();
  }

  if (reference.find_first_of("/[(") == std::string::npos) {
    return node.child(reference.c_str()).child_value();
  }

  try {
    const pugi::xpath_node result = node.select_node(reference.c_str());
    if (auto attribute = result.attribute()) {
      return attribute.value();
    }
    return result.node().child_value();
  } catch (const pugi::xpath_exception&) {
    return "";
  }
}

void XmlSourceReader::build_row_views(Data& data) {
  data.column_count = data.header.size();
  data.row_views.clear();
  data.row_views.reserve(data.rows.size() * data.column_count);
  for (const auto& row : data.rows) {
    for (const auto& value : row) {
      data.row_views.push_back(value);
    }
  }
}
