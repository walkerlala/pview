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
#include "ExceptionAnalyzer.h"
#include "FileUtils.h"
#include "IncludeAnalyzer.h"
#include "IndexCtx.h"
#include "IndexQueue.h"
#include "MYSQLConn.h"
#include "PView.h"
#include "ParseTask.h"
#include "SQL.h"
#include "StrUtils.h"
#include "ThreadPool.h"
#include "Timer.h"
#include "Unit.h"
#include "siphash.h"

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
DECLARE_int64(db_update_batch_size);
DECLARE_string(debug_single_file);

namespace pview {
/**
 * Decl::Function  ->  clang::FunctionDecl
 */
static bool is_decl_normal_function(const Decl *d) {
  auto kind = d->getKind();
  return (kind == Decl::Function);
}

/**
 * Decl::CXXMethod  ->  clang::CXXMethodDecl
 *  which is a child class of clang::Function
 *
 * Decl::CXXConstructor  ->  clang::CXXConstructorDecl
 *  which is a child class of clang::CXXMethodDecl
 *
 * Decl::CXXDestructor   ->  clang::CXXDestructorDecl
 *  which is a child class of clang::CXXMethodDecl
 *
 * Decl::CXXConversion   ->  clang::CXXConversionDecl
 *  which is a child class of clang::CXXMethodDecl
 */
static bool is_decl_cxx_method(const Decl *d) {
  auto kind = d->getKind();
  return (kind == Decl::CXXMethod || kind == Decl::CXXConstructor ||
          kind == Decl::CXXDestructor || kind == Decl::CXXConversion);
}

/******************************************************************************
 * ParseTask
 *
 ******************************************************************************/
/**
 * Init @mysql_conn_ if necessary.
 *
 * @returns 0 on success, otherwise 1
 */
int ParseTask::init_mysql_conn() {
  if (mysql_conn_) {
    return 0;
  }

  IndexCtx *ctx = IndexCtx::get_instance();
  auto *mysql_conn_mgr = ctx->get_mysql_conn_mgr();
  mysql_conn_ = mysql_conn_mgr->create_mysql_conn();
  return (mysql_conn_ == nullptr);
}

/** Entry point of "store task" */
void ParseTask::do_store() {
  if (init_mysql_conn()) {
    LOG(ERROR) << get_store_task_desc() << "failed to create mysql connection"
               << ". Abort indexing.";
    return;
  }
  ASSERT(mysql_conn_);

  IndexCtx *ctx = IndexCtx::get_instance();
  IndexQueue *queue = ctx->get_index_queue();
  while (true) {
    auto stmts = queue->get_stmts();
    if (!stmts || !stmts->size()) {
      break;
    }

    for (size_t n = 0; n < stmts->size(); n++) {
      const std::string &stmt = (*stmts)[n];
      if (mysql_conn_->dml(stmt)) {
        LOG(WARNING) << "Fail to update table in mysql"
                     << ". Discard all subsequent stmts"
                     << ". Current offset " << n << ", total " << stmts->size();
        break;
      }
    }
    LOG_IF(INFO, FLAGS_verbose)
        << "Done storing a batch of stmts: " << stmts->size();
  }
  LOG(INFO) << get_store_task_desc() << "done its job. Exit.";
}

void ParseTask::append_indexed_func_defs_value(std::ostringstream &oss,
                                               const FuncDefPtr &d) {
  int64_t func_def_id = d->get_id();
  ASSERT(func_def_id >= 0);
  const auto &usr = d->get_info().usr;
  const auto &qualified = d->get_info().qualified;
  bool might_throw = d->get_info().func_might_throw;
  const int64_t location_file_id = d->get_info().source_location.file_id;
  int64_t location_line = d->get_info().source_location.line;

  /* clang-format off */
  const auto &stmt = fmt::format(
      PCTX->get_sql_func_def_insert_param(),
      fmt::arg("arg_func_def_id", func_def_id),
      fmt::arg("arg_create_time", current_time_),
      fmt::arg("arg_usr", usr),
      fmt::arg("arg_qualified", qualified),
      fmt::arg("arg_might_throw", might_throw),
      fmt::arg("arg_location_file_id", location_file_id),
      fmt::arg("arg_location_line", location_line));
  /* clang-format on */

  oss << stmt;
}

std::string ParseTask::gen_batch_update_func_defs_stmt(size_t *start) {
  ASSERT(start);

  std::ostringstream oss;
  size_t appended = 0;
  size_t n = *start;
  for (; n < func_defs_.size() && appended < FLAGS_db_update_batch_size; n++) {
    const auto &d = func_defs_[n];
    if (!d->get_info().usr.size() || !d->get_info().qualified.size()) {
      continue;
    } else if (d->get_info().source_location.file_id < 0) {
      continue;
    }

    if (appended == 0) {
      oss << PCTX->get_sql_func_def_insert_header();
    } else if (appended != 0) {
      oss << ", ";
    }
    append_indexed_func_defs_value(oss, d);
    appended++;
  }
  *start = n;
  return oss.str();
}

void ParseTask::append_indexed_func_calls_value(std::ostringstream &oss,
                                                const FuncCallPtr &d) {
  int64_t func_call_id = d->get_id();
  ASSERT(func_call_id >= 0);
  const auto &caller_usr = d->get_caller_usr();
  bool caller_might_throw = d->get_caller_might_throw();
  const auto &usr = d->get_info().usr;
  const auto &qualified = d->get_info().qualified;
  const int64_t location_file_id = d->get_info().source_location.file_id;
  int64_t location_line = d->get_info().source_location.line;

  /* clang-format off */
  const auto &stmt = fmt::format(
      PCTX->get_sql_func_call_insert_param(),
      fmt::arg("arg_func_call_id", func_call_id),
      fmt::arg("arg_create_time", current_time_),
      fmt::arg("arg_caller_usr", caller_usr),
      fmt::arg("arg_caller_might_throw", caller_might_throw),
      fmt::arg("arg_usr", usr),
      fmt::arg("arg_qualified", qualified),
      fmt::arg("arg_location_file_id", location_file_id),
      fmt::arg("arg_location_line", location_line));
  /* clang-format on */

  oss << stmt;
}

std::string ParseTask::gen_batch_update_func_calls_stmt(size_t *start) {
  ASSERT(start);

  std::ostringstream oss;
  size_t appended = 0;
  size_t n = *start;
  for (; n < func_calls_.size() && appended < FLAGS_db_update_batch_size; n++) {
    const auto &d = func_calls_[n];
    /** Skip func call without caller info. */
    if (!d->get_caller_usr().size()) {
      continue;
    } else if (d->get_info().source_location.file_id < 0) {
      continue;
    }
    if (appended == 0) {
      oss << PCTX->get_sql_func_call_insert_header();
    } else if (appended != 0) {
      oss << ", ";
    }
    append_indexed_func_calls_value(oss, d);
    appended++;
  }
  *start = n;
  return oss.str();
}

int ParseTask::send_indexed_result() {
  std::vector<std::string> stmts;

  /** FuncDef index result */
  size_t fd_start = 0;
  while (fd_start < func_defs_.size()) {
    auto &&defs_stmt = gen_batch_update_func_defs_stmt(&fd_start);
    if (defs_stmt.size()) {
      stmts.push_back(std::move(defs_stmt));
    }
  }

  /** FuncCall index result */
  size_t fc_start = 0;
  while (fc_start < func_calls_.size()) {
    auto &&calls_stmt = gen_batch_update_func_calls_stmt(&fc_start);
    if (calls_stmt.size()) {
      stmts.push_back(std::move(calls_stmt));
    }
  }

  /** filepaths */
  /* clang-format off */
  std::string replace_fn_stmt = fmt::format(
      PCTX->get_sql_replace_filepath(),
      fmt::arg("arg_filepath_id", current_filepath_id_),
      fmt::arg("arg_filepath", current_filepath_),
      fmt::arg("arg_create_time", current_time_));
  /* clang-format on */
  ASSERT(replace_fn_stmt.size());
  stmts.push_back(std::move(replace_fn_stmt));

  func_defs_.clear();
  func_calls_.clear();
  class_defs_.clear();

  IndexCtx *ctx = IndexCtx::get_instance();
  auto *index_queue = ctx->get_index_queue();
  index_queue->add_stmts(std::move(stmts));
  return 0;
}

int ParseTask::update_indexed_file(const std::string &filepath,
                                   int64_t filepath_id, int64_t timestamp) {
  ASSERT(mysql_conn_);

  /* clang-format off */
  const std::string &replace_stmt = fmt::format(
      PCTX->get_sql_replace_filepath(),
      fmt::arg("arg_filepath_id", filepath_id),
      fmt::arg("arg_filepath", filepath),
      fmt::arg("arg_create_time", timestamp));
  /* clang-format on */

  {
    MYSQLTableLock lk(mysql_conn_, kPViewIndexDB,
                      PCTX->get_filepath_tbl_name());
    if (lk.lock()) {
      LOG(WARNING) << "Fail to lock filepaths table in mysql";
      return 1;
    }
    if (mysql_conn_->dml(replace_stmt)) {
      return 1;
    }
  }
  return 0;
}

int64_t ParseTask::query_or_assign_file_id(const std::string &filepath) {
  /** First query in-mem cache */
  auto itr = local_filepath_cache_.find(filepath);
  if (itr != local_filepath_cache_.end()) {
    return itr->second;
  }

  /** Read-modify-update */
  ASSERT(mysql_conn_);
  int64_t fn_id = -1;
  do {
    auto stmt = fmt::format(PCTX->get_sql_query_filepath_id(),
                            fmt::arg("arg_filepath", filepath));
    MYSQLTableLock lk(mysql_conn_, kPViewIndexDB,
                      PCTX->get_filepath_tbl_name());
    if (lk.lock()) {
      LOG(WARNING) << "Fail to lock filepaths table in mysql";
      break;
    }

    auto stmt_result = mysql_conn_->query(stmt);
    while (stmt_result->next()) {
      fn_id = stmt_result->getInt(1);
      /** filepath is primary key, so we don't have to check for duplicate */
      break;
    }

    if (fn_id >= 0) {
      break;
    }

    int64_t file_mtime = get_file_mtime(filepath);

    IndexCtx *ctx = IndexCtx::get_instance();
    fn_id = ctx->get_next_filepath_id();

    /* clang-format off */
    const std::string &replace_stmt =
        fmt::format(PCTX->get_sql_replace_filepath(),
                    fmt::arg("arg_filepath_id", fn_id),
                    fmt::arg("arg_filepath", filepath),
                    fmt::arg("arg_create_time", file_mtime));
    /* clang-format on */

    if (mysql_conn_->dml(replace_stmt)) {
      LOG(ERROR) << "Fail to update filepath id for: " << filepath
                 << ", new id: " << fn_id;
      fn_id = -1;
      break; /* update failed */
    }
  } while (0);

  /** Update in-mem cache */
  if (fn_id >= 0) {
    local_filepath_cache_.emplace(filepath, fn_id);
  }

  return fn_id;
}

int64_t ParseTask::get_file_last_mtime(const std::string &filepath) {
  const auto &stmt = fmt::format(PCTX->get_sql_get_file_mtime(),
                                 fmt::arg("arg_filepath", filepath));
  auto stmt_result = mysql_conn_->query(stmt);
  if (!stmt_result) {
    return 0;
  }
  while (stmt_result->next()) {
    int64_t mtime = stmt_result->getInt(1);
    return mtime;
  }
  return 0;
}

/** Entry point of "parse task" */
void ParseTask::do_parse() {
  if (init_mysql_conn()) {
    LOG(ERROR) << get_parse_task_desc()
               << "failed to create mysql connection. Abort indexing.";
    return;
  }

  Timer timer;
  size_t total_work = 0;
  size_t success_work = 0;
  /** Parse translation unit and generate the graph */
  while (true) {
    size_t idx = counter_->fetch_add(1);
    if (idx >= cmds_.size()) {
      break;
    }
    total_work++;
    if (!index_translation_unit(idx)) {
      success_work++;
    }
  }

  LOG(INFO) << get_parse_task_desc() << "finished indexing " << total_work
            << " translation units (" << success_work << " succeeded) in "
            << timer.DurationMicroseconds() / 1000 << " ms.";
  clean_up();
}

void ParseTask::clean_up() {
  cmds_.clear();
  counter_ = nullptr;

  mysql_conn_ = nullptr;

  source_mgr_ = nullptr;

  current_time_ = 0;
  current_filepath_.clear();
  current_filepath_id_ = -1;
  current_translation_unit_idx_ = 0;

  func_defs_.clear();
  func_calls_.clear();
  class_defs_.clear();

  local_filepath_cache_.clear();
}

std::string ParseTask::get_parse_task_desc() const {
  auto &thread_pool = ThreadPool::GlobalPool();
  return ("ParseTask[" + std::to_string(thread_pool.GetWorkerId()) + "] ");
}

std::string ParseTask::get_store_task_desc() const {
  auto &thread_pool = ThreadPool::GlobalPool();
  return ("StoreTask[" + std::to_string(thread_pool.GetWorkerId()) + "] ");
}

/** return true on error, otherwise false */
bool ParseTask::gen_source_location(Location &dst, SourceLocation loc) {
  bool invalid = false;

  auto &mgr = *source_mgr_;

  SourceLocation spelling_loc = mgr.getSpellingLoc(loc);

  /**
   * We use spelling line number instead of expansion line number so that
   * we could identify function calls inside macro defintion as "call by macro"
   */
  unsigned line_num = mgr.getSpellingLineNumber(loc, &invalid);
  if (!invalid) {
    dst.line = line_num;
  } else {
    dst.line = kInvalidLine;
  }

  unsigned col_num = mgr.getSpellingLineNumber(loc, &invalid);
  if (!invalid) {
    dst.column = col_num;
  } else {
    dst.column = kInvalidColumn;
  }

  dst.file = mgr.getFilename(spelling_loc).str();
  bool in_project = simplify_source_path(dst.file);
  if (dst.file.size() > kMaxFilePathLen) {
    LOG(WARNING) << "Path too long (larger than " << kMaxFilePathLen
                 << "): " << dst.file << ". Reset to magic.";
    dst.file = kTooLongFilePath;
  }
  if (dst.file == current_filepath_) {
    dst.file_id = current_filepath_id_;
  } else if (in_project) {
    dst.file_id = query_or_assign_file_id(dst.file);
  } else {
    dst.file_id = kInvalidFileId;
  }
  return false;
}

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
bool ParseTask::simplify_source_path(std::string &filepath) {
  IndexCtx *ctx = IndexCtx::get_instance();
  const auto &project_root = ctx->get_project_root();

  if (!string_starts_with(filepath, project_root)) {
    return false;
  }

  const size_t min_len = project_root.size();
  ASSERT(min_len <= filepath.size());

  size_t new_path_len = filepath.size() - min_len;
  if (!new_path_len) {
    filepath = "";
    return false; /* unknown */
  }

  std::string new_path(new_path_len, '\0');
  char *dst = &(new_path[0]);
  char *src = &(filepath[min_len]);
  std::memcpy(dst, src, new_path_len);
  filepath = new_path;
  return true;
}

/** return true on error, otherwise false */
bool ParseTask::gen_source_range(Range &dst, SourceRange range) {
  SourceLocation range_begin = range.getBegin();
  if (gen_source_location(dst.start, range_begin)) {
    return true;
  }

  SourceLocation range_end = range.getEnd();
  if (gen_source_location(dst.end, range_end)) {
    return true;
  }
  return false;
}

void ParseTask::add_func_def(const FuncDefPtr &ptr) {
  func_defs_.push_back(ptr);
}

void ParseTask::add_func_call(const FuncCallPtr &ptr) {
  func_calls_.push_back(ptr);
}

void ParseTask::add_class_def(const ClassDefPtr &ptr) {
  class_defs_.push_back(ptr);
}

/** @returns true on error, otherwise false */
bool ParseTask::index_translation_unit(size_t idx) {
  ASSERT(idx < cmds_.size());
  Timer timer;

  const CompileCommand &cmd = cmds_[idx];

  if (FLAGS_debug_single_file.size() &&
      cmd.Filename != FLAGS_debug_single_file) {
    DLOG(INFO) << "Skip " << cmd.Filename;
    return false;
  }

  /**
   * Skip this file if it is not updated since last time.
   */
  std::string original_filepath = cmd.Filename;

  std::string filepath = original_filepath;
  simplify_source_path(filepath);
  int64_t last_mtime = get_file_last_mtime(filepath);

  IncludeAnalyzer include_analyzer(original_filepath, last_mtime, cmd);
  bool need_reindex = include_analyzer.check_need_reindex();
  if (!need_reindex) {
    LOG(INFO) << "File indexes up-to-date (" << idx << "/" << cmds_.size()
              << "): " << original_filepath;
    return false;
  }

  IndexCtx *ctx = IndexCtx::get_instance();
  int64_t filepath_id = ctx->get_next_filepath_id();

  /**
   * Write an entry of this file into the database, with a timestamp value 0.
   *
   * After all tokens are indexed, the timestamp value will be corrected to
   * file_mtime. If anything bad happens in-between, this file is marked as
   * 0 and the next time we try to index it, it will be indexed again.
   *
   * Obselete entries of this file will be purged async.
   */
  if (update_indexed_file(filepath, filepath_id, 0)) {
    LOG(ERROR) << "Failed to update indexed file: " << filepath;
    return true;
  }

  timespec tv;
  clock_gettime(CLOCK_REALTIME, &tv);
  current_time_ = static_cast<int64_t>(tv.tv_sec);
  current_filepath_ = filepath;
  current_filepath_id_ = filepath_id;
  current_translation_unit_idx_ = idx;

  unique_ptr<CompilerInstance> inst = build_compiler_instance(cmd);
  if (!inst) {
    LOG(ERROR) << "Failed to build CompilerInstance for file " << cmd.Filename;
    return true;
  }
  this->update_source_mgr(&(inst->getSourceManager()));

  auto action = make_unique<PViewFrontendAction>(this);

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
      LOG(ERROR) << "clang crash when indexing file: " << cmd.Filename;
      return true;
    }
  }

  if (!ok) {
    LOG(ERROR) << "failed to index file: " << cmd.Filename
               << ", err: " << err_msg;
    return true;
  }

  /**
   * Send index result in the same batch in the following order:
   *  - statements for FuncDef
   *  - statements for FuncCall
   *  - statements for filepath
   * The IndexQueue and ParseTask::store_tasks() guarantee the
   * "happens-after-or-fail" relationship within the same batch, so that
   * we are sure that update to filepaths will come after anything other, which
   * means that
   *  - If update to a file path happens, all index result of this file
   *    have succeeded, otherwise
   *  - If update to a file does not happen, it will be re-index the next time.
   */
  if (send_indexed_result()) {
    LOG(ERROR) << "Fail to send batched-update indexed result for " << filepath;
    return true;
  }

  LOG(INFO) << "Done indexing in " << timer.DurationMicroseconds() / 1000
            << " ms (" << idx << "/" << cmds_.size() << "): " << cmd.Filename;
  return false;
}

