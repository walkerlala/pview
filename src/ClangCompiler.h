//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <memory>

#include "PView.h"

namespace pview {
/**
 * Construct a clang CompilerInstance for indexing / analyzing, using a single
 * CompileCommand from compile_commands.json
 *
 * @return nullptr if failed
 */
std::unique_ptr<clang::CompilerInstance> build_compiler_instance(
    const clang::tooling::CompileCommand &cmd);

/** Remove meaningless dot from filepath */
std::string normalize_path(llvm::StringRef path);

/** Translate clang::FileEntry into normal unix filepath */
std::string path_from_file_entry(const clang::FileEntry &file);
}  // namespace pview
