#include "utils.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

bool is_default_graph_marker(const std::string& graph) {
  return graph == "http://w3id.org/rml/defaultGraph" || graph == "<http://w3id.org/rml/defaultGraph>";
}

std::string format_statement(const std::string& subject, const std::string& predicate, const std::string& object) {
  return subject + " " + predicate + " " + object + " .\n";
}

std::string format_statement(const std::string& subject, const std::string& predicate, const std::string& object, const std::string& graph) {
  if (graph.empty() || is_default_graph_marker(graph)) {
    return format_statement(subject, predicate, object);
  }
  return subject + " " + predicate + " " + object + " " + graph + " .\n";
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////// RML Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::unordered_map<std::string, std::string> uri_map;

std::vector<std::string> extract_substrings(const std::string& str);

static std::string transform_string(const std::string& input) {
  static constexpr std::string_view digit_map[10] = {
      "A01f", "B08f", "C997", "D745", "EEAF",
      "FFNb", "GGbf", "HHQf", "IIDv", "JJ9W"};

  std::string result;
  result.reserve(input.size() * 2);

  for (unsigned char ch : input) {
    if (ch >= 'a' && ch <= 'z') {
      result += static_cast<char>((ch - 'a' + 1) % 26 + 'a');
    } else if (ch >= 'A' && ch <= 'Z') {
      result += static_cast<char>((ch - 'A' + 1) % 26 + 'A');
    } else if (ch >= '0' && ch <= '9') {
      result += digit_map[ch - '0'];
    } else {
      result += '_';
    }
  }

  return result;
}

std::string get_local_now_iso8601() {
  using namespace std::chrono;
  using centiseconds = duration<long long, std::centi>;
  auto now = floor<centiseconds>(system_clock::now());
  zoned_time<centiseconds> zt{current_zone(), now};

  return std::format("{:%FT%T%Ez}", zt);
}

std::string get_current_date_time_string() {
  using namespace std::chrono;

  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

  const std::time_t tt = system_clock::to_time_t(now);
  std::tm tm{};

#ifdef _WIN32
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%d%H%M%S")
      << std::setw(3) << std::setfill('0') << ms.count();

  return oss.str();
}

std::string generate_random_string(std::size_t length) {
  static const std::string chars =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dist(0, chars.size() - 1);

  std::string result;
  result.reserve(length);

  for (std::size_t i = 0; i < length; ++i) {
    result += chars[dist(gen)];
  }

  return result;
}

std::string resolve_function_input(const std::vector<std::string>& commands,
                                   std::size_t input_index,
                                   std::unordered_map<std::string, std::string>& row) {
  const std::size_t offset = 1 + input_index * 3;
  if (offset + 2 >= commands.size()) {
    return "";
  }

  const std::string& input_kind = commands[offset + 1];
  const std::string& input_value = commands[offset + 2];

  if (input_kind == "constant") {
    return input_value;
  }

  if (input_kind == "reference") {
    auto it = row.find(input_value);
    if (it == row.end()) {
      std::cout << "Error: Function input reference not found: '" << input_value << "'" << std::endl;
      exit(1);
    }
    return it->second;
  }

  if (input_kind == "template") {
    std::string resolved = input_value;
    std::vector<std::string> matches = extract_substrings(resolved);
    for (const auto& match : matches) {
      auto it = row.find(match);
      if (it == row.end()) {
        std::cout << "Error: Function input template reference not found: '" << match << "'" << std::endl;
        exit(1);
      }
      resolved = replace_substring(resolved, "{" + match + "}", it->second);
    }
    return resolved;
  }

  if (input_kind == "function") {
    std::string nested_function = input_value;
    size_t pos = 0;
    while ((pos = nested_function.find("%3B%3B", pos)) != std::string::npos) {
      nested_function.replace(pos, 6, ";;");
      pos += 2;
    }
    return handle_function_call(nested_function, 0, "", row);
  }

  std::cout << "Error: Unsupported function input kind: '" << input_kind << "'" << std::endl;
  exit(1);
}

std::string handle_function_call(std::string function_signature,
                                 int line_count,
                                 std::string realation_name,
                                 std::unordered_map<std::string, std::string>& row) {
  // 1. Split command
  const std::vector<std::string> commands = split_by_substring(function_signature, ";;");
  const std::string function_type = commands[0];

  if (function_type == "==FUNC==DATE_NOW") {
    // Handle date time function call.
    const std::string time = get_local_now_iso8601();
    return time;
  } else if (function_type == "==FUNC==RANDOM") {
    constexpr const std::size_t length = 40;
    std::string random_string = generate_random_string(length);
    return random_string;
  } else if (function_type == "==FUNC==ALWAYS_RETURNS_ABC") {
    return "ABC";
  } else if (function_type == "==FUNC==EQUAL") {
    return resolve_function_input(commands, 0, row) == resolve_function_input(commands, 1, row) ? "true" : "false";
  } else if (function_type == "==FUNC==TO_UPPER_CASE") {
    std::string value = resolve_function_input(commands, 0, row);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });
    return value;
  } else if (function_type == "==FUNC==TO_UPPER_CASE_URL") {
    std::string value = resolve_function_input(commands, 0, row);
    const bool has_scheme = value.starts_with("http://") || value.starts_with("https://") ||
                            value.starts_with("HTTP://") || value.starts_with("HTTPS://");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });
    if (!has_scheme) {
      value = "http://" + value;
    }
    return value;
  } else if (function_type == "==FUNC==STRING_LENGTH") {
    const std::string value = resolve_function_input(commands, 0, row);
    return std::to_string(value.size());
  } else if (function_type == "==FUNC==STRING_SUBSTRING") {
    const std::string value = resolve_function_input(commands, 0, row);
    const std::string start_value = resolve_function_input(commands, 1, row);
    std::size_t start = 0;
    try {
      start = static_cast<std::size_t>(std::stoll(start_value));
    } catch (const std::exception&) {
      std::cout << "Error: Invalid substring start index: '" << start_value << "'" << std::endl;
      exit(1);
    }
    if (start > value.size()) {
      return "NULL";
    }
    if (start == value.size()) {
      return "";
    }
    return value.substr(start);
  } else if (function_type == "==FUNC==STRING_REPLACE") {
    std::string value = resolve_function_input(commands, 0, row);
    const std::string find_value = resolve_function_input(commands, 1, row);
    const std::string replace_value = resolve_function_input(commands, 2, row);
    if (find_value.empty()) {
      return value;
    }
    size_t pos = 0;
    while ((pos = value.find(find_value, pos)) != std::string::npos) {
      value.replace(pos, find_value.length(), replace_value);
      pos += replace_value.length();
    }
    return value;
  } else if (function_type == "==FUNC==ESCAPE") {
    const std::string value = resolve_function_input(commands, 0, row);
    const std::string mode = resolve_function_input(commands, 1, row);
    if (mode != "html") {
      std::cout << "Error: Unsupported escape mode: '" << mode << "'" << std::endl;
      exit(1);
    }

    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
      if (c == '&') {
        escaped += "&amp;";
      } else if (c == '<') {
        escaped += "&lt;";
      } else if (c == '>') {
        escaped += "&gt;";
      } else if (c == '"') {
        escaped += "&quot;";
      } else if (c == '\'') {
        escaped += "&#39;";
      } else {
        escaped.push_back(c);
      }
    }
    return escaped;
  } else if (function_type == "==FUNC==GENERATE_IRI") {
    // Assume only one input
    const std::string base_iri = resolve_function_input(commands, 0, row);

    std::string to_add = transform_string(std::to_string(line_count) + realation_name);

    const std::string generated_uri = base_iri + to_add;

    return generated_uri;
  } else {
    // No funciton found
    std::cout << "Error: Requested function not found. Got: '" << function_signature << "'" << std::endl;
    exit(1);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////// CREATE Function //////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<std::string> extract_substrings(const std::string& str) {
  std::vector<std::string> substrings;
  substrings.reserve(5);

  size_t startPos = 0;

  while ((startPos = str.find('{', startPos)) != std::string::npos) {
    if (startPos > 0 && str[startPos - 1] == '\\') {
      startPos++;
      continue;
    }

    size_t endPos = startPos + 1;
    while (endPos < str.size()) {
      if (str[endPos] == '}' && !(endPos > 0 && str[endPos - 1] == '\\')) {
        std::string extracted = str.substr(startPos + 1, endPos - startPos - 1);
        std::string unescaped;
        unescaped.reserve(extracted.size());
        for (size_t i = 0; i < extracted.size(); ++i) {
          if (extracted[i] == '\\' && i + 1 < extracted.size() &&
              (extracted[i + 1] == '{' || extracted[i + 1] == '}')) {
            continue;
          }
          unescaped.push_back(extracted[i]);
        }
        substrings.emplace_back(std::move(unescaped));
        startPos = endPos + 1;
        break;
      }
      endPos++;
    }

    if (endPos >= str.size()) {
      break;
    }
  }
  return substrings;
}

