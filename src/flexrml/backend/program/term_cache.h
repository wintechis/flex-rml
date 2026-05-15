#pragma once

#include <string>
#include <string_view>
#include <vector>

struct TermCacheEntry {
  std::string key;
  std::string value;
  std::string scratch;
  bool computed = false;
};

inline void reset_term_cache(std::vector<TermCacheEntry>& entries) {
  for (TermCacheEntry& entry : entries) {
    entry.computed = false;
  }
}

inline void append_term_cache_key_part(std::string& key, std::string_view value) {
  key += std::to_string(value.size());
  key.push_back(':');
  key.append(value);
  key.push_back('|');
}

template <typename RuntimeTerm>
std::string term_cache_key(const std::string& base_uri, const RuntimeTerm& term) {
  std::string key;
  key.reserve(base_uri.size() + term.content.size() * 24 + 32);
  append_term_cache_key_part(key, base_uri);
  for (const std::string& part : term.content) {
    append_term_cache_key_part(key, part);
  }
  return key;
}
