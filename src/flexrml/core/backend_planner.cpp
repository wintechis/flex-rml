#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "physical_plan.h"

std::string ra_partitioner_string(const std::string& ra_expressions);

namespace {

using Node = std::map<std::string, std::string>;
using LogicalExpression = std::vector<Node>;
using Plan = std::vector<std::vector<std::string>>;

std::vector<std::string> split(const std::string& value, const std::string& delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    std::size_t pos = value.find(delimiter, start);
    if (pos == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }
    parts.push_back(value.substr(start, pos - start));
    start = pos + delimiter.size();
  }
  return parts;
}

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string join(const std::vector<std::string>& values, const std::string& delimiter) {
  std::ostringstream output;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      output << delimiter;
    }
    output << values[i];
  }
  return output.str();
}

std::string replace_all(std::string value, const std::string& from, const std::string& to) {
  if (from.empty()) {
    return value;
  }
  std::size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
  return value;
}

std::size_t count_substr(const std::string& value, const std::string& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = value.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

std::vector<std::string> non_empty_lines(const std::string& value) {
  std::vector<std::string> lines;
  std::istringstream input(value);
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::pair<std::string, std::string> split_projection_segment(const std::string& segment) {
  const auto pos = segment.rfind("](");
  if (pos == std::string::npos) {
    throw std::runtime_error("Could not parse projection segment: " + segment);
  }
  auto source = trim(segment.substr(pos + 2));
  while (!source.empty() && source.back() == ')') {
    source.pop_back();
  }
  return {trim(segment.substr(0, pos)), trim(source)};
}

std::string nth_create_arg(const std::string& segment, std::size_t index) {
  auto parts = split(segment, "create(");
  if (index >= parts.size()) {
    throw std::runtime_error("Could not parse create() expression.");
  }
  auto arg = parts[index];
  const auto close = arg.find(')');
  if (close != std::string::npos) {
    arg = arg.substr(0, close);
  }
  return replace_all(trim(arg), ",", "===");
}

Node creation_node(const std::string& segment, int create_count) {
  if (create_count != 3 && create_count != 4) {
    throw std::runtime_error("Could not parse relational algebra expression.");
  }
  Node creation{{"type", "creation"},
                {"s_content", nth_create_arg(segment, 1)},
                {"p_content", nth_create_arg(segment, 2)},
                {"o_content", nth_create_arg(segment, 3)}};
  if (create_count == 4) {
    creation["g_content"] = nth_create_arg(segment, 4);
  }
  return creation;
}

std::vector<LogicalExpression> parse_ra(const std::string& ra_text) {
  std::vector<LogicalExpression> expressions;
  for (const auto& ra_expression : non_empty_lines(ra_text)) {
    const auto pi_count = count_substr(ra_expression, "pi[");
    auto pi_parts = split(ra_expression, "pi[");

    if (pi_count == 2) {
      auto [projection_arguments, source] = split_projection_segment(pi_parts.back());
      Node projection{{"type", "projection"},
                      {"in_relation", source},
                      {"arguments", replace_all(projection_arguments, ",", "===")}};
      auto creation = creation_node(pi_parts[1], static_cast<int>(count_substr(pi_parts[1], "create(")));
      expressions.push_back({projection, creation});
    } else if (pi_count == 3) {
      auto parent_parts = split(pi_parts[2], "bowtie");
      auto [projection_arguments1, source1] = split_projection_segment(parent_parts[0]);
      auto [projection_arguments2, source2] = split_projection_segment(pi_parts.back());

      Node projection1{{"type", "projection"},
                       {"in_relation", source1},
                       {"arguments", trim(replace_all(projection_arguments1, ",", "==="))},
                       {"name", "parent"}};
      Node projection2{{"type", "projection"},
                       {"in_relation", source2},
                       {"arguments", trim(replace_all(projection_arguments2, ",", "==="))},
                       {"name", "child"}};

      Node join_node;
      if (parent_parts.size() < 2 || split(parent_parts[1], "[").size() == 1) {
        if (source1 != source2) {
          throw std::runtime_error("Natural joins across different sources are not supported.");
        }
      } else {
        auto join_arguments = split(split(parent_parts[1], "[")[1], "]")[0];
        std::vector<std::string> physical_join_parts;
        for (const auto& join_argument : split(join_arguments, ",")) {
          auto join_parts = split(trim(replace_all(join_argument, "=", "===")), "===");
          if (join_parts.size() < 2) {
            throw std::runtime_error("Could not parse join arguments.");
          }
          physical_join_parts.push_back(replace_all(join_parts[0], source1, "parent"));
          physical_join_parts.push_back(replace_all(join_parts[1], source2, "child"));
        }
        join_node = {{"type", "equi-join"}, {"arguments", join(physical_join_parts, "===")}};
      }

      auto creation = creation_node(pi_parts[1], static_cast<int>(count_substr(pi_parts[1], "create(")));
      creation["s_content"] = replace_all(creation["s_content"], source1, "parent");
      creation["p_content"] = replace_all(creation["p_content"], source1, "parent");
      creation["o_content"] = replace_all(creation["o_content"], source2, "child");

      if (join_node.empty()) {
        Node projection{{"type", "projection"},
                        {"in_relation", source1},
                        {"arguments", projection1["arguments"] + "===" + projection2["arguments"]}};
        expressions.push_back({projection, creation});
      } else {
        expressions.push_back({projection1, projection2, join_node, creation});
      }
    } else {
      throw std::runtime_error("Detected unsupported relational algebra expression: " + ra_expression);
    }
  }
  return expressions;
}

std::string typed_constant_value(const std::vector<std::string>& element) {
  if (element.size() < 3) {
    throw std::runtime_error("Invalid create() element.");
  }
  if (element[2] == "iri") {
    return "<" + element[0] + ">";
  }
  if (element[2] == "blanknode") {
    return "_:" + element[0];
  }
  if (element[2] != "literal") {
    return "\"" + element[0] + "\"";
  }

  std::string language = element.size() > 3 ? element[3] : "None";
  std::string datatype = element.size() > 4 ? element[4] : "None";
  if (language == "None" && datatype == "None") {
    if (element[0] == "true" || element[0] == "false") {
      datatype = "http://www.w3.org/2001/XMLSchema#boolean";
    } else if (std::regex_match(element[0], std::regex(R"(^-?(0|[1-9][0-9]*)$)"))) {
      datatype = "http://www.w3.org/2001/XMLSchema#integer";
    } else if (std::regex_match(element[0], std::regex(R"(^-?(0|[1-9][0-9]*)\.[0-9]+$)"))) {
      datatype = "http://www.w3.org/2001/XMLSchema#decimal";
    }
  }

  std::string value = "\"" + element[0] + "\"";
  if (language != "None") {
    value += "@" + language;
  } else if (datatype != "None") {
    value += "^^<" + datatype + ">";
  }
  return value;
}

void fold_creation(Node& creation) {
  std::vector<std::string> keys = {"s_content", "p_content", "o_content"};
  if (creation.count("g_content") != 0) {
    keys.push_back("g_content");
  }

  for (const auto& key : keys) {
    auto element = split(creation[key], "===");
    if (element.size() > 1 && element[1] == "template" && element[0].find('{') == std::string::npos) {
      element[1] = "constant";
    }
    if (element.size() <= 1 || element[1] != "constant") {
      continue;
    }
    creation[key] = typed_constant_value(element) + "===preformatted===xxx";
  }
}

void constant_folding(std::vector<LogicalExpression>& expressions) {
  for (auto& expression : expressions) {
    if (expression.size() == 2) {
      fold_creation(expression[1]);
    } else if (expression.size() == 4) {
      fold_creation(expression[3]);
    } else {
      throw std::runtime_error("Constant folding is not supported for this RA expression.");
    }
  }
}

std::vector<std::string> partition_ids(const std::vector<LogicalExpression>& expressions) {
  std::string input;
  for (const auto& expression : expressions) {
    const auto& creation = expression.size() == 2 ? expression[1] : expression[3];
    input += creation.at("s_content") + "|||" + creation.at("p_content") + "|||" + creation.at("o_content");
    if (creation.count("g_content") != 0) {
      input += "|||" + creation.at("g_content");
    }
    input += "\n";
  }
  if (!input.empty()) {
    input.pop_back();
  }

  const auto partition_result = ra_partitioner_string(input);

  std::vector<std::pair<int, std::string>> rows;
  for (const auto& line : non_empty_lines(partition_result)) {
    auto parts = split(line, "|||");
    if (parts.size() >= 2) {
      rows.emplace_back(std::stoi(parts[0]), parts[1]);
    }
  }
  std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
    return left.first < right.first;
  });

  std::vector<std::string> result;
  for (const auto& row : rows) {
    result.push_back(row.second);
  }
  return result;
}

