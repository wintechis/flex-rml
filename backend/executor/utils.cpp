#include "utils.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

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

std::vector<std::string> split_csv_line(const std::string& str, char separator) {
  std::vector<std::string> result;
  result.reserve(64);

  std::string token;
  token.reserve(str.size());
  bool insideQuotes = false;

  for (char c : str) {
    if (c == '"') {
      // Handle quote escaping
      if (insideQuotes && !token.empty() && token.back() == '"') {
        token.pop_back();
      } else {
        insideQuotes = !insideQuotes;
      }
    } else if (c == separator && !insideQuotes) {
      // End of token, push and clear
      result.push_back(std::move(token));
      token.clear();
    } else {
      // Only append non-control characters.
      if (!iscntrl(static_cast<unsigned char>(c))) {
        token.push_back(c);
      }
    }
  }

  if (insideQuotes) {
    std::cout << "Runtime error occurred. Malformed CSV: unmatched quote." << std::endl;
    std::exit(1);
  }
  result.push_back(std::move(token));

  return result;
}

int get_index(const std::vector<std::string>& input_vector, std::string searched_element) {
  auto it = std::find(input_vector.begin(), input_vector.end(), searched_element);

  if (it != input_vector.end()) {
    int index = std::distance(input_vector.begin(), it);
    return index;
  } else {
    std::cout << "Element '" << searched_element << "' not found in input data!" << std::endl;
    std::exit(1);
  }
}

uint64_t combinedHash(std::vector<std::string>& fields) {
  uint64_t hash = 0;
  for (const auto& field : fields) {
    uint64_t fieldHash = XXH3_64bits(field.data(), field.size());
    hash ^= fieldHash + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
  }
  return hash;
}

std::string replace_substring(const std::string& original, const std::string& toReplace, const std::string& replacement) {
  std::string result = original;
  std::size_t pos = result.find(toReplace);
  if (pos != std::string::npos) {
    result.replace(pos, toReplace.length(), replacement);
  }
  return result;
}

std::string clean_blank_node(std::string_view raw) {
  // sequence number
  static std::atomic_uint64_t seq{0};

  std::string out;
  out.reserve(raw.size());

  // keep letters, digits, '_', '.', '-'  (no ':' or '/')
  for (unsigned char c : raw) {
    if (std::isalnum(c) || c == '_' || c == '.' || c == '-')
      out.push_back(static_cast<char>(c));
  }

  // trim leading '.' or '-' …
  while (!out.empty() && (out.front() == '.' || out.front() == '-'))
    out.erase(out.begin());
  // … and trailing '.'
  while (!out.empty() && out.back() == '.')
    out.pop_back();

  // fallback if nothing valid remains
  if (out.empty()) {
    uint64_t id = seq.fetch_add(1, std::memory_order_relaxed);
    out = "bnode" + std::to_string(id);
  }
  return out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////// CREATE Function //////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<std::string> extract_substrings(const std::string& str) {
  std::vector<std::string> substrings;
  substrings.reserve(5);

  size_t startPos = 0, endPos = 0;

  while ((startPos = str.find('{', startPos)) != std::string::npos) {
    if (startPos == 0 || str[startPos - 1] != '\\') {
      endPos = str.find('}', startPos);
      if (endPos != std::string::npos) {
        substrings.emplace_back(str, startPos + 1, endPos - startPos - 1);
        startPos = endPos + 1;
      } else {
        break;  // no matching closing brace found
      }
    } else {
      startPos++;
    }
  }
  return substrings;
}

