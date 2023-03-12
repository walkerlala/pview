//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "PView.h"

namespace pview {
class IAFrontendAction;
class IAPPCallBack;
class IAConsumer;
class IncludeAnalyzer;

/******************************************************************************
 * A analyzer, which, given a translation unit and its last modification time,
 * could check whether this file need to be re-indexed.
 *
 * The IncludeAnalyzer not only check the modification time of the @filepath
 * itself, but also all the header files it includes, including the include
 * files of include files, recursively.
 *
 * The working mechanism is much like a pview::ParseTask:
 *  - check modification time of @filepath.
 *  - If @filepath not updated, drive a clang to compile the translation unit
 *    and get all the file it includes, and all the files which are included
 *    by included files.
 *
 * The compilation work is a little heavy, and it seems we only need some
 * preprocessor hook for this task, but clang does not provide such interface.
 ******************************************************************************/
class IncludeAnalyzer {
 public:
  IncludeAnalyzer(const std::string &filepath, int64_t last_mtime,
                  clang::tooling::CompileCommand cmd)
      : filepath_(filepath),
        last_mtime_(last_mtime),
        cmd_(cmd),
        source_mgr_(nullptr),
        newest_mtime_(0) {}
  ~IncludeAnalyzer() = default;

  bool check_need_reindex();

  int64_t get_newest_file_mtime() const { return newest_mtime_; }

  const std::string &get_filepath() const { return filepath_; }

  /** Get source files manager for current translation unit */
  clang::SourceManager *get_source_mgr() { return source_mgr_; }
  /** Replace source files manager for current translation unit */
  void update_source_mgr(clang::SourceManager *source_mgr) {
    source_mgr_ = source_mgr;
  }

  void update_included_file_mtime(int64_t mtime) {
    if (mtime > newest_mtime_) {
      newest_mtime_ = mtime;
    }
  }

 protected:
  const std::string filepath_;
  /**
   * Last modification time of @filepath_
   *
   * If the modification time of @filepath_, or modification of any of its
   * include files is newer than this, then check_need_reindex() should return
   * true.
   *
   * Note that @last_mtime_ could be inited to 0, which means that we haven seen
   * this file before, and check_need_reindex() should immediately return true.
   */
  const int64_t last_mtime_;
  /**
   * clang compile commands (from compile_commands.json) that could be used to
   * generate clang CompilerInstance to compile @filepath_
   */
  const clang::tooling::CompileCommand cmd_;

  /** A transient clang object that is updated before each compilation */
  clang::SourceManager *source_mgr_;

  /**
   * The newest modification time within all @filepath_'s include files
   * since unix epoch in seconds.
   */
  int64_t newest_mtime_;
};

class IAFrontendAction : public clang::ASTFrontendAction {
 protected:
  IncludeAnalyzer *const analyzer_;
  std::shared_ptr<IAConsumer> data_consumer_;

 public:
  IAFrontendAction(IncludeAnalyzer *analyzer);
  ~IAFrontendAction() = default;

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance &ci, llvm::StringRef inFile) override;
};

class IAConsumer : public clang::index::IndexDataConsumer {
 public:
  IAConsumer(IncludeAnalyzer *analyzer) : analyzer_(analyzer) {}
  ~IAConsumer() = default;

  void initialize(clang::ASTContext &ctx) override { this->ctx_ = &ctx; }
  bool handleDeclOccurrence(
      const clang::Decl *d, clang::index::SymbolRoleSet roles,
      llvm::ArrayRef<clang::index::SymbolRelation> relations,
      clang::SourceLocation src_loc, ASTNodeInfo ast_node) override {
    /* true to continue indexing, or false to abort. */
    return true;
  }

 protected:
  clang::ASTContext *ctx_;
  IncludeAnalyzer *const analyzer_;
};

class IAPPCallBack : public clang::PPCallbacks {
 public:
  IAPPCallBack(IncludeAnalyzer *analyzer) : analyzer_(analyzer) {}
  ~IAPPCallBack() = default;

  /// Callback invoked whenever a source file is entered or exited.
  ///
  /// \param Loc Indicates the new location.
  /// \param PrevFID the file that was exited if \p Reason is ExitFile.
  void FileChanged(clang::SourceLocation Loc,
                   clang::PPCallbacks::FileChangeReason Reason,
                   clang::SrcMgr::CharacteristicKind FileType,
                   clang::FileID PrevFID) override;

  /// Callback invoked whenever a source file is skipped as the result
  /// of header guard optimization.
  ///
  /// \param SkippedFile The file that is skipped instead of entering \#include
  ///
  /// \param FilenameTok The file name token in \#include "FileName" directive
  /// or macro expanded file name token from \#include MACRO(PARAMS) directive.
  /// Note that FilenameTok contains corresponding quotes/angles symbols.
  void FileSkipped(const clang::FileEntryRef &SkippedFile,
                   const clang::Token &FilenameTok,
                   clang::SrcMgr::CharacteristicKind FileType) override;

  /// Callback invoked whenever an inclusion directive of
  /// any kind (\c \#include, \c \#import, etc.) has been processed, regardless
  /// of whether the inclusion will actually result in an inclusion.
  void InclusionDirective(clang::SourceLocation HashLoc,
                          const clang::Token &IncludeTok,
                          llvm::StringRef FileName, bool IsAngled,
                          clang::CharSourceRange FilenameRange,
                          const clang::FileEntry *File,
                          llvm::StringRef SearchPath,
                          llvm::StringRef RelativePath,
                          const clang::Module *Imported,
                          clang::SrcMgr::CharacteristicKind FileType) override;

 protected:
  IncludeAnalyzer *const analyzer_;
};
}  // namespace pview