std::string make_safe_iri(const std::string& node, bool encode_non_ascii = true) {
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

  static constexpr char hex_digits[] = "0123456789ABCDEF";

  for (unsigned char c : node) {
    if (encode_map.count(c)) {
      result += encode_map.at(c);  // Append the encoded value
    } else if (encode_non_ascii && c > 127) {
      result += '%';
      result += hex_digits[(c >> 4) & 0x0F];
      result += hex_digits[c & 0x0F];
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

  if (term_type == "uri" || term_type == "iri" || term_type == "unsafeiri") {
    // Check for invalid characters
    if ((term_type == "uri" || term_type == "iri") && contains_invalid_chars(rdf_term, errorChars)) {
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
      "Error: unsupported term type. Valid term types are 'uri', 'iri', "
      "'unsafeiri', 'blanknode', 'literal'. Received: " +
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

std::string resolve_annotation_value(const std::string& annotation,
                                     std::unordered_map<std::string, std::string>& map) {
  if (annotation == "None") {
    return annotation;
  }

  std::string resolved = annotation;
  std::vector<std::string> matches = extract_substrings(resolved);
  if (!matches.empty()) {
    for (const auto& match : matches) {
      resolved = replace_substring(resolved, "{" + match + "}", map[match]);
    }
    return unmaskString(resolved);
  }

  auto it = map.find(annotation);
  if (it != map.end()) {
    return it->second;
  }

  return annotation;
}

std::string escape_braces(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() * 2);

  for (char c : value) {
    if (c == '{' || c == '}') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }

  return escaped;
}

std::string infer_literal_datatype(const std::string& rdf_term,
                                   const std::string& lang_tag,
                                   const std::string& data_type) {
  if (lang_tag != "None" || data_type != "None") {
    return data_type;
  }

  static const std::regex integer_pattern(R"(^-?(0|[1-9][0-9]*)$)");
  static const std::regex decimal_pattern(R"(^-?(0|[1-9][0-9]*)\.[0-9]+$)");

  if (rdf_term == "true" || rdf_term == "false") {
    return "http://www.w3.org/2001/XMLSchema#boolean";
  }
  if (std::regex_match(rdf_term, integer_pattern)) {
    return "http://www.w3.org/2001/XMLSchema#integer";
  }
  if (std::regex_match(rdf_term, decimal_pattern)) {
    return "http://www.w3.org/2001/XMLSchema#decimal";
  }

  return data_type;
}

std::string create_operator(const std::string& term_map,
                            const std::string& term_map_type,
                            const std::string& term_type,
                            const std::string& lang_tag,
                            const std::string& data_type,
                            const std::string& base_uri,
                            std::unordered_map<std::string, std::string>& map) {
  std::string rdf_term = term_map;

  auto normalize_iri_annotation = [&base_uri](std::string annotation_value) {
    if (annotation_value == "None") {
      return annotation_value;
    }
    if (!(annotation_value.starts_with("http://") || annotation_value.starts_with("https://"))) {
      annotation_value = base_uri + annotation_value;
    }
    return annotation_value;
  };

  // Handle template
  if (term_map_type == "template") {
    std::string effective_lang_tag = resolve_annotation_value(lang_tag, map);
    std::string effective_data_type = resolve_annotation_value(data_type, map);
    effective_data_type = normalize_iri_annotation(effective_data_type);

    // Find all matches in term_map
    std::vector<std::string> matches = extract_substrings(rdf_term);

    // Fill in template
    for (const auto& match : matches) {
      // Get data of row at match
      std::string data = map[match];
      // If IRI make data safes
      if (term_type == "uri") {
        data = make_safe_iri(data, true);
      } else if (term_type == "iri") {
        data = make_safe_iri(data, false);
      }

      // Replace reference id with actual data
      std::string placeholder = "{" + match + "}";
      rdf_term = replace_substring(rdf_term, placeholder, data);
      if (rdf_term.find(placeholder) == std::string::npos) {
        std::string escaped_placeholder = "{" + escape_braces(match) + "}";
        rdf_term = replace_substring(rdf_term, escaped_placeholder, data);
      }

      // unmask data, remove \\ in fromt of { or }
      rdf_term = unmaskString(rdf_term);
    }

    // Add base iri if needed
    if ((term_type == "uri" || term_type == "iri" || term_type == "unsafeiri") && !(rdf_term.starts_with("http://") || rdf_term.starts_with("https://"))) {
      rdf_term = base_uri + rdf_term;
    }

    rdf_term = handle_term_type(term_type, rdf_term, effective_lang_tag, effective_data_type);

    return rdf_term;
  }
  // Handle reference
  else if (term_map_type == "reference") {
    std::string data = map[term_map];
    std::string effective_lang_tag = resolve_annotation_value(lang_tag, map);
    std::string effective_data_type = resolve_annotation_value(data_type, map);
    effective_data_type = normalize_iri_annotation(effective_data_type);

    if (term_type == "literal") {
      effective_data_type = infer_literal_datatype(data, effective_lang_tag, effective_data_type);
    }

    // Replace reference id with actual data
    rdf_term = replace_substring(rdf_term, term_map, data);

    // Add base iri if needed
    if ((term_type == "uri" || term_type == "iri" || term_type == "unsafeiri") && !(rdf_term.starts_with("http://") || rdf_term.starts_with("https://"))) {
      rdf_term = base_uri + rdf_term;
    }

    rdf_term = handle_term_type(term_type, rdf_term, effective_lang_tag, effective_data_type);

    return rdf_term;
  } else if (term_map_type == "constant") {
    std::string effective_lang_tag = resolve_annotation_value(lang_tag, map);
    std::string effective_data_type = resolve_annotation_value(data_type, map);
    effective_data_type = normalize_iri_annotation(effective_data_type);
    if (term_type == "literal") {
      effective_data_type = infer_literal_datatype(rdf_term, effective_lang_tag, effective_data_type);
    }
    rdf_term = handle_term_type(term_type, rdf_term, effective_lang_tag, effective_data_type);
    return rdf_term;
  } else {
    std::cout << "Error: term map type not supported! Valid types are: 'template', 'reference', 'constant'. Received term map type: '" << term_map_type << "'" << " and term map '" << term_map << "'" << std::endl;
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
    outputFile << format_statement(subject, predicate, object);
  } else {
    graph = handle_term_type(g_content[2], g_content[0], "", "");
    outputFile << format_statement(subject, predicate, object, graph);
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
    outputFile << format_statement(s_content[0], p_content[0], o_content[0]);
  } else {
    outputFile << format_statement(s_content[0], p_content[0], o_content[0], g_content[0]);
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
    std::string res = format_statement(subject, predicate, object);
    unique_triple.insert(res);
    return unique_triple;
  } else {
    graph = handle_term_type(g_content[2], g_content[0], "", "");

    std::string res = format_statement(subject, predicate, object, graph);
    unique_triple.insert(res);
    return unique_triple;
  }
}

std::unordered_set<std::string> handle_constant_preformatted_dependent(const std::vector<std::string>& s_content, const std::vector<std::string>& p_content,
                                                                       const std::vector<std::string>& o_content, const std::vector<std::string>& g_content,
                                                                       const fs::path& output_file_name, std::unordered_set<std::string>& unique_triple) {
  if (g_content.empty()) {
    std::string res = format_statement(s_content[0], p_content[0], o_content[0]);
    unique_triple.insert(res);
    return unique_triple;
  } else {
    std::string res = format_statement(s_content[0], p_content[0], o_content[0], g_content[0]);
    unique_triple.insert(res);
    return unique_triple;
  }
}
