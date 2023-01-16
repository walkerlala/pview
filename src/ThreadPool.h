#pragma once

#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_gettid
#error "SYS_gettid unavailable on this system"
#endif

#define gettid() ((pid_t)syscall(SYS_gettid))

#include <cassert>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Common.h"
#include "Timer.h"

namespace pview {
constexpr size_t kDefaultGroupId = 0;

struct ThreadStat {
  ThreadStat() {}
  ThreadStat(const ThreadStat &stat) = default;

  nano_t begin;
  nano_t end;

  uint64_t task_id = 0;
  uint64_t task_group_id = 0;
  pid_t task_parent = 0;
  pid_t thread_id = 0;
  std::string task_name;
};

class ThreadPool final {
 public:
  static ThreadPool &GlobalPool();
  static std::mutex &AddTaskGroupMutex();
  static std::string GetTaskDesc();

  //
  // User task to be executed
  //
  struct Task {
    Task() {}
    Task(uint64_t id, uint64_t gid, pid_t parent, std::function<void(pid_t)> f)
        : task_id(id), task_group_id(gid), task_parent(parent), func(f) {}
    uint64_t task_id;
    uint64_t task_group_id;
    pid_t task_parent;
    std::function<void(pid_t)> func;
  };

  //
  // Thread local struct to describe the status of a worker
  //
  struct WorkerStatus {
    // whether this worker is running/active
    bool running;

    // globally incremented id when a new worker is created
    size_t worker_id;

    // task id of the current running task
    size_t task_id;

    // group id of the current running task
    size_t task_group_id;

    // parent thread id.
    // Parent thread is the thread that created the running task.
    pid_t task_parent;

    // pthread id of the underlying pthread that is running this task.
    pid_t thread_id;
  };

  //
  // Worker of thread pool
  //
  struct Worker {
    Worker() = delete;

    Worker(Worker &&rhs) : worker_thread(std::move(rhs.worker_thread)) {}

    template <typename T>
    Worker(const T &thread_func) : worker_thread(thread_func) {}

    template <typename T>
    Worker(T &&thread_func) : worker_thread(thread_func) {}

    void Join() {
      worker_thread.join();
      joined = true;
    }
    ~Worker() { assert(joined); }

    std::thread worker_thread;
    bool joined = false;
  };

  size_t size() const { return initial_size_; }

  // check if the calling thread is a worker of this thread pool
  bool in_worker_thread() const { return tp_owner == this; }

  // Add task with task_id and task_group_id, and notify worker.
  template <class F, class... Args>
  std::future<ThreadStat> add_task(uint64_t id, uint64_t gid, F &&f,
                                   Args &&...args) {
    pid_t task_parent = gettid();
    auto wrapped_func = [f, id, gid, task_parent, args...](pid_t thread_id) {
      ThreadStat stat;
      stat.task_id = id;
      stat.task_group_id = gid;
      stat.thread_id = thread_id;
      stat.task_parent = task_parent;
      stat.begin = GetNanoSecSinceEpoch();
      stat.end = GetNanoSecSinceEpoch();
      auto func = std::bind(f, args...);
      func();
      stat.end = GetNanoSecSinceEpoch();
      return stat;
    };

    auto task =
        std::make_shared<std::packaged_task<ThreadStat(pid_t)>>(wrapped_func);

    auto res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      ASSERT(!stopped, "Adding task on stopped thread pool");
      tasks.emplace(id, gid, task_parent,
                    [task](pid_t thread_id) { (*task)(thread_id); });
    }
    queue_condition.notify_one();
    return res;
  }

  template <class F, class... Args>
  std::future<ThreadStat> add_task(uint64_t task_id, F &&f, Args &&...args) {
    return add_task(task_id, kDefaultGroupId, std::move(f), std::move(args)...);
  }

  size_t GetTaskId() const {
    return in_worker_thread() ? worker_status.task_id : 0u;
  }
  size_t GetWorkerId() const { return worker_status.worker_id; }

 public:
  thread_local static WorkerStatus worker_status;

 private:
  ThreadPool(const std::string &name, size_t sz);
  ThreadPool(const ThreadPool &) = delete;
  void operator=(const ThreadPool &) = delete;
  ~ThreadPool();

  // Create a new worker
  Worker *CreateWorker();

  // Append a (usually new) worker to the end of the worker vector
  void AppendWorker(Worker *worker);

 private:
  thread_local static ThreadPool *tp_owner;

  // Name of the thread pool. Worker of this thread pool will has a name of
  // name[${worker_id}]
  const std::string name_;

  // Initialize size when this thread pool is initialized. This is the expected
  // size of thread pool. There may be moments where there are more worker than
  // initial_size_, but eventually the number of active worker will shrink back
  // to initial_size_ (others become "sleeping worker")
  const size_t initial_size_;

  // globally unique worker id.
  std::atomic_uint64_t next_worker_id_;

  std::vector<Worker *> workers_;
  std::mutex workers_mutex_;

  std::queue<Task> tasks;
  std::mutex queue_mutex;
  std::condition_variable queue_condition;
  std::atomic_int stopped;
};

// shortcut for waiting for a group of task to finish
template <typename T>
class result_set final {
  using FT = typename std::future<T>;

 public:
  result_set() {}
  DISALLOW_COPY_AND_ASSIGN(result_set<T>);

  void clear() { results.clear(); }
  void insert(FT &&v) { results.push_back(std::forward<FT>(v)); }
  size_t size() const { return results.size(); }

  void get_all_with_except(std::vector<T> &ret) {
    for (auto &&result : results) {
      try {
        ret.emplace_back(result.get());
      } catch (std::exception &e) {
        LOG(ERROR) << "Unhandled exception: " << e.what();
        ASSERT(false);
      } catch (...) {
        ASSERT(false);
      }
    }
  }

 private:
  std::vector<FT> results;
};
}  // namespace pview