/*********************************************
 * PViewConsumer
 *
 *********************************************/
bool PViewConsumer::handleDeclOccurrence(
    const Decl *d, SymbolRoleSet roles,
    llvm::ArrayRef<SymbolRelation> relations, SourceLocation src_loc,
    ASTNodeInfo ast_node) {
  [[maybe_unused]] bool is_decl = roles & uint32_t(SymbolRole::Declaration);
  [[maybe_unused]] bool is_def = roles & uint32_t(SymbolRole::Definition);
  if (is_decl && d->getKind() == Decl::Binding) is_def = true;

  [[maybe_unused]] auto kind = d->getKind();
  [[maybe_unused]] bool is_cxx_method = is_decl_cxx_method(d);
  [[maybe_unused]] bool is_function = is_decl_normal_function(d);
  [[maybe_unused]] bool is_method_or_func = (is_cxx_method || is_function);
  [[maybe_unused]] bool is_class = (kind == Decl::CXXRecord);

  if (is_method_or_func) {
    handle_func(d, roles, relations, src_loc, ast_node);
  }
  return true;
}

void PViewConsumer::handle_func(const Decl *d, SymbolRoleSet roles,
                                llvm::ArrayRef<SymbolRelation> relations,
                                SourceLocation src_loc, ASTNodeInfo ast_node) {
  [[maybe_unused]] bool is_decl = roles & uint32_t(SymbolRole::Declaration);
  [[maybe_unused]] bool is_def = roles & uint32_t(SymbolRole::Definition);
  if (is_decl && d->getKind() == Decl::Binding) is_def = true;

  [[maybe_unused]] auto kind = d->getKind();
  [[maybe_unused]] bool is_cxx_method = is_decl_cxx_method(d);
  [[maybe_unused]] bool is_function = is_decl_normal_function(d);
  [[maybe_unused]] bool is_method_or_func = (is_cxx_method || is_function);
  [[maybe_unused]] bool is_class = (kind == Decl::CXXRecord);

  DeclInfo info;
  if (gen_decl_info(info, d, is_decl, is_def, is_method_or_func, is_class,
                    src_loc)) {
    LOG_IF(WARNING, FLAGS_verbose) << "Fail to get decl info for symbol";
    return;
  }

  // spell, extent, comments use OrigD while most others use adjusted |D|.
  const Decl *origD = ast_node.OrigD;
  [[maybe_unused]] const DeclContext *sem_dc =
      origD->getDeclContext()->getRedeclContext();
  const DeclContext *lex_dc = ast_node.ContainerDC->getRedeclContext();
  {
    const NamespaceDecl *nd;
    while ((nd = clang::dyn_cast<NamespaceDecl>(clang::cast<Decl>(sem_dc))) &&
           nd->isAnonymousNamespace())
      sem_dc = nd->getDeclContext()->getRedeclContext();
    while ((nd = clang::dyn_cast<NamespaceDecl>(clang::cast<Decl>(lex_dc))) &&
           nd->isAnonymousNamespace())
      lex_dc = nd->getDeclContext()->getRedeclContext();
  }

  auto *fd = clang::dyn_cast<clang::FunctionDecl>(d);
  ASSERT(fd);

  bool is_static = fd->isStatic();
  bool is_virtual =
      (is_cxx_method && clang::dyn_cast<clang::CXXMethodDecl>(d) &&
       clang::dyn_cast<clang::CXXMethodDecl>(d)->isVirtual());
  if (is_def) {
    /** Get cxx method class definition. */
#if 0
    ClassDefPtr cls_def = nullptr;
    if (is_cxx_method) {
      do {
        auto *cxx_md = clang::dyn_cast<clang::CXXMethodDecl>(d);
        ASSERT(cxx_md, "Fail to dynamic cast a CXX method");

        const clang::CXXRecordDecl *cls_decl = cxx_md->getParent();
        ASSERT(cls_decl, "Fail to get CXXRecordDecl from AST tree for method ");
        ASSERT(cls_decl->hasDefinition(),
               "ClassDecl of class method does not have definition");

        cls_def = try_get_cls_def(cls_decl);
        if (!cls_def) {
          SourceLocation cls_src_loc = cls_decl->getInnerLocStart();

          DeclInfo cls_info;
          if (gen_decl_info(cls_info, cls_decl, /*is_decl*/ false,
                            /*is_def*/ true, /*is_method_or_func*/ false,
                            /*is_class*/ true, cls_src_loc)) {
            LOG_IF(WARNING, FLAGS_verbose) << "Fail to get decl info for class";
            break;
          }
          cls_def = make_shared<ClassDef>(cls_info);
          parse_task_->cache_add_cls_def(cls_decl, cls_def);
          parse_task_->add_cls_def(cls_def);
        }
        ASSERT(cls_def);

      } while (0);
    }
    auto func_def = make_shared<FuncDef>(info, is_static, is_virtual, cls_def);
    if (cls_def) {
      cls_def->add_method(func_def);
    }
    parse_task_->cache_add_func_def(fd, func_def);
    parse_task_->add_func_def(func_def);
#endif
    info.func_might_throw = func_might_throw(fd);
    int64_t new_id = IndexCtx::get_instance()->get_next_func_def_id();
    auto func_def =
        make_shared<FuncDef>(new_id, info, is_static, is_virtual, nullptr);
    parse_task_->add_func_def(func_def);
  } else if (is_decl) {
    /** Do nothing for declaration */
  } else {
    /**
     * Function call
     */
    int64_t new_id = IndexCtx::get_instance()->get_next_func_call_id();
    auto func_call = make_shared<FuncCall>(new_id, info);
    /** Obtain its caller's information if any */
    const Decl *dc = clang::cast<Decl>(lex_dc);
    if (is_decl_cxx_method(dc) || is_decl_normal_function(dc)) {
      auto *caller_fd = clang::dyn_cast<clang::FunctionDecl>(dc);
      ASSERT(caller_fd);
      bool caller_might_throw = func_might_throw(caller_fd);

      std::string caller_usr;
      uint64_t caller_usr_hash;
      gen_usr(caller_fd, caller_usr, caller_usr_hash);

      func_call->set_caller_info(caller_usr_hash, caller_usr,
                                 caller_might_throw);
    } else {
      DLOG(WARNING) << "No caller object for func call " << info.usr;
    }
    parse_task_->add_func_call(func_call);
  }
}

