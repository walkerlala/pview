//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <memory>
#include <string>

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

  void set_project_root(const std::string &root) {
    ASSERT(root.size() && root[0] == '/');
    project_root_ = root;
  }
  const auto &get_project_root() const { return project_root_; }

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
   * cmake project root (i.e., the directory where the compile_commands.json
   * file locates).
   *
   * Project root should be absoluate path, so that we can use it to transform
   * absoluate source file path into relative path.
   */
  std::string project_root_ = "/";

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
};
}  // namespace pview
