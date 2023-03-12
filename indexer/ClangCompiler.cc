//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include "ClangCompiler.h"
#include "FileUtils.h"

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

using clang::ASTConsumer;
using clang::CompilerInstance;
using clang::CompilerInvocation;
using clang::DiagnosticOptions;
using clang::DiagnosticsEngine;
using clang::FileEntry;
using clang::FileSystemOptions;
using clang::IgnoringDiagConsumer;
using clang::IntrusiveRefCntPtr;
using clang::MultiplexConsumer;
using clang::PCHContainerOperations;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::TargetInfo;
using clang::index::IndexingOptions;
using clang::tooling::CompileCommand;

namespace pview {
/**
 * Generate a clang::CompilerInvocation, which could be used to invoke
 * the clang compiler to index a single file.
 *
 * @param args    Compiler parameters from compile_commands.json
 * @returns nullptr if failed
 */
static shared_ptr<CompilerInvocation> build_compiler_invocation(
    const CompileCommand &cmd) {
  const vector<string> &cmd_line = cmd.CommandLine;
  vector<const char *> cmd_line_char;
  for (const auto &cmd_str : cmd_line) {
    cmd_line_char.push_back(cmd_str.c_str());
  }
  llvm::ArrayRef<const char *> args(cmd_line_char);

  IntrusiveRefCntPtr<DiagnosticsEngine> diags(
      CompilerInstance::createDiagnostics(new DiagnosticOptions,
                                          new IgnoringDiagConsumer, true));

  clang::driver::Driver driver(args[0], llvm::sys::getDefaultTargetTriple(),
                               *diags, "whatever title",
                               llvm::vfs::getRealFileSystem());
  driver.setCheckInputsExist(false);
  unique_ptr<clang::driver::Compilation> comp(driver.BuildCompilation(args));
  if (!comp) {
    LOG(ERROR) << "compilation not built";
    return nullptr;
  }
  const clang::driver::JobList &jobs = comp->getJobs();
  if (jobs.size() != 1 || !clang::isa<clang::driver::Command>(*jobs.begin())) {
    LOG(ERROR) << "more than 1 job in a single compilation";
    return nullptr;
  }

  const auto &command = clang::cast<clang::driver::Command>(*jobs.begin());
  const auto &creator_name = command.getCreator().getName();
  if (creator_name != string{"clang"} && creator_name != string{"clang++"}) {
    LOG(ERROR) << "compilation creator not clang/clang++: " << creator_name;
    return nullptr;
  }
  const llvm::opt::ArgStringList &cc_args = command.getArguments();
  auto ci = make_shared<CompilerInvocation>();
  if (!CompilerInvocation::CreateFromArgs(*ci, cc_args, *diags)) {
    LOG(ERROR) << "failed to creator compiler invocation from args";
    return nullptr;
  }

  ci->getDiagnosticOpts().IgnoreWarnings = true;
  ci->getFrontendOpts().DisableFree = false;
  return ci;
}

/**
 * Construct a clang CompilerInstance for indexing / analyzing, using a single
 * CompileCommand from compile_commands.json
 *
 * @return nullptr if failed
 */
unique_ptr<CompilerInstance> build_compiler_instance(
    const CompileCommand &cmd) {
  auto ci = build_compiler_invocation(cmd);
  if (!ci) {
    LOG(ERROR) << "failed to build CompilerInvocation for " << cmd.Filename;
    return nullptr;
  }

  auto inst =
      make_unique<CompilerInstance>(make_shared<PCHContainerOperations>());
  inst->setInvocation(ci);
  inst->createDiagnostics(new IgnoringDiagConsumer(), /*take ownership*/ true);
  inst->getDiagnostics().setIgnoreAllWarnings(true);
  inst->setTarget(TargetInfo::CreateTargetInfo(
      inst->getDiagnostics(), inst->getInvocation().TargetOpts));
  if (!inst->hasTarget()) {
    LOG(ERROR) << "File does not has target: " << cmd.Filename;
    return nullptr;
  }
  inst->createFileManager(llvm::vfs::getRealFileSystem());
  SourceManager *source_mgr =
      new SourceManager(inst->getDiagnostics(), inst->getFileManager(), true);
  inst->setSourceManager(source_mgr);
  return inst;
}

/** Remove meaningless dot from filepath */
string normalize_path(llvm::StringRef path) {
  llvm::SmallString<256> p(path);
  llvm::sys::path::remove_dots(p, true);
  return {p.data(), p.size()};
}

/** Translate clang::FileEntry into normal unix filepath */
string path_from_file_entry(const clang::FileEntry &file) {
  string ret;
  if (file.getName().startswith("/../")) {
    // Resolve symlinks outside of working folders. This handles leading path
    // components, e.g. (/lib -> /usr/lib) in
    // /../lib/gcc/x86_64-linux-gnu/10/../../../../include/c++/10/utility
    ret = file.tryGetRealPathName();
  } else {
    // If getName() refers to a file within a workspace folder, we prefer it
    // (which may be a symlink).
    ret = normalize_path(file.getName());
  }
  ret = real_path(ret);
  return ret;
}
}  // namespace pview
