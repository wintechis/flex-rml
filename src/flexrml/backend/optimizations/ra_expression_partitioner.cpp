#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// used to store string result
static std::string g_result_str;

// Definition of DfEntry structure similar to your Go struct.
struct DfEntry {
  std::string S_invar;
  std::string S_termtype;
  std::string P_invar;
  std::string P_term_map_type;
  std::string O_invar;
  std::string O_termtype;
  std::string O_literal_type;
  std::string G_invar;
  int S_partition;
  int P_partition;
  int O_partition;
  int G_partition;
  int id;
  std::string partition_id;
};

// Function to create a DfEntry. This is analogous to the addData function.
DfEntry addData(const std::string& S_invar, const std::string& S_termtype,
                const std::string& P_invar, const std::string& P_term_map_type,
                const std::string& O_invar, const std::string& O_termtype,
                const std::string& O_literal_type, const std::string& G_invar,
                int id, int s_partition, int p_partition, int o_partition, int g_partition) {
  DfEntry data;
  data.S_invar = S_invar;
  data.S_termtype = S_termtype;
  data.P_invar = P_invar;
  data.P_term_map_type = P_term_map_type;
  data.O_invar = O_invar;
  data.O_termtype = O_termtype;
  data.O_literal_type = O_literal_type;
  data.G_invar = G_invar;
  data.S_partition = s_partition;
  data.P_partition = p_partition;
  data.O_partition = o_partition;
  data.G_partition = g_partition;
  data.id = id;
  data.partition_id = "";
  return data;
}

// Sorts entries by O_termtype and then by O_invar.
std::vector<DfEntry> SortByOtermtypeOinvar(const std::vector<DfEntry>& entries) {
  std::vector<DfEntry> sortedEntries = entries;
  std::sort(sortedEntries.begin(), sortedEntries.end(), [](const DfEntry& a, const DfEntry& b) {
    if (a.O_termtype < b.O_termtype)
      return true;
    if (a.O_termtype > b.O_termtype)
      return false;
    return a.O_invar < b.O_invar;
  });
  return sortedEntries;
}

// Sorts entries based on the provided fieldName ("S_invar", "P_invar", or "G_invar").
std::vector<DfEntry> sortDFEntries(const std::vector<DfEntry>& entries, const std::string& fieldName) {
  std::vector<DfEntry> sortedEntries = entries;
  if (fieldName == "S_invar") {
    std::sort(sortedEntries.begin(), sortedEntries.end(), [](const DfEntry& a, const DfEntry& b) {
      return a.S_invar < b.S_invar;
    });
  } else if (fieldName == "P_invar") {
    std::sort(sortedEntries.begin(), sortedEntries.end(), [](const DfEntry& a, const DfEntry& b) {
      return a.P_invar < b.P_invar;
    });
  } else if (fieldName == "G_invar") {
    std::sort(sortedEntries.begin(), sortedEntries.end(), [](const DfEntry& a, const DfEntry& b) {
      return a.G_invar < b.G_invar;
    });
  } else {
    throw std::runtime_error("Unknown fieldName when sorting");
  }
  return sortedEntries;
}

// Returns true if all entries have P_term_map_type equal to "constant"
bool enforceInvar(const std::vector<DfEntry>& entries) {
  std::set<std::string> ptermMapTypeSet;
  for (const auto& entry : entries) {
    ptermMapTypeSet.insert(entry.P_term_map_type);
  }
  return (ptermMapTypeSet.size() == 1 && ptermMapTypeSet.count("constant") == 1);
}

// Returns the substring before the first '{' character, or the whole string if not found.
std::string get_template_invariant(const std::string& template_string) {
  size_t index = template_string.find('{');
  if (index == std::string::npos)
    return template_string;
  return template_string.substr(0, index);
}

std::string get_invar(const std::vector<std::string>& content) {
  std::string invariant = "";
  std::string term_map = content[0];
  std::string term_map_type = content[1];  // template, constant, reference
  if (term_map_type == "constant") {
    invariant = term_map;
  } else if (term_map_type == "template") {
    invariant = get_template_invariant(term_map);
  } else if (term_map_type == "reference") {
    invariant = "";
  } else {
    std::cout << "Error: term map type not found! Got: " << term_map_type << std::endl;
    std::exit(1);
  }

  return invariant;
}