std::string make_safe_iri(const std::string& node) {
  // Lookup table for encoding special characters
  // clang-format off
      static const std::unordered_map<char, std::string> encode_map = {
          {' ', "%20"}, {'!', "%21"}, {'\"', "%22"}, {'#', "%23"}, {'$', "%24"}, 
          {'%', "%25"}, {'&', "%26"}, {'\'', "%27"}, {'(', "%28"}, {')', "%29"}, 
          {'*', "%2A"}, {'+', "%2B"}, {',', "%2C"}, {'/', "%2F"}, {':', "%3A"}, 
          {';', "%3B"}, {'<', "%3C"}, {'=', "%3D"}, {'>', "%3E"}, {'?', "%3F"}, 
          {'@', "%40"}, {'[', "%5B"}, {'\\', "%5C"}, {']', "%5D"}, {'{', "%7B"}, 
          {'|', "%7C"}, {'}', "%7D"}
        };
  // clang-format on

  // Reserved size
  constexpr size_t fixed_reserve_size = 800;
  std::string result;
  result.reserve(fixed_reserve_size);

  for (char c : node) {
    if (encode_map.count(c)) {
      result += encode_map.at(c);  // Append the encoded value
    } else {
      result += c;  // Append the character as-is
    }
  }

  return result;
}

// Check if a string contains any invalid characters
bool contains_invalid_chars(const std::string& str,
                            const std::unordered_set<char>& invalidChars) {
  return std::ranges::any_of(
      str, [&invalidChars](char c) { return invalidChars.contains(c); });
}

std::string handle_term_type(const std::string& term_type,
                             const std::string& rdf_term,
                             const std::string& lang_tag,
                             const std::string& data_type) {
  static const std::unordered_set<char> errorChars = {' ', '!', '"', '\'', '(',
                                                      ')', ',', '[', ']'};

  if (term_type == "iri") {
    // Check for invalid characters
    if (contains_invalid_chars(rdf_term, errorChars)) {
      std::string error_msg = "Error: invalid IRI detected for node: '" + rdf_term + "'. ";
      if (continue_on_error == true) {
        std::cout << error_msg << "Skipping!\n";
      }
      error_msg += "Stop!";
      throw std::runtime_error(error_msg);
    }
    return "<" + rdf_term + ">";
  } else if (term_type == "blanknode") {
    std::string rdf_term_clean = clean_blank_node(rdf_term);
    return "_:" + rdf_term_clean;

  } else if (term_type == "literal") {
    std::string literal = "\"" + rdf_term + "\"";

    // datatype is more important then langtag
    if (data_type != "None") {
      literal += "^^<" + data_type + ">";
    } else if (lang_tag != "None") {
      literal += "@" + lang_tag;
    }
    return literal;
  }

  // Return an empty string for unsupported term types
  std::string error_msg =
      "Error: unsupported term type. Valid term types are 'iri', "
      "'blanknode', 'literal'. Received: " +
      term_type;
  std::cout << error_msg << std::endl;
  throw std::runtime_error(error_msg);
}

std::string unmaskString(const std::string& input) {
  std::string output;
  output.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && (i + 1 < input.size()) &&
        (input[i + 1] == '{' || input[i + 1] == '}')) {
      // Skip the backslash and add the next character
      output += input[i + 1];
      ++i;  // Skip the next character since it is already added
    } else {
      // Add the current character to the output
      output += input[i];
    }
  }

  return output;
}

