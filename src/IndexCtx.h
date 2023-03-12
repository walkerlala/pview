//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <memory>
#include <string>

#include <clang/Tooling/CompilationDatabase.h>

#include "IndexQueue.h"
#include "MYSQLConn.h"
#include "MYSQLMgr.h"

namespace pview {
/******************************************************************************
 * Global singleton indexing context shared by all tasks.
 *
 * IndexCtx provides interfaces to read and modify "global variables".
 ******************************************************************************/
class IndexCtx {
 public:
  ~IndexCtx() = default;

  static IndexCtx *get_instance();

  void set_max_dop(size_t parse_task, size_t store_tasks) {
    ASSERT(parse_task >= 1 && store_tasks >= 1);
    num_parse_tasks_ = parse_task;
    num_store_tasks_ = store_tasks;
  }
  size_t get_num_parse_task() const { return num_parse_tasks_; }
  size_t get_num_store_task() const { return num_store_tasks_; }

  void set_compile_commands(
      std::unique_ptr<clang::tooling::CompilationDatabase> cdb) {
    cdb_ = std::move(cdb);
  }
  auto *get_comile_commands() { return cdb_.get(); }

  void set_project_root(const std::string &root) {
    ASSERT(root.size() && root[0] == '/');
    project_root_ = root;
  }
  const auto &get_project_root() const { return project_root_; }

  void set_project_id(int64_t project_id) { project_id_ = project_id; }
  int64_t get_project_id() const { return project_id_; }

  void set_project_table_names(const std::string &filepath_table,
                               const std::string &func_defs_table,
                               const std::string &func_calls_table);

  const std::string &get_filepath_tbl_name() const { return filepath_table_; }
  const std::string &get_func_defs_tbl_name() const { return func_defs_table_; }
  const std::string &get_func_calls_tbl_name() const {
    return func_calls_table_;
  }

  /**
   * MYSQL table for filename
   */
  const auto &get_sql_filepaths_create() const { return sql_filepaths_create_; }
  const auto &get_sql_query_filepath_id() const {
    return sql_query_filepath_id_;
  }
  const auto &get_sql_replace_filepath() const { return sql_replace_filepath_; }
  const auto &get_sql_is_file_exists() const { return sql_is_file_exists_; }
  const auto &get_sql_get_file_mtime() const { return sql_get_file_mtime_; }
  const auto &get_sql_is_file_obselete() const { return sql_is_file_obselete_; }
  const auto &get_sql_max_filepath_id() const { return sql_max_filepath_id_; }

  /**
   * MYSQL table for FuncDef
   */
  const auto &get_sql_func_def_create() const { return sql_func_def_create_; }
  const auto &get_sql_func_def_insert_header() const {
    return sql_func_def_insert_header_;
  }
  const auto &get_sql_func_def_insert_param() const {
    return sql_func_def_insert_param_;
  }
  const auto &get_sql_query_func_def_id() const {
    return sql_query_func_def_id_;
  }
  const auto &get_sql_query_func_def_usr() const {
    return sql_query_func_def_usr_;
  }
  const auto &get_sql_query_func_def_using_qualified() const {
    return sql_query_func_def_using_qualified_;
  }
  const auto &get_sql_max_func_def_id() const { return sql_max_func_def_id_; }

  /**
   * MYSQL table for FuncCall
   */
  const auto &get_sql_func_call_create() const { return sql_func_call_create_; }
  const auto &get_sql_func_call_insert_header() const {
    return sql_func_call_insert_header_;
  }
  const auto &get_sql_func_call_insert_param() const {
    return sql_func_call_insert_param_;
  }
  const auto &get_sql_query_func_call_info() const {
    return sql_query_func_call_info_;
  }
  const auto &get_sql_query_func_call_info_with_throw() const {
    return sql_query_func_call_info_with_throw_;
  }
  const auto &get_sql_query_caller_usr_using_qualified() const {
    return sql_query_caller_usr_using_qualified_;
  }
  const auto &get_sql_max_func_call_id() const { return sql_max_func_call_id_; }

  void set_mysql_mgr(const std::shared_ptr<MYSQLMgr> &ptr) { mysql_mgr_ = ptr; }
  const auto &get_mysql_mgr() const { return mysql_mgr_; }