bool PViewConsumer::func_might_throw(const clang::FunctionDecl *fd) {
  ExceptionAnalyzer exception_tracer;

  // Does not check recursively.
  //
  // FIXME
  // Ideally we want to check only the function itself (i.e., level=1)
  // because we will link the call path ourself using the database.
  // But we have to handle MACRO properly.
  // Now we hardcode it to be 20, which should deal with most of the case.
  exception_tracer.setMaxTraceLevel(20);

  // Ignore AssertionError
  // Note that
  //  - the @ignored_exceptions is a list separated by ;
  //    for example, "AExceptionType;BExceptionType"
  //  - Each name should be a short-name, e.g., "AExceptionType"
  //    instead of "SomeNameSpace::AExceptionType"
  std::string ignored_exceptions = "AssertionError";
  llvm::SmallVector<llvm::StringRef, 8> ignored_vec;
  llvm::StringRef(ignored_exceptions).split(ignored_vec, ",", -1, false);

  llvm::StringSet<> ignored_set;
  ignored_set.insert(ignored_vec.begin(), ignored_vec.end());
  exception_tracer.ignoreExceptions(std::move(ignored_set));

  // Ingore bad alloc throw by new
  exception_tracer.ignoreBadAlloc(true);

  auto exception_behaviour = exception_tracer.analyze(fd).getBehaviour();
  return (exception_behaviour == pview::ExceptionAnalyzer::State::Throwing);
}