// Partitions the data and returns a mapping string of id and partition_id.
std::string create_partitions(std::vector<DfEntry> invar_data) {
  // Partitioning subject: sort by S_invar.
  invar_data = sortDFEntries(invar_data, "S_invar");
  int cur_group = 0;
  std::string cur_invariant = "XXXzzzXXXppp";
  for (auto& entry : invar_data) {
    if (entry.S_termtype == "blanknode") {
      entry.S_partition = 0;
    } else if (entry.S_invar.rfind(cur_invariant, 0) == 0) {  // Checks if S_invar starts with cur_invariant.
      entry.S_partition = cur_group;
    } else {
      cur_group++;
      cur_invariant = entry.S_invar;
      entry.S_partition = cur_group;
    }
  }

  // Partitioning predicate: sort by P_invar.
  invar_data = sortDFEntries(invar_data, "P_invar");
  bool enforce_invar = enforceInvar(invar_data);
  cur_group = 0;
  cur_invariant = "XXXzzzXXXppp";
  for (auto& entry : invar_data) {
    if (entry.P_invar == cur_invariant && enforce_invar) {
      entry.P_partition = cur_group;
    } else if (entry.P_invar.rfind(cur_invariant, 0) == 0 && !enforce_invar) {
      entry.P_partition = cur_group;
    } else {
      cur_group++;
      cur_invariant = entry.P_invar;
      entry.P_partition = cur_group;
    }
  }

  // Partitioning object: sort by O_termtype and O_invar.
  invar_data = SortByOtermtypeOinvar(invar_data);
  cur_group = 0;
  cur_invariant = "XXXzzzXXXppp";
  std::string cur_literal_type = "XXXzzzXXXppp";
  for (auto& entry : invar_data) {
    if (entry.O_termtype == "blanknode") {
      entry.O_partition = 0;
    } else if (entry.O_termtype == "literal") {
      if (entry.O_literal_type != cur_literal_type) {
        cur_group++;
        cur_literal_type = entry.O_literal_type;
      }
      entry.O_partition = cur_group;
    } else if (entry.O_invar.rfind(cur_invariant, 0) == 0) {
      entry.O_partition = cur_group;
    } else {
      cur_group++;
      cur_invariant = entry.O_invar;
      entry.O_partition = cur_group;
    }
  }

  // Partitioning graph: sort by G_invar.
  invar_data = sortDFEntries(invar_data, "G_invar");
  cur_group = 0;
  cur_invariant = "XXXzzzXXXppp";
  for (auto& entry : invar_data) {
    if (entry.G_invar.rfind(cur_invariant, 0) == 0) {
      entry.G_partition = cur_group;
    } else {
      cur_group++;
      cur_invariant = entry.G_invar;
      entry.G_partition = cur_group;
    }
  }

  // Generate partition_id for each entry and create mapping string.
  std::ostringstream oss;
  for (auto& entry : invar_data) {
    entry.partition_id = std::to_string(entry.S_partition) + "." +
                         std::to_string(entry.P_partition) + "." +
                         std::to_string(entry.O_partition) + "." +
                         std::to_string(entry.G_partition);
    oss << entry.id << "|||" << entry.partition_id << "\n";
  }
  return oss.str();
}

std::vector<std::string> split_by_substring(const std::string& str, const std::string& delimiter) {
  std::vector<std::string> result;
  size_t start = 0;
  size_t end = str.find(delimiter);

  while (end != std::string::npos) {
    result.push_back(str.substr(start, end - start));
    start = end + delimiter.length();
    end = str.find(delimiter, start);
  }

  result.push_back(str.substr(start));

  return result;
}

extern "C" {
// Function to process the RDF string and return the result
const char* ra_partitioner(const char* ra_expressions_char) {
  std::string ra_expressions_str(ra_expressions_char);
  std::vector<std::string> ra_expressions = split_by_substring(ra_expressions_str, "\n");

  std::vector<DfEntry> data;

  g_result_str = "";

  int id = 0;
  for (const auto& ra_expression : ra_expressions) {
    std::vector<std::string> ra_expression_split = split_by_substring(ra_expression, "|||");
    if (ra_expression_split.size() == 3) {
      std::vector<std::string> s_content = split_by_substring(ra_expression_split[0], "===");
      std::vector<std::string> p_content = split_by_substring(ra_expression_split[1], "===");
      std::vector<std::string> o_content = split_by_substring(ra_expression_split[2], "===");

      std::string s_invar = get_invar(s_content);
      std::string p_invar = get_invar(p_content);
      std::string o_invar = get_invar(o_content);

      data.push_back(addData(s_invar, s_content[2], p_invar, p_content[1], o_invar, o_content[2], "", "", id, -1, -1, -1, -1));
      id++;
    } else {
      std::cout << "Size not supported in partitioner." << std::endl;
      std::exit(1);
    }
  }

  g_result_str = create_partitions(data);

  return g_result_str.c_str();
}
}
