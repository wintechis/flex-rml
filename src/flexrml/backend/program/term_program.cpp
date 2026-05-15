#include "term_program.h"

#include "utils.h"

int projected_column_index(const std::vector<std::string>& projected_header,
                           std::string_view name) {
  for (std::size_t i = 0; i < projected_header.size(); ++i) {
    if (projected_header[i] == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static bool is_dynamic_annotation(const std::string& annotation,
                                  const std::vector<std::string>& projected_header) {
  return annotation.starts_with("==FUNC==") ||
         annotation.find('{') != std::string::npos ||
         projected_column_index(projected_header, annotation) >= 0;
}

static bool compile_template_parts(const std::string& term_map,
                                   const std::vector<std::string>& projected_header,
                                   std::vector<CompiledTemplatePart>& parts) {
  std::string literal;
  literal.reserve(term_map.size());

  for (std::size_t i = 0; i < term_map.size();) {
    if (term_map[i] == '\\' && i + 1 < term_map.size() &&
        (term_map[i + 1] == '{' || term_map[i + 1] == '}')) {
      literal.push_back(term_map[i + 1]);
      i += 2;
      continue;
    }

    if (term_map[i] != '{') {
      literal.push_back(term_map[i]);
      ++i;
      continue;
    }

    const std::size_t placeholder_start = i;
    ++i;
    std::string reference_name;
    bool closed = false;
    for (; i < term_map.size(); ++i) {
      if (term_map[i] == '\\' && i + 1 < term_map.size() &&
          (term_map[i + 1] == '{' || term_map[i + 1] == '}')) {
        reference_name.push_back(term_map[i + 1]);
        ++i;
      } else if (term_map[i] == '}') {
        closed = true;
        ++i;
        break;
      } else {
        reference_name.push_back(term_map[i]);
      }
    }

    if (!closed) {
      literal.append(term_map, placeholder_start, std::string::npos);
      break;
    }

    if (!literal.empty()) {
      parts.push_back(CompiledTemplatePart{std::move(literal), -1});
      literal.clear();
    }

    const int index = projected_column_index(projected_header, reference_name);
    if (index < 0) {
      return false;
    }
    parts.push_back(CompiledTemplatePart{"", index});
  }

  if (!literal.empty()) {
    parts.push_back(CompiledTemplatePart{std::move(literal), -1});
  }
  return true;
}

static CompiledTerm compile_term(const std::vector<std::string>& content,
                                 const std::vector<std::string>& projected_header,
                                 const std::string& base_uri) {
  CompiledTerm term;
  if (content.size() < 3 || content[1] == "function") {
    return term;
  }

  term.term_map = content[0];
  term.term_type = content[2];
  const bool is_literal = term.term_type == "literal";
  if (is_literal) {
    if (content.size() > 3) {
      term.lang_tag = content[3];
    }
    if (content.size() > 4) {
      term.data_type = content[4];
    }
    if (is_dynamic_annotation(term.lang_tag, projected_header) ||
        is_dynamic_annotation(term.data_type, projected_header)) {
      return term;
    }
    if (term.data_type != "None" &&
        !(term.data_type.starts_with("http://") || term.data_type.starts_with("https://"))) {
      term.data_type = base_uri + term.data_type;
    }
  }

  term.add_base_iri = term.term_type == "uri" || term.term_type == "iri" || term.term_type == "unsafeiri";

  if (content[1] == "preformatted") {
    term.map_type = CompiledTermMapType::Preformatted;
  } else if (content[1] == "constant") {
    term.map_type = CompiledTermMapType::Constant;
    term.infer_datatype = is_literal && term.lang_tag == "None" && term.data_type == "None";
  } else if (content[1] == "reference") {
    term.map_type = CompiledTermMapType::Reference;
    term.reference_index = projected_column_index(projected_header, term.term_map);
    if (term.reference_index < 0) {
      return CompiledTerm{};
    }
    term.infer_datatype = is_literal && term.lang_tag == "None" && term.data_type == "None";
  } else if (content[1] == "template") {
    term.map_type = CompiledTermMapType::Template;
    if (!compile_template_parts(term.term_map, projected_header, term.parts)) {
      return CompiledTerm{};
    }
  } else {
    return term;
  }

  term.usable = true;
  return term;
}

bool term_needs_row_map(const std::vector<std::string>& content,
                        const CompiledTerm& compiled) {
  return content.size() > 1 && (content[1] == "function" || !compiled.usable);
}

CompiledRuntimeTerm compile_runtime_term(const std::vector<std::string>& content,
                                         const std::vector<std::string>& projected_header,
                                         const std::string& base_uri) {
  CompiledRuntimeTerm term;
  term.content = content;
  term.compiled = compile_term(term.content, projected_header, base_uri);
  if (term.content.empty() || term.content[0] == "NULL") {
    term.render_op = CompiledRuntimeTerm::RenderOp::Null;
  } else if (term.content.size() > 1 && term.content[1] == "function") {
    term.render_op = CompiledRuntimeTerm::RenderOp::Function;
  } else if (term.content.size() > 1 && term.content[1] == "preformatted") {
    term.render_op = CompiledRuntimeTerm::RenderOp::Preformatted;
  } else if (term.compiled.usable) {
    term.render_op = CompiledRuntimeTerm::RenderOp::Compiled;
  } else {
    term.render_op = CompiledRuntimeTerm::RenderOp::Fallback;
  }
  return term;
}
