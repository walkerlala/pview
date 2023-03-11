//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <string>
#include <vector>

namespace pview {
bool string_starts_with(const std::string &str, const std::string &pattern);

bool string_ends_with(const std::string &str, const std::string &pattern);

std::vector<std::string> split_string(const std::string &str, char delimiter);
std::vector<std::string> split_string(const std::string &str,
                                      const std::string &delimiter);
}  // namespace pview