  void set_mysql_conn_mgr(std::unique_ptr<MYSQLConnMgr> &&ptr) {
    mysql_conn_mgr_ = std::move(ptr);
  }
  auto *get_mysql_conn_mgr() const { return mysql_conn_mgr_.get(); }

  void set_index_queue(std::unique_ptr<IndexQueue> queue) {
    index_queue_ = std::move(queue);
  }
  auto *get_index_queue() const { return index_queue_.get(); }

  void set_next_filepath_id(int64_t val) { next_filepath_id_.store(val); }
  int64_t get_next_filepath_id() { return next_filepath_id_.fetch_add(1); }

  void set_next_func_def_id(int64_t val) { next_func_def_id_.store(val); }
  int64_t get_next_func_def_id() { return next_func_def_id_.fetch_add(1); }

  void set_next_func_call_id(int64_t val) { next_func_call_id_.store(val); }
  int64_t get_next_func_call_id() { return next_func_call_id_.fetch_add(1); }

 protected:
  IndexCtx() {}

 protected:
  size_t num_parse_tasks_ = 1;
  size_t num_store_tasks_ = 1;

  /**
   * CompilationDatabase detected from compile_commands.json
   */
  std::unique_ptr<clang::tooling::CompilationDatabase> cdb_;

  /**
   * cmake project root (i.e., the directory where the compile_commands.json
   * file locates).
   *
   * Project root should be absoluate path, so that we can use it to transform
   * absoluate source file path into relative path.
   */
  std::string project_root_ = "/";

  /**
   * Unique id associated with project_root_
   *
   * The project id is stored in a meta table in backed database.
   * For new project, a new project id is allocated starting from the current
   * max project id.
   *
   * Note that all project id must > 0.
   */
  int64_t project_id_ = -1;

  /**
   * MYSQL table name to store filepath/func_def/func_call, in form of
   *  filepaths_{project_id}
   *  func_definitions_{project_id}
   *  func_calls_{project_id}
   */
  std::string filepath_table_;
  std::string func_defs_table_;
  std::string func_calls_table_;

  /**
   * MYSQLMgr that manage internal mysqld subprocess
   */
  std::shared_ptr<MYSQLMgr> mysql_mgr_;
  /**
   * MYSQL Connection manager, which create connection object the the
   * internal/external mysqld database for tasks on demand.
   */
  std::unique_ptr<MYSQLConnMgr> mysql_conn_mgr_;
  /**
   * Global multi-producer/multi-consumer queue used to communicate index result
   * between the parse task and store task.
   *
   * Multiple parse tasks parse translation unit and store the parsed results
   * (as SQL statements) into the index queue.
   * Multiple store tasks acquire the parse results from the index queue and
   * then store them into the dedicated mysqld database.
   */
  std::unique_ptr<IndexQueue> index_queue_;

  std::atomic_int64_t next_filepath_id_ = 1;
  std::atomic_int64_t next_func_def_id_ = 1;
  std::atomic_int64_t next_func_call_id_ = 1;

  /**
   * SQL. table name formatted with project_id
   */
  /**
   * MYSQL table for filename
   */
  std::string sql_filepaths_create_;
  std::string sql_query_filepath_id_;
  std::string sql_replace_filepath_;
  std::string sql_is_file_exists_;
  std::string sql_get_file_mtime_;
  std::string sql_is_file_obselete_;
  std::string sql_max_filepath_id_;

  /**
   * MYSQL table for ClassDef
   */
  std::string sql_class_def_create_;

  /**
   * MYSQL table for FuncDef
   */
  std::string sql_func_def_create_;
  std::string sql_func_def_insert_header_;
  std::string sql_func_def_insert_param_;
  std::string sql_query_func_def_id_;
  std::string sql_query_func_def_usr_;
  std::string sql_query_func_def_using_qualified_;
  std::string sql_max_func_def_id_;

  /**
   * MYSQL table for FuncCall
   */
  std::string sql_func_call_create_;
  std::string sql_func_call_insert_header_;
  std::string sql_func_call_insert_param_;
  std::string sql_query_func_call_info_;
  std::string sql_query_func_call_info_with_throw_;
  std::string sql_query_caller_usr_using_qualified_;
  std::string sql_max_func_call_id_;
};

#define PCTX (IndexCtx::get_instance())
}  // namespace pview
