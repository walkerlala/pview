//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "PView.h"

namespace pview {
/******************************************************************************
 * Producer/consumer queue for storing and consuming SQL statements.
 *
 * The producer side:
 *    "Parse tasks" will index project files, generate SQL statements
 *    (INSERT or UPDATE statements), and then put these statements
 *    into the index queue using the add_stmt() interfaces.
 *    SQL statements are often grouped into batch to reduce overhead
 *    (i.e., INSERT INTO xxx VALUES (...), (...) ... (...); ).
 *
 * The consumer side:
 *    "Store tasks" will acquire these SQL statements and simply send them
 *    to the backend MYSQL server.
 *
 * There are usually more producer than consumer (see IndexCtx::set_max_dop()),
 * because:
 *  - Parse tasks do much more work than store tasks,
 *    e.g., indexing source file, formatting SQL stmt, etc.
 *  - Large number of store task means large number of mysql connection
 *    and large number of concurrent DML statements and will incur overhead
 *    when dealing with the backend MYSQL server (mysql have to deal with
 *    lock contention when inserting into adjacent pages).
 *    The best practice is to form a "connection pool" as in most database
 *    application.
 *
 * The IndexQueue also provide serveral synchronization mechanism in a simple
 * way:
 *
 * - Conjection control when there are too many stmts in queue:
 *   When there are too many stmts in queue, the producer will be put to wait
 *   at the add_stmt() interfaces until the # of stmts drop down to some
 *   threshold.
 ******************************************************************************/
class IndexQueue {
 public:
  IndexQueue() {}
  ~IndexQueue() = default;

  int init() { return 0; }

  /**
   * Consumer call this to get a batch of stmts.
   *
   * Consumer should discard subsequent stmt of the same vector if any previous
   * stmt failed, to guarantee happens-after-or-fail relationship between stmts.
   */
  std::unique_ptr<std::vector<std::string>> get_stmts();
  size_t get_num_stmts() { return num_stmts_.load(); }

  /**
   * Producer call this to add a batch of stmts.
   */
  void add_stmts(std::vector<std::string> &&input);

  void set_stop() {
    stop_.store(true);
    {
      std::unique_lock lk(stmt_consumer_mtx_);
      stmt_consumer_cv_.notify_all();
      stmt_producer_cv_.notify_all();
    }
  }

 protected:
  std::atomic<bool> stop_ = false;

  std::mutex stmt_consumer_mtx_;
  std::condition_variable stmt_consumer_cv_;
  std::condition_variable stmt_producer_cv_;
  /**
   * FIFO queue.
   *
   * All store thread took a single vector at a time.
   *
   * Each stmt inside a vector is send the db one-by-one, and if any stmt
   * failed, all subsequent stmts inside the same vector are discarded,
   * such that it guarantee happens-after-or-fail relationship between
   * all stmts.
   */
  std::queue<std::unique_ptr<std::vector<std::string>>> stmts_;
  /**
   * Num of SQL statement in queue
   */
  std::atomic_size_t num_stmts_ = 0;
};
}  // namespace pview