std::string create_operator(const std::string& term_map,
                            const std::string& term_map_type,
                            const std::string& term_type,
                            const std::string& lang_tag,
                            const std::string& data_type,
                            const std::string& base_uri,
                            std::unordered_map<std::string, std::string>& map) {
  std::string rdf_term = term_map;

  // Handle template
  if (term_map_type == "template") {
    // Find all matches in term_map
    std::vector<std::string> matches = extract_substrings(rdf_term);

    // Fill in template
    for (const auto& match : matches) {
      // Get data of row at match
      std::string data = map[match];
      // If IRI make data safes
      if (term_type == "iri") {
        data = make_safe_iri(data);
      }

      // Replace reference id with actual data
      rdf_term = replace_substring(rdf_term, "{" + match + "}", data);

      // unmask data, remove \\ in fromt of { or }
      rdf_term = unmaskString(rdf_term);
    }

    // Add base iri if needed
    if (term_type == "iri" && !(rdf_term.starts_with("http://") ||
                                rdf_term.starts_with("https://"))) {
      rdf_term = base_uri + rdf_term;
    }

    rdf_term = handle_term_type(term_type, rdf_term, lang_tag, data_type);

    return rdf_term;
  }
  // Handle reference
  else if (term_map_type == "reference") {
    std::string data = map[term_map];

    // Replace reference id with actual data
    rdf_term = replace_substring(rdf_term, term_map, data);

    // Add base iri if needed
    if (term_type == "iri" && !(rdf_term.starts_with("http://") ||
                                rdf_term.starts_with("https://"))) {
      rdf_term = base_uri + rdf_term;
    }

    rdf_term = handle_term_type(term_type, rdf_term, lang_tag, data_type);

    return rdf_term;
  } else if (term_map_type == "constant") {
    rdf_term = handle_term_type(term_type, rdf_term, lang_tag, data_type);
    return rdf_term;
  } else {
    std::cout << "Error: term map type not supported! Valid types are: "
                 "'template', 'reference', 'constant'. Received: "
              << term_map_type << term_map << std::endl;
    exit(1);
  }
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void handle_constant(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                     const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                     const fs::path& output_file_name) {
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cout << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }
  std::string subject, predicate, object, graph;
  subject = handle_term_type(s_content[2], s_content[0], "", "");
  predicate = handle_term_type(p_content[2], p_content[0], "", "");
  object = handle_term_type(o_content[2], o_content[0], o_content[3], o_content[4]);

  if (g_content.empty()) {
    outputFile << subject + " " + predicate + " " + object + " .\n";
  } else {
    graph = handle_term_type(g_content[2], g_content[0], "", "");
    outputFile << subject + " " + predicate + " " + object + " " + graph + " .\n";
  }
}

void handle_constant_preformatted(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                  const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                  const fs::path& output_file_name) {
  std::ofstream outputFile(output_file_name, std::ios::app);
  if (!outputFile) {
    std::cout << "Error: Unable to open file for writing." << std::endl;
    std::exit(1);
  }

  if (g_content.empty()) {
    outputFile << s_content[0] + " " + p_content[0] + " " + o_content[0] + " .\n";
  } else {
    outputFile << s_content[0] + " " + p_content[0] + " " + o_content[0] + " " + g_content[0] + " .\n";
  }
}

std::unordered_set<std::string> handle_constant_dependent(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                                          const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                                          const fs::path& output_file_name, std::unordered_set<std::string>& unique_triple) {
  std::string subject, predicate, object, graph;
  subject = handle_term_type(s_content[2], s_content[0], "", "");
  predicate = handle_term_type(p_content[2], p_content[0], "", "");
  object = handle_term_type(o_content[2], o_content[0], o_content[3], o_content[4]);

  if (g_content.empty()) {
    std::string res = subject + " " + predicate + " " + object + " .\n";
    unique_triple.insert(res);
    return unique_triple;
  } else {
    graph = handle_term_type(g_content[2], g_content[0], "", "");

    std::string res = subject + " " + predicate + " " + object + " " + graph + " .\n";
    unique_triple.insert(res);
    return unique_triple;
  }
}

std::unordered_set<std::string> handle_constant_preformatted_dependent(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                                                       const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                                                       const fs::path& output_file_name, std::unordered_set<std::string>& unique_triple) {
  if (g_content.empty()) {
    std::string res = s_content[0] + " " + p_content[0] + " " + o_content[0] + " .\n";
    unique_triple.insert(res);
    return unique_triple;
  } else {
    std::string res = s_content[0] + " " + p_content[0] + " " + o_content[0] + " " + g_content[0] + " .\n";
    unique_triple.insert(res);
    return unique_triple;
  }
}