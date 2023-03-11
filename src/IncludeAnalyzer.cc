//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include <atomic>
#include <memory>
#include <vector>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Driver/Action.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Tool.h>
#include <clang/Format/Format.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/MultiplexConsumer.h>
#include <clang/Index/IndexDataConsumer.h>
#include <clang/Index/IndexingAction.h>
#include <clang/Index/IndexingOptions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/Core/Replacement.h>
#include <llvm/ADT/CachedHashString.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/CrashRecoveryContext.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "ClangCompiler.h"
#include "Common.h"
#include "FileUtils.h"
#include "IncludeAnalyzer.h"
#include "PView.h"
#include "StrUtils.h"
#include "Timer.h"

using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

using clang::ASTConsumer;
using clang::CompilerInstance;
using clang::CompilerInvocation;
using clang::Decl;
using clang::DeclContext;
using clang::DiagnosticOptions;
using clang::DiagnosticsEngine;
using clang::FileEntry;
using clang::FileSystemOptions;
using clang::IgnoringDiagConsumer;
using clang::IntrusiveRefCntPtr;
using clang::MacroArgs;
using clang::MacroDefinition;
using clang::MacroDirective;
using clang::MultiplexConsumer;
using clang::NamespaceDecl;
using clang::PCHContainerOperations;
using clang::Preprocessor;
using clang::PreprocessorOptions;
using clang::PrintingPolicy;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::TargetInfo;
using clang::Token;
using clang::index::IndexingOptions;
using clang::index::SymbolRelation;
using clang::index::SymbolRole;
using clang::index::SymbolRoleSet;
using clang::tooling::CompilationDatabase;
using clang::tooling::CompileCommand;

DECLARE_int64(verbose);