std::vector<Plan> create_physical_plans(const std::vector<LogicalExpression>& expressions,
                                        const std::vector<std::string>& base_uris,
                                        const std::string& default_base_uri,
                                        const std::string& output_file_path,
                                        const std::string& continue_on_error) {
  std::vector<Plan> plans;
  for (std::size_t i = 0; i < expressions.size(); ++i) {
    const auto& expression = expressions[i];
    const auto plan_base_uri = i < base_uris.size() && !base_uris[i].empty() ? base_uris[i] : default_base_uri;

    if (expression.size() == 2) {
      const auto& projection = expression[0];
      const auto& creation = expression[1];
      Plan plan{{"seq_scan", projection.at("in_relation"), projection.at("arguments")}};
      if (creation.count("g_content") == 0) {
        plan.push_back({"format", creation.at("s_content"), creation.at("p_content"), creation.at("o_content")});
      } else {
        plan.push_back({"format", creation.at("s_content"), creation.at("p_content"),
                        creation.at("o_content"), creation.at("g_content")});
      }
      plan.push_back({output_file_path});
      plan.push_back({plan_base_uri});
      plan.push_back({continue_on_error});
      plans.push_back(plan);
    } else if (expression.size() == 4) {
      const auto& creation = expression[3];
      Plan plan{{"seq_scan", expression[0].at("in_relation"), expression[0].at("arguments"), expression[0].at("name")},
                {"seq_scan", expression[1].at("in_relation"), expression[1].at("arguments"), expression[1].at("name")},
                {"hash_join", expression[2].at("arguments")}};
      if (creation.count("g_content") == 0) {
        plan.push_back({"format", creation.at("s_content"), creation.at("p_content"), creation.at("o_content")});
      } else {
        plan.push_back({"format", creation.at("s_content"), creation.at("p_content"),
                        creation.at("o_content"), creation.at("g_content")});
      }
      plan.push_back({output_file_path});
      plan.push_back({plan_base_uri});
      plan.push_back({continue_on_error});
      plans.push_back(plan);
    } else {
      throw std::runtime_error("Unsupported length of RA expression.");
    }
  }
  return plans;
}

