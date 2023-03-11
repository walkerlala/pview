//=---------------------------------------------------------------------------=/
// Copyright The pview authors
// SPDX-License-Identifier: Apache-2.0
//=---------------------------------------------------------------------------=/
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "IndexQueue.h"

using std::make_unique;

DECLARE_int64(verbose);

constexpr size_t kTooManyInfoWarnThreshold = 2048;
constexpr size_t kTooManyInfoWaitThreshold = 2048 * 10;
constexpr size_t kProducerWaitExitThreshold = 2048 * 6;
std::atomic_uint64_t g_lower_than_producer_times = 0;

namespace pview {
std::unique_ptr<std::vector<std::string>> IndexQueue::get_stmts() {
  auto break_cond = [&]() {
    bool have_info = stmts_.size();
    return (have_info || stop_);
  };

  std::unique_lock lk(stmt_consumer_mtx_);
  stmt_consumer_cv_.wait(lk, break_cond);

  if (!stmts_.size()) {
    return nullptr;
  }

  auto res = std::move(stmts_.front());
  stmts_.pop();
  size_t num_sql = 0;
  if (res) {
    num_sql = res->size();
  }
  num_stmts_.fetch_sub(num_sql);

  if (stmts_.size() >= kTooManyInfoWarnThreshold) {
    LOG_EVERY_N(WARNING, 100)
        << "Still too many statements in queue: " << stmts_.size();
  }

  if (stmts_.size() < kProducerWaitExitThreshold) {
    stmt_producer_cv_.notify_all();
  } else if (stmts_.size() < kTooManyInfoWaitThreshold) {
    uint64_t lower_times = g_lower_than_producer_times.fetch_add(1);
    if (lower_times % 1024 == 0) {
      LOG(INFO) << "Wakeup one producer";
      stmt_producer_cv_.notify_one();
    }
  }
  return res;
}

void IndexQueue::add_stmts(std::vector<std::string> &&input) {
  std::unique_lock lk(stmt_consumer_mtx_);

  size_t num_sql = input.size();
  stmts_.push(make_unique<std::vector<std::string>>(std::move(input)));
  input.clear();
  num_stmts_.fetch_add(num_sql);

  if (stmts_.size() >= kTooManyInfoWarnThreshold) {
    LOG_IF_EVERY_N(WARNING, FLAGS_verbose, 100)
        << "Too many func defs in queue: " << stmts_.size();
  }
  stmt_consumer_cv_.notify_one();

  if (stmts_.size() >= kTooManyInfoWaitThreshold) {
    LOG_EVERY_N(WARNING, 100)
        << "Too many func defs in queue: " << stmts_.size()
        << ", wait until lower than threshold.";

    auto break_cond = [&]() {
      bool stmt_less = (stmts_.size() < kTooManyInfoWaitThreshold);
      return (stmt_less || stop_);
    };
    stmt_producer_cv_.wait(lk, break_cond);
  }
}
}  // namespace pview