void PViewConsumer::gen_usr(const Decl *d, std::string &usr,
                            uint64_t &usr_hash) const {
  d = d->getCanonicalDecl();
  llvm::SmallString<kUsrLength> tmp_usr;
  clang::index::generateUSRForDecl(d, tmp_usr);

  usr = static_cast<std::string>(tmp_usr);
  usr_hash = hash_usr(usr);
}

/** return true on error, otherwise false */
bool PViewConsumer::gen_decl_info(DeclInfo &info, const Decl *d, bool is_decl,
                                  bool is_def, bool is_method_or_func,
                                  bool is_class, SourceLocation src_loc) const {
  d = d->getCanonicalDecl();
  gen_usr(d, info.usr, info.usr_hash);

  info.short_name = gen_short_name(d);
  info.qualified = gen_qualified_name(d);
  info.qualified_hash = hash_usr(info.qualified);

  if (parse_task_->gen_source_location(info.source_location, src_loc)) {
    return true;
  }

  /** Get definition body range, i.e., [start, end] */
  if (is_method_or_func) {
    auto *fd = clang::dyn_cast<clang::FunctionDecl>(d);
    ASSERT(fd);
    clang::Stmt *func_stmt = fd->getBody();
    if (func_stmt) {
      SourceRange src_range = func_stmt->getSourceRange();
      if (parse_task_->gen_source_range(info.source_range, src_range)) {
        return true;
      }
    } else {
      /** For function call, we don't treat invalid range as error */
      parse_task_->gen_source_range(info.source_range, fd->getSourceRange());
    }
  } else if (is_class) {
    /**
     * Currently we don't want source range for a class definition
     */
  }

  return false;
}

