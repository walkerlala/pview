//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include <string>
#include <vector>

#include "StrUtils.h"

namespace pview {
bool string_starts_with(const std::string &str, const std::string &pattern) {
  if (str.size() < pattern.size()) {
    return false;
  }
  auto str_begin = str.begin();
  auto p_begin = pattern.begin();
  auto p_end = pattern.end();
  for (; p_begin < p_end; p_begin++, str_begin++) {
    if (*p_begin != *str_begin) {
      return false;
    }
  }
  return true;
}

bool string_ends_with(const std::string &str, const std::string &pattern) {
  if (str.size() < pattern.size()) {
    return false;
  }
  auto str_begin = str.rbegin();
  auto p_begin = pattern.rbegin();
  auto p_end = pattern.rend();
  for (; p_begin < p_end; p_begin++, str_begin++) {
    if (*p_begin != *str_begin) {
      return false;
    }
  }
  return true;
}

template <typename T>
static std::vector<std::string> split_string_impl(const std::string &str,
                                                  const T &delimiter) {
  std::vector<std::string> strings;

  std::string::size_type pos = 0;
  std::string::size_type prev = 0;
  while ((pos = str.find(delimiter, prev)) != std::string::npos) {
    strings.push_back(str.substr(prev, pos - prev));
    prev = pos + 1;
  }

  std::string last = str.substr(prev);
  if (last.size()) {
    strings.push_back(last);
  }

  return strings;
}

std::vector<std::string> split_string(const std::string &str, char delimiter) {
  return split_string_impl(str, delimiter);
}

std::vector<std::string> split_string(const std::string &str,
                                      const std::string &delimiter) {
  return split_string_impl(str, delimiter);
}

std::string replace_string(std::string target, const std::string &search,
                           const std::string &replace) {
  size_t pos = 0;
  while ((pos = target.find(search, pos)) != std::string::npos) {
    target.replace(pos, search.length(), replace);
    pos += replace.length();
  }
  return target;
}
}  // namespace pview
