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
class PViewConsumer;
class PViewPPCallBack;
class ParseTask;

/******************************************************************************
 * Indexing service which
 *  - drive the compilation of every translation unit, and (parse task)
 *  - accept indexed token from PViewConsumer, and (parse task)
 *  - generate SQL statements, and (parse task)
 *  - store SQL statements into backend mysql server (store task)
 *
 * For every translation unit, the control flow goes like this:
 *
 *   ParseTask::run()
 *    -> ParseTask::index_translation_unit(idx)
 *      -> launch clang frontend to compile the idx'th translation unit,
 *         with a PViewConsumer passed in as a "acceptor"
 *        -> PViewConsumer translate every accepted record and then
 *           invoke ParseTask's interface to store indexed records
 ******************************************************************************/
class ParseTask {
 public:
  ParseTask(const std::vector<clang::tooling::CompileCommand> cmds,
            const std::shared_ptr<std::atomic_uint64_t> &counter)
      : cmds_(cmds), counter_(counter) {}
  ~ParseTask() = default;

  /** Entry point of "parse task" */
  void do_parse();
  /** Entry point of "store task" */
  void do_store();

  /** Get source files manager for current translation unit */
  clang::SourceManager *get_source_mgr() { return source_mgr_; }
  /** Replace source files manager for current translation unit */
  void update_source_mgr(clang::SourceManager *source_mgr) {
    source_mgr_ = source_mgr;
  }

  /** return true on error, otherwise false */
  bool gen_source_location(Location &dst, clang::SourceLocation loc);

  /** return true on error, otherwise false */
  bool gen_source_range(Range &dst, clang::SourceRange range);

  /** Accumulate indexed result for current translation unit */
  void add_func_def(const FuncDefPtr &ptr);
  void add_func_call(const FuncCallPtr &ptr);
  void add_class_def(const ClassDefPtr &ptr);

  /** Send update info to background store threads */
  int send_indexed_result();

 protected:
  /**
   * Init @mysql_conn_ if necessary.
   *
   * @returns 0 on success, otherwise 1
   */
  int init_mysql_conn();

  /**
   * Simplify filepath that belongs to the current project
   *
   * For filepath that belongs to the current project, the project root
   * is stripped
   * (i.e., from `/path/to/project/subdir/file.cc` to `subdir/file.cc`).
   * Other filepath is unchanged.
   *
   * return true if @filepath belongs to current project, otherwise false
   */
  bool simplify_source_path(std::string &filepath);

  /**
   * Index the @idx compilation unit from compile_commands.json
   *
   * @param idx     The n-th compilation task from compile_commands.json
   *
   * @returns true on error, otherwise false
   */
  bool index_translation_unit(size_t idx);

  /**
   * Query the backend database for file id of @filepath
   *
   * If @filepath does not exist in the database, assign a new one for it
   * and update it into the backend server.
   * This is a atomic read-modify-update operation using database write lock.
   */
  int64_t query_or_assign_file_id(const std::string &filepath);

  /**
   * Get last modification time of @filepath from the backend database
   *
   * If @filepath does not exists in the db, return 0
   *
   * Note:
   *  Return value is not necessary the lastest modification time of @filepath,
   *  but could be the lastest modification time of the files it includes.
   */
  int64_t get_file_last_mtime(const std::string &filepath);

  /**
   * Unconditinally update @filepath using a @filepath_id and @timestamp
   */
  int update_indexed_file(const std::string &filepath, int64_t filepath_id,
                          int64_t timestamp);

  /**
   * Generate a batch version of INSERT statement that insert all newly
   * generated FuncDef into the backend database.
   */
  std::string gen_batch_update_func_defs_stmt(size_t *start);
  /**
   * Generate a batch version of INSERT statement that insert all newly
   * generated FuncCall into the backend database.
   */
  std::string gen_batch_update_func_calls_stmt(size_t *start);
  /**
   * Helper function for gen_batch_update_func_defs_stmt()
   */
  void append_indexed_func_defs_value(std::ostringstream &oss,
                                      const FuncDefPtr &d);
  /**
   * Helper function for gen_batch_update_func_calls_stmt()
   */
  void append_indexed_func_calls_value(std::ostringstream &oss,
                                       const FuncCallPtr &d);
  /**
   * Cleanup.
   *  - Close MYSQL connection
   */
  void clean_up();