std::string plan_to_string(const Plan& plan) {
  std::ostringstream output;
  for (const auto& element : plan) {
    output << join(element, "|||") << "\n";
  }
  return output.str();
}

std::uintmax_t plan_file_size(const Plan& plan) {
  std::uintmax_t total = 0;
  auto add_size = [&](const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
      total += std::filesystem::file_size(path, ec);
    }
  };

  if (plan.size() == 5) {
    add_size(plan[0][1]);
  } else if (plan.size() == 7) {
    add_size(plan[0][1]);
    add_size(plan[1][1]);
  } else {
    throw std::runtime_error("Error ordering plans.");
  }
  return total;
}

std::vector<std::vector<Plan>> group_plans(const std::vector<std::string>& ids,
                                           const std::vector<Plan>& plans,
                                           bool heuristic_ordering,
                                           bool can_order) {
  std::vector<std::pair<std::string, std::vector<Plan>>> grouped;
  for (std::size_t i = 0; i < plans.size(); ++i) {
    const auto id = i < ids.size() ? ids[i] : std::to_string(i);
    auto found = std::find_if(grouped.begin(), grouped.end(), [&](const auto& entry) {
      return entry.first == id;
    });
    if (found == grouped.end()) {
      grouped.push_back({id, {plans[i]}});
    } else {
      found->second.push_back(plans[i]);
    }
  }

  std::vector<std::vector<Plan>> partitions;
  for (auto& entry : grouped) {
    partitions.push_back(std::move(entry.second));
  }

  if (heuristic_ordering && can_order) {
    std::stable_sort(partitions.begin(), partitions.end(), [](const auto& left, const auto& right) {
      std::uintmax_t left_size = 0;
      std::uintmax_t right_size = 0;
      for (const auto& plan : left) {
        left_size += plan_file_size(plan);
      }
      for (const auto& plan : right) {
        right_size += plan_file_size(plan);
      }
      return left_size > right_size;
    });
  }
  return partitions;
}

std::string serialize_partitions(const std::vector<std::vector<Plan>>& partitions) {
  std::ostringstream output;
  for (const auto& partition : partitions) {
    for (const auto& plan : partition) {
      auto plan_str = plan_to_string(plan);
      while (!plan_str.empty() && (plan_str.back() == '\n' || plan_str.back() == '\r')) {
        plan_str.pop_back();
      }
      output << plan_str << "PxPwPePrP";
    }
    output << "TTTtttTTTtttTTT";
  }
  return output.str();
}

