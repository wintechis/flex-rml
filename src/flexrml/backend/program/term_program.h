#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "utils.h"

enum class CompiledTermMapType {
  Preformatted,
  Constant,
  Reference,
  Template
};

struct CompiledTemplatePart {
  std::string literal;
  int reference_index = -1;
};

struct CompiledTerm {
  bool usable = false;
  CompiledTermMapType map_type = CompiledTermMapType::Constant;
  std::string term_map;
  std::string term_type;
  std::string lang_tag = "None";
  std::string data_type = "None";
  std::vector<CompiledTemplatePart> parts;
  int reference_index = -1;
  bool infer_datatype = false;
  bool add_base_iri = false;
};

struct CompiledRuntimeTerm {
  std::vector<std::string> content;
  CompiledTerm compiled;
  enum class RenderOp {
    Null,
    Function,
    Preformatted,
    Compiled,
    Fallback
  };
  RenderOp render_op = RenderOp::Fallback;
};

int projected_column_index(const std::vector<std::string>& projected_header,
                           std::string_view name);

CompiledRuntimeTerm compile_runtime_term(const std::vector<std::string>& content,
                                         const std::vector<std::string>& projected_header,
                                         const std::string& base_uri);

bool term_needs_row_map(const std::vector<std::string>& content,
                        const CompiledTerm& compiled);

inline void render_compiled_term(const CompiledTerm& term,
                                 const std::vector<std::string_view>& row,
                                 const std::string& base_uri,
                                 std::string& out,
                                 std::string& scratch) {
  if (term.map_type == CompiledTermMapType::Preformatted) {
    out = term.term_map;
    return;
  }

  scratch.clear();
  std::string_view rdf_term = term.term_map;
  if (term.map_type == CompiledTermMapType::Reference) {
    rdf_term = row[term.reference_index];
  } else if (term.map_type == CompiledTermMapType::Template) {
    if (scratch.capacity() < term.term_map.size()) {
      scratch.reserve(term.term_map.size());
    }
    for (const CompiledTemplatePart& part : term.parts) {
      if (part.reference_index >= 0) {
        const std::string_view value = row[part.reference_index];
        if (term.term_type == "uri") {
          append_safe_iri(value, true, scratch);
        } else if (term.term_type == "iri") {
          append_safe_iri(value, false, scratch);
        } else {
          scratch += value;
        }
      } else {
        scratch += part.literal;
      }
    }
    rdf_term = scratch;
  }

  if ((term.map_type == CompiledTermMapType::Reference || term.map_type == CompiledTermMapType::Template) &&
      term.add_base_iri &&
      !(rdf_term.starts_with("http://") || rdf_term.starts_with("https://"))) {
    if (rdf_term.data() != scratch.data()) {
      scratch.assign(rdf_term);
    }
    scratch.insert(0, base_uri);
    rdf_term = scratch;
  }

  const std::string datatype =
      term.infer_datatype ? infer_literal_datatype(rdf_term, term.lang_tag, term.data_type) : term.data_type;
  handle_term_type_into(term.term_type, rdf_term, term.lang_tag, datatype, out);
}