  /**
   * Return a diagnostic string header for logging
   *  Parse task[${task_id}]
   */
  std::string get_parse_task_desc() const;

  /**
   * Return a diagnostic string header for logging
   *  Store task[${task_id}]
   */
  std::string get_store_task_desc() const;

 protected:
  std::vector<clang::tooling::CompileCommand> cmds_;
  std::shared_ptr<std::atomic_uint64_t> counter_;

  std::shared_ptr<MYSQLConn> mysql_conn_;

  /** A transient clang object that is updated before each compilation */
  clang::SourceManager *source_mgr_ = nullptr;

  /**
   * Timestamp used as a mono-increased "id" for index entries in the database,
   * such that we have multi-version for a entry in the database, and don't
   * have to delete obselete entry for a file each time we try to index a file.
   * (we use a background purger for this task, which run every 1 second).
   */
  int64_t current_time_ = 0;
  /**
   * Current source filepath for every translation unit.
   * It is updated before each compilation.
   */
  std::string current_filepath_;
  int64_t current_filepath_id_ = -1;
  /**
   * Current translation unit offset within @cmds_
   * Update before each compilation.
   */
  size_t current_translation_unit_idx_ = 0;

  /**
   * Accumulated parse result for every translation unit.
   *
   * These index result will be transformed into a batch version of INSERT
   * statement at the end of translation unit indexing and then send to the
   * backedn database server for persistent.
   */
  std::vector<FuncDefPtr> func_defs_;
  std::vector<FuncCallPtr> func_calls_;
  std::vector<ClassDefPtr> class_defs_;

  /**
   * In-mem cache to avoid querying the database for every filepath.
   *
   * { filepath, lastest-modify-time }
   */
  std::unordered_map<std::string, int64_t> local_filepath_cache_;
};

/******************************************************************************
 * A boilerplate ASTFrontendAction.
 *
 * ASTFrontendAction is hiddened after a MultiplexConsumer.
 * Even though it could be "multiplex", there is only one consumer currently.
 * This class is a boilerplate to use the clang frontend.
 * The core part is PViewConsumer.
 ******************************************************************************/
class PViewFrontendAction : public clang::ASTFrontendAction {
 protected:
  ParseTask *const parse_task_;
  std::shared_ptr<PViewConsumer> data_consumer_;

 public:
  PViewFrontendAction(ParseTask *parse_task);
  ~PViewFrontendAction() = default;

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance &ci, llvm::StringRef inFile) override;
};

/******************************************************************************
 * AST consumer for indexing
 *
 * This is the core component for indexing.
 *
 * It accept as input each `clang::Decl` which is output by the clang frontend
 * when it is parsing and compiling each translation unit.
 * The PViewConsumer then generate function/class symbols in form of
 * pview::FuncDef / pview::FuncCall / pview::ClassDef, which are then
 * sended to the corresponding ParseTask and be transformed into a set of
 * SQL statements and committed to the backend database.
 ******************************************************************************/
class PViewConsumer : public clang::index::IndexDataConsumer {
 public:
  PViewConsumer(ParseTask *task) : parse_task_(task) {}
  ~PViewConsumer() = default;

  void initialize(clang::ASTContext &ctx) override { this->ctx_ = &ctx; }
  bool handleDeclOccurrence(
      const clang::Decl *d, clang::index::SymbolRoleSet roles,
      llvm::ArrayRef<clang::index::SymbolRelation> relations,
      clang::SourceLocation src_loc, ASTNodeInfo ast_node) override;

 protected:
  void handle_func(const clang::Decl *d, clang::index::SymbolRoleSet roles,
                   llvm::ArrayRef<clang::index::SymbolRelation> relations,
                   clang::SourceLocation src_loc, ASTNodeInfo ast_node);

  ClassDefPtr try_get_cls_def(const clang::CXXRecordDecl *cls_decl) const;

  /**
   * Return whether a function will throw.
   *
   * This is based on static analysis of the function definition, and
   * should not have false-positive.
   */
  bool func_might_throw(const clang::FunctionDecl *fd);