SimplePlan plan_to_simple_plan(const Plan& plan) {
  if (plan.size() != 5 || plan[0].size() < 3 || plan[1].size() < 4 || plan[2].empty() || plan[3].empty()) {
    throw std::runtime_error("Invalid simple physical plan.");
  }

  SimplePlan simple;
  simple.input_file_name = plan[0][1];
  simple.projected_attributes = split(plan[0][2], "===");
  simple.output_file_name = plan[2][0];
  simple.base_uri = plan[3][0];
  simple.s_content = split(plan[1][1], "===");
  simple.p_content = split(plan[1][2], "===");
  simple.o_content = split(plan[1][3], "===");
  if (plan[1].size() > 4) {
    simple.generate_graph = true;
    simple.g_content = split(plan[1][4], "===");
  }
  return simple;
}

PhysicalPlan plan_to_physical_plan(const Plan& plan) {
  PhysicalPlan physical_plan;
  physical_plan.has_function_call = plan_to_string(plan).find("==FUNC==") != std::string::npos;
  if (plan.size() == 5) {
    physical_plan.kind = PhysicalPlanKind::Simple;
    physical_plan.simple = plan_to_simple_plan(plan);
  } else if (plan.size() == 7) {
    physical_plan.kind = PhysicalPlanKind::Complex;
    physical_plan.raw = plan_to_string(plan);
    while (!physical_plan.raw.empty() && (physical_plan.raw.back() == '\n' || physical_plan.raw.back() == '\r')) {
      physical_plan.raw.pop_back();
    }
  } else {
    throw std::runtime_error("Unsupported physical plan size: " + std::to_string(plan.size()));
  }
  return physical_plan;
}

std::vector<PlanPartition> to_typed_partitions(const std::vector<std::vector<Plan>>& partitions) {
  std::vector<PlanPartition> typed_partitions;
  typed_partitions.reserve(partitions.size());
  for (const auto& partition : partitions) {
    PlanPartition typed_partition;
    typed_partition.plans.reserve(partition.size());
    for (const auto& plan : partition) {
      PhysicalPlan physical_plan = plan_to_physical_plan(plan);
      typed_partition.has_function_call = typed_partition.has_function_call || physical_plan.has_function_call;
      typed_partition.plans.push_back(std::move(physical_plan));
    }
    typed_partitions.push_back(std::move(typed_partition));
  }
  return typed_partitions;
}

std::vector<std::string> parse_base_uris(const char* value) {
  if (value == nullptr) {
    return {};
  }
  return split(value, "\n");
}

}  // namespace

std::string get_ra_sources_string(const std::string& ra_text) {
  auto expressions = parse_ra(ra_text);
  std::vector<std::string> lines;
  for (const auto& expression : expressions) {
    if (expression.size() == 2) {
      lines.push_back(expression[0].at("in_relation"));
    } else if (expression.size() == 4) {
      lines.push_back(expression[0].at("in_relation") + "|||" + expression[1].at("in_relation"));
    }
  }
  return join(lines, "\n");
}

std::string create_plan_partitions_string(const std::string& ra_text,
                                          const std::string& base_uris_text,
                                          const std::string& default_base_uri,
                                          const std::string& output_file_path,
                                          const std::string& continue_on_error,
                                          bool materialize_constants,
                                          bool heuristic_ordering,
                                          bool can_order) {
  auto expressions = parse_ra(ra_text);
  auto ids = partition_ids(expressions);
  if (materialize_constants) {
    constant_folding(expressions);
  }
  auto plans = create_physical_plans(
      expressions,
      parse_base_uris(base_uris_text.c_str()),
      default_base_uri,
      output_file_path,
      continue_on_error);
  auto partitions = group_plans(ids, plans, heuristic_ordering, can_order);
  return serialize_partitions(partitions);
}

std::vector<PlanPartition> create_plan_partitions(const std::string& ra_text,
                                                  const std::string& base_uris_text,
                                                  const std::string& default_base_uri,
                                                  const std::string& output_file_path,
                                                  const std::string& continue_on_error,
                                                  bool materialize_constants,
                                                  bool heuristic_ordering,
                                                  bool can_order) {
  auto expressions = parse_ra(ra_text);
  auto ids = partition_ids(expressions);
  if (materialize_constants) {
    constant_folding(expressions);
  }
  auto plans = create_physical_plans(
      expressions,
      parse_base_uris(base_uris_text.c_str()),
      default_base_uri,
      output_file_path,
      continue_on_error);
  auto partitions = group_plans(ids, plans, heuristic_ordering, can_order);
  return to_typed_partitions(partitions);
}