std::string PViewConsumer::gen_qualified_name(const clang::Decl *d) const {
  ASSERT(d);
  d = d->getCanonicalDecl();
  const auto *nd = clang::dyn_cast<clang::NamedDecl>(d);
  std::string qualified;
  if (nd) {
    llvm::raw_string_ostream os(qualified);
    nd->printQualifiedName(os, get_print_policy());
    simplify_anonymous(qualified);
  }
  return qualified;
}

std::string PViewConsumer::gen_short_name(const clang::Decl *d) const {
  ASSERT(d);
  d = d->getCanonicalDecl();
  const auto *nd = clang::dyn_cast<clang::NamedDecl>(d);
  std::string short_name;
  if (nd) {
    short_name = nd->getNameAsString();
  }
  return short_name;
}

PrintingPolicy PViewConsumer::get_print_policy() const {
  PrintingPolicy pp(ctx_->getLangOpts());
  pp.AnonymousTagLocations = false;
  pp.TerseOutput = true;
  pp.PolishForDeclaration = true;
  pp.ConstantsAsWritten = true;
  pp.SuppressTagKeyword = true;
  pp.SuppressUnwrittenScope = false;
  pp.SuppressInitializers = true;
  pp.FullyQualifiedName = false;
  return pp;
}

void PViewConsumer::simplify_anonymous(std::string &name) {
  for (std::string::size_type i = 0;;) {
    if ((i = name.find("(anonymous ", i)) == std::string::npos) break;
    i++;
    if (name.size() - i > 19 && name.compare(i + 10, 9, "namespace") == 0)
      name.replace(i, 19, "anon ns");
    else
      name.replace(i, 9, "anon");
  }
}