  /** Generate USR and USR hash for @d */
  void gen_usr(const clang::Decl *d, std::string &usr,
               uint64_t &usr_hash) const;

  /**
   * Generate DeclInfo for a clang::Decl
   *
   * return true on error, otherwise false
   */
  bool gen_decl_info(DeclInfo &info, const clang::Decl *d, bool is_decl,
                     bool is_def, bool is_method_or_func, bool is_class,
                     clang::SourceLocation src_loc) const;

  /**
   * Generate "qualified" name for a clang::Decl
   *
   * A "qualified" name is one that we use to specify a c++ symbol without
   * ambiguity, e.g., "MyNameSpace::MyClass::MyMethod".
   * The qualified name does NOT specify function parameters, though.
   */
  std::string gen_qualified_name(const clang::Decl *d) const;

  /**
   * Generate short name for a clang::Decl
   *
   * A short name is just a shorter version of qualified name, i.e., "MyMethod".
   */
  std::string gen_short_name(const clang::Decl *d) const;

  clang::PrintingPolicy get_print_policy() const;

  static void simplify_anonymous(std::string &name);

 protected:
  clang::ASTContext *ctx_;
  ParseTask *const parse_task_;
};

/******************************************************************************
 * A preprocessor callback to index symbols in macro expansion.
 *
 * See all interfaces in clang/Lex/PPCallbacks.h
 ******************************************************************************/
class PViewPPCallBack : public clang::PPCallbacks {
 public:
  PViewPPCallBack(ParseTask *parse_task) : parse_task_(parse_task) {}
  ~PViewPPCallBack() = default;

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
  void InclusionDirective(clang::SourceLocation HashLoc,
                          const clang::Token &IncludeTok,
                          llvm::StringRef FileName, bool IsAngled,
                          clang::CharSourceRange FilenameRange,
                          const clang::FileEntry *File,
                          llvm::StringRef SearchPath,
                          llvm::StringRef RelativePath,
                          const clang::Module *Imported,
                          clang::SrcMgr::CharacteristicKind FileType) override {
    /**
     * Language servers need this for file management, e.g., builds up
     * dependency graph between files so that the language server knowns
     * some already-changed included file will affect the current translation
     * unit and do parsing again;
     *
     * We will need this in future work.
     */
  }

  /// Called by Preprocessor::HandleMacroExpandedIdentifier when a
  /// macro invocation is found.
  void MacroExpands(const clang::Token &MacroNameTok,
                    const clang::MacroDefinition &MD, clang::SourceRange Range,
                    const clang::MacroArgs *Args) override;

  /// Hook called whenever a macro definition is seen.
  void MacroDefined(const clang::Token &MacroNameTok,
                    const clang::MacroDirective *MD) override;

  /// Hook called whenever a macro \#undef is seen.
  /// \param MacroNameTok The active Token
  /// \param MD A MacroDefinition for the named macro.
  /// \param Undef New MacroDirective if the macro was defined, null otherwise.
  ///
  /// MD is released immediately following this callback.
  void MacroUndefined(const clang::Token &MacroNameTok,
                      const clang::MacroDefinition &MD,
                      const clang::MacroDirective *Undef) override;

  /// Hook called when a source range is skipped.
  /// \param Range The SourceRange that was skipped. The range begins at the
  /// \#if/\#else directive and ends after the \#endif/\#else directive.
  /// \param EndifLoc The end location of the 'endif' token, which may precede
  /// the range skipped by the directive (e.g excluding comments after an
  /// 'endif').
  void SourceRangeSkipped(clang::SourceRange, clang::SourceLocation) override {
    /**
     * Language servers need this to determine whether the code block under
     * cursor is meaningful.
     * Not useful for us currently.
     */
  }

 protected:
  /**
   * Generate DeclInfo for macro definition/invocation.
   *
   * return true on error, false otherwise.
   */
  bool gen_macro_info(DeclInfo &info, const clang::Token &tok,
                      clang::SourceLocation src_loc,
                      clang::SourceRange src_range, bool is_def) const;

 protected:
  ParseTask *const parse_task_;
};
}  // namespace pview