namespace pview {
/******************************************************************************
 * IncludeAnalyzer
 *
 ******************************************************************************/
bool IncludeAnalyzer::check_need_reindex() {
  if (!check_file_exists(filepath_)) {
    LOG(ERROR) << "File does not exists in filesystem: " << filepath_
               << ". Mark as NOT need to reindex";
    return false;
  }

  int64_t cur_file_mtime = get_file_mtime(filepath_);
  update_included_file_mtime(cur_file_mtime);

  /**
   * Check modification of @filepath itself, and then check all its include
   * files
   *
   * Note that @last_mtime_ could be inited to 0, which means that we haven seen
   * this file before, and check_need_reindex() should immediately return true.
   */
  if (last_mtime_ <= 0) {
    return true;
  }

  if (cur_file_mtime > last_mtime_) {
    return true;
  }

  Timer timer;

  unique_ptr<CompilerInstance> inst = build_compiler_instance(cmd_);
  if (!inst) {
    LOG(ERROR) << "Failed to build CompilerInstance for file " << cmd_.Filename;
    return true;
  }
  this->update_source_mgr(&(inst->getSourceManager()));

  auto action = make_unique<IAFrontendAction>(this);

  std::string err_msg;
  bool ok = false;
  {
    llvm::CrashRecoveryContext crc;
    auto parse = [&]() {
      if (!action->BeginSourceFile(*inst, inst->getFrontendOpts().Inputs[0]))
        return;
      if (llvm::Error e = action->Execute()) {
        err_msg = llvm::toString(std::move(e));
        return;
      }
      action->EndSourceFile();
      ok = true;
    };
    if (!crc.RunSafely(parse)) {
      LOG(ERROR) << "clang crash when anaylyzing " << cmd_.Filename;
      return true;
    }
  }

  if (!ok) {
    LOG(ERROR) << "failed to analyze included files for " << cmd_.Filename
               << ", err: " << err_msg;
    return true;
  }

  LOG_IF(INFO, FLAGS_verbose)
      << "Done analyze included files in "
      << timer.DurationMicroseconds() / 1000 << " ms: " << cmd_.Filename;

  return (newest_mtime_ > last_mtime_);
}
/*********************************************
 * IAPPCallBack
 *
 *********************************************/
/// Callback invoked whenever a source file is entered or exited.
///
/// \param Loc Indicates the new location.
/// \param PrevFID the file that was exited if \p Reason is ExitFile.
void IAPPCallBack::FileChanged(clang::SourceLocation Loc,
                               clang::PPCallbacks::FileChangeReason Reason,
                               clang::SrcMgr::CharacteristicKind FileType,
                               clang::FileID PrevFID) {}

/// Callback invoked whenever a source file is skipped as the result
/// of header guard optimization.
///
/// \param SkippedFile The file that is skipped instead of entering \#include
///
/// \param FilenameTok The file name token in \#include "FileName" directive
/// or macro expanded file name token from \#include MACRO(PARAMS) directive.
/// Note that FilenameTok contains corresponding quotes/angles symbols.
void IAPPCallBack::FileSkipped(const clang::FileEntryRef &SkippedFile,
                               const clang::Token &FilenameTok,
                               clang::SrcMgr::CharacteristicKind FileType) {}

/// Callback invoked whenever an inclusion directive of
/// any kind (\c \#include, \c \#import, etc.) has been processed, regardless
/// of whether the inclusion will actually result in an inclusion.
///
/// \param HashLoc The location of the '#' that starts the inclusion
/// directive.
///
/// \param IncludeTok The token that indicates the kind of inclusion
/// directive, e.g., 'include' or 'import'.
///
/// \param FileName The name of the file being included, as written in the
/// source code.
///
/// \param IsAngled Whether the file name was enclosed in angle brackets;
/// otherwise, it was enclosed in quotes.
///
/// \param FilenameRange The character range of the quotes or angle brackets
/// for the written file name.
///
/// \param File The actual file that may be included by this inclusion
/// directive.
///
/// \param SearchPath Contains the search path which was used to find the file
/// in the file system. If the file was found via an absolute include path,
/// SearchPath will be empty. For framework includes, the SearchPath and
/// RelativePath will be split up. For example, if an include of "Some/Some.h"
/// is found via the framework path
/// "path/to/Frameworks/Some.framework/Headers/Some.h", SearchPath will be
/// "path/to/Frameworks/Some.framework/Headers" and RelativePath will be
/// "Some.h".
///
/// \param RelativePath The path relative to SearchPath, at which the include
/// file was found. This is equal to FileName except for framework includes.
///
/// \param Imported The module, whenever an inclusion directive was
/// automatically turned into a module import or null otherwise.
///
/// \param FileType The characteristic kind, indicates whether a file or
/// directory holds normal user code, system code, or system code which is
/// implicitly 'extern "C"' in C++ mode.
void IAPPCallBack::InclusionDirective(
    clang::SourceLocation HashLoc, const clang::Token &IncludeTok,
    llvm::StringRef FileName, bool IsAngled,
    clang::CharSourceRange FilenameRange, const clang::FileEntry *File,
    llvm::StringRef SearchPath, llvm::StringRef RelativePath,
    const clang::Module *Imported, clang::SrcMgr::CharacteristicKind FileType) {
  time_t file_mtime = File->getModificationTime();
  if (file_mtime <= 0) {
    LOG(ERROR) << "Fail to get modification time for file "
               << File->tryGetRealPathName().str();
    return;
  }
  analyzer_->update_included_file_mtime(file_mtime);
}

/*********************************************
 * IAFrontendAction
 *
 *********************************************/
IAFrontendAction::IAFrontendAction(IncludeAnalyzer *analyzer)
    : analyzer_(analyzer), data_consumer_(make_shared<IAConsumer>(analyzer)) {}

unique_ptr<ASTConsumer> IAFrontendAction::CreateASTConsumer(
    CompilerInstance &ci, llvm::StringRef inFile) {
  /** Preprocessor callback to handle included files */
  shared_ptr<Preprocessor> pp = ci.getPreprocessorPtr();
  pp->addPPCallbacks(make_unique<IAPPCallBack>(analyzer_));

  /** Dummy AST consumer which do nothing */
  IndexingOptions index_opts;
  vector<unique_ptr<ASTConsumer>> consumers;
  consumers.push_back(clang::index::createIndexingASTConsumer(
      data_consumer_, index_opts, std::move(pp)));

  return make_unique<MultiplexConsumer>(std::move(consumers));
}
}  // namespace pview
