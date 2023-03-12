#include <cassert>
#include <mutex>
#include <string>

#include <glog/logging.h>

#include "ThreadPool.h"

namespace pview {
thread_local ThreadPool *ThreadPool::tp_owner = nullptr;
thread_local ThreadPool::WorkerStatus ThreadPool::worker_status = {0};

ThreadPool &ThreadPool::GlobalPool() {
  const size_t num_workers = std::thread::hardware_concurrency() * 3;
  static ThreadPool global_pool_("GlobalPool", num_workers);
  return global_pool_;
}

std::mutex &ThreadPool::AddTaskGroupMutex() {
  static std::mutex add_task_group_mutex;
  return add_task_group_mutex;
}

std::string ThreadPool::GetTaskDesc() {
  auto &thread_pool = ThreadPool::GlobalPool();
  if (thread_pool.in_worker_thread()) {
    std::ostringstream oss;
    oss << "Task[ " << thread_pool.GetWorkerId() << "]";
    return oss.str();
  } else {
    return "MAIN_THREAD";
  }
}

ThreadPool::ThreadPool(const std::string &name, size_t sz)
    : name_(name), initial_size_(sz), next_worker_id_(0), stopped(false) {
  for (size_t i = 0; i < initial_size_; ++i) {
    Worker *worker = CreateWorker();
    AppendWorker(worker);
  }
}

ThreadPool::Worker *ThreadPool::CreateWorker() {
  auto thread_func = [this]() {
    tp_owner = this;
    pid_t pid = gettid();
    uint64_t worker_id = this->next_worker_id_.fetch_add(1);

    worker_status.thread_id = pid;
    worker_status.running = false;
    worker_status.worker_id = worker_id;

    // thread name is restricted to 16 characters, including the terminating
    // null byte ('\0').
    std::string suffix = "[" + std::to_string(worker_id) + "]";
    std::string tname = this->name_.substr(0, 15 - suffix.size()) + suffix;
    if (::pthread_setname_np(pthread_self(), tname.c_str()) != 0) {
      ASSERT(false, "failed to set thread name");
    }

    while (true) {
      {
        Task task;
        {
          std::unique_lock<std::mutex> lock(queue_mutex);
          queue_condition.wait(
              lock, [this] { return this->stopped || !this->tasks.empty(); });
          if (stopped && tasks.empty()) return;
          task = std::move(tasks.front());
          tasks.pop();
        }

        // set thread local variables
        worker_status.task_id = task.task_id;
        worker_status.task_group_id = task.task_group_id;
        worker_status.task_parent = task.task_parent;
        worker_status.running = true;

        task.func(pid);
      }
      worker_status.running = false;
    }
  };

  Worker *worker = new Worker(std::move(thread_func));
  return worker;
}

void ThreadPool::AppendWorker(Worker *worker) {
  std::unique_lock lk(workers_mutex_);
  workers_.push_back(worker);
}

ThreadPool::~ThreadPool() {
  stopped.store(true);
  queue_condition.notify_all();

  for (Worker *&worker : workers_) {
    worker->Join();
    delete worker;
    worker = nullptr;
  }
  workers_.clear();
}
}  // namespace pview