/*********************************************
 * PViewPPCallBack
 *
 *********************************************/
/**
 * Generate DeclInfo for macro definition/invocation.
 *
 * return true on error, false otherwise.
 */
bool PViewPPCallBack::gen_macro_info(DeclInfo &info, const Token &tok,
                                     SourceLocation src_loc,
                                     SourceRange src_range, bool is_def) const {
  llvm::StringRef name = tok.getIdentifierInfo()->getName();
  info.short_name = static_cast<std::string>(name);

  llvm::SmallString<256> usr("@macro@");
  usr += name;
  uint64_t usr_hash = hash_usr(usr);

  info.usr = static_cast<std::string>(usr);
  info.usr_hash = usr_hash;

  info.qualified = static_cast<std::string>(usr);
  info.qualified_hash = usr_hash;

  if (parse_task_->gen_source_location(info.source_location, src_loc)) {
    return true;
  }
  if (is_def && parse_task_->gen_source_range(info.source_range, src_range)) {
    return true;
  }
  return false;
}

/// Called by Preprocessor::HandleMacroExpandedIdentifier when a
/// macro invocation is found.
///
/// To us, this is like a function call.
void PViewPPCallBack::MacroExpands(const Token &MacroNameTok,
                                   const MacroDefinition &MD, SourceRange range,
                                   const MacroArgs *Args) {
  clang::MacroInfo *mi = MD.getMacroInfo();
  /** Do not handle builtin macro, like __LINE__ or _Pragma */
  if (!mi || mi->isBuiltinMacro()) {
    return;
  }

  SourceManager *sm = parse_task_->get_source_mgr();
  SourceLocation src_loc = sm->getSpellingLoc(range.getBegin());
  SourceRange src_range = SourceRange{src_loc, src_loc};

  DeclInfo info;
  if (gen_macro_info(info, MacroNameTok, src_loc, src_range,
                     /*is_def*/ false)) {
    DLOG_IF(INFO, FLAGS_verbose)
        << "Macro invocation without file/line info: " << info.qualified;
    return;
  }

  int64_t new_id = IndexCtx::get_instance()->get_next_func_call_id();
  auto macro_call = make_shared<FuncLikeMacroCall>(new_id, info);
  parse_task_->add_func_call(macro_call);
}

