#include "termtype_helper.h"

/**
 * @brief Encodes a URL by replacing special characters with their respective percent-encoded values.
 *
 * @param node The input string to be URL-encoded.
 * @return std::string The URL-encoded version of the input string.
 */
std::string url_encode(const std::string& node) {
  std::string result = "";
  for (char c : node) {
    switch (c) {
      case ' ':
        result += "%20";
        break;
      case '!':
        result += "%21";
        break;
      case '\"':
        result += "%22";
        break;
      case '#':
        result += "%23";
        break;
      case '$':
        result += "%24";
        break;
      case '%':
        result += "%25";
        break;
      case '&':
        result += "%26";
        break;
      case '\'':
        result += "%27";
        break;
      case '(':
        result += "%28";
        break;
      case ')':
        result += "%29";
        break;
      case '*':
        result += "%2A";
        break;
      case '+':
        result += "%2B";
        break;
      case ',':
        result += "%2C";
        break;
      case '/':
        result += "%2F";
        break;
      case ':':
        result += "%3A";
        break;
      case ';':
        result += "%3B";
        break;
      case '<':
        result += "%3C";
        break;
      case '=':
        result += "%3D";
        break;
      case '>':
        result += "%3E";
        break;
      case '?':
        result += "%3F";
        break;
      case '@':
        result += "%40";
        break;
      case '[':
        result += "%5B";
        break;
      case '\\':
        result += "%5C";
        break;
      case ']':
        result += "%5D";
        break;
      case '{':
        result += "%7B";
        break;
      case '|':
        result += "%7C";
        break;
      case '}':
        result += "%7D";
        break;
      default:
        result += c;
        break;
    }
  }
  return result;
}

/**
 *@brief Handles the term mapping for IRIs by replacing all white space with %20 and encloses string in < >.
 *
 * @param node - The element to be converted into an IRI.
 *
 * @return std::string - The processed node.
 */
std::string handle_term_type_IRI(const std::string& node) {
  static const std::string error_chars = " !\"'(),[]";

  // Check if IRI is valid; if not, skip it
  if (node.find_first_of(error_chars) != std::string::npos) {
    log("Invalid IRI detected: ");
    log(node.c_str());
    logln(" - Skipped!");
    return "";
  }

  return "<" + node + ">";
}

/**
 *@brief Handles the term mapping for blank nodes by adding _: in front of string.
 *
 * @param node - The element to be converted into a blank node.
 *
 * @return std::string - The processed node.
 */
std::string handle_term_type_BlankNode(const std::string& node) {
  return "_:" + node;
}

/**
 *@brief Handles the term mapping for literals by enclosing the string in " ".
 *
 * @param node - The element to be converted into a literal.
 *
 * @return std::string - The processed node.
 */
std::string handle_term_type_Literal(const std::string& node) {
  // remove all '\' in string
  std::string cleanedNode;
  for (char c : node) {
    if (c != '\\') {
      cleanedNode += c;
    }
  }

  // Enclose in " "
  return "\"" + cleanedNode + "\"";
}

/**
 *@brief Handles the term mapping for a node based on its term type.
 *
 * @param term_type - The string containing the term type (e.g., IRI, BlankNode, Literal).
 * @param node - The element to be modified based on its term type.
 *
 * @return std::string - The processed node.
 */
std::string handle_term_type(const std::string& term_type, const std::string& node) {
  if (term_type == IRI_TERM_TYPE) {
    return handle_term_type_IRI(node);
  } else if (term_type == "http://www.w3.org/ns/r2rml#BlankNode") {
    return handle_term_type_BlankNode(node);
  } else if (term_type == LITERAL_TERM_TYPE) {
    return handle_term_type_Literal(node);
  } else {
    throw_error("Error occured handling term type! - Unknown term type found.");
    return "";
  }
}