/// Hook called whenever a macro definition is seen.
///
/// To us, this is like a function definition.
void PViewPPCallBack::MacroDefined(const Token &MacroNameTok,
                                   const MacroDirective *MD) {
  const clang::MacroInfo *mi = MD->getMacroInfo();
  /** Do not handle builtin macro, like __LINE__ or _Pragma */
  if (!mi || mi->isBuiltinMacro()) {
    return;
  }

  SourceLocation src_loc = MD->getLocation();
  SourceRange src_range{mi->getDefinitionLoc(), mi->getDefinitionEndLoc()};

  DeclInfo info;
  if (gen_macro_info(info, MacroNameTok, src_loc, src_range, /*is_def*/ true)) {
    DLOG_IF(INFO, FLAGS_verbose)
        << "Macro definition without file/line info: " << info.qualified;
    return;
  }

  int64_t new_id = IndexCtx::get_instance()->get_next_func_def_id();
  auto func_def = make_shared<FuncLikeMacroDef>(new_id, info);
  parse_task_->add_func_def(func_def);
}

void PViewPPCallBack::MacroUndefined(const Token &MacroNameTok,
                                     const MacroDefinition &MD,
                                     const MacroDirective *Undef) {
  /**
   * This give us more fine-grained control when there is duplicate macro name
   * with different definition when parsing the same translation unit.
   * Leave this as a future work.
   */
}

/*********************************************
 * PViewFrontendAction
 *
 *********************************************/
PViewFrontendAction::PViewFrontendAction(ParseTask *parse_task)
    : parse_task_(parse_task),
      data_consumer_(make_shared<PViewConsumer>(parse_task_)) {}

unique_ptr<ASTConsumer> PViewFrontendAction::CreateASTConsumer(
    CompilerInstance &ci, llvm::StringRef inFile) {
  /**
   * Preprocessor callback to index macros definition/expansion,
   * and handle include/import, etc.
   */
  shared_ptr<Preprocessor> pp = ci.getPreprocessorPtr();
  pp->addPPCallbacks(make_unique<PViewPPCallBack>(parse_task_));

  /**
   * AST consumer to index function/class/method definitions,
   * throw expressions, etc.
   */
  IndexingOptions index_opts;
  index_opts.SystemSymbolFilter = IndexingOptions::SystemSymbolFilterKind::All;
  index_opts.IndexFunctionLocals = true;
  index_opts.IndexImplicitInstantiation = true;
  index_opts.IndexParametersInDeclarations = true;
  index_opts.IndexTemplateParameters = false;

  vector<unique_ptr<ASTConsumer>> consumers;
  consumers.push_back(clang::index::createIndexingASTConsumer(
      data_consumer_, index_opts, std::move(pp)));

  return make_unique<MultiplexConsumer>(std::move(consumers));
}
}  // namespace pview
