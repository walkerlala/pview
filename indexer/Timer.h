#pragma once

#include <sys/time.h>
#include <time.h>
#include <string>

#include "./Common.h"

static const int64_t kNanoSec_MicroSec = 1000;
static const int64_t kNanoSec_MilliSec = 1000000;
static const int64_t kNanoSec_Sec = 1000000000ll;
static const int64_t kMicroSec_MilliSec = 1000;
static const int64_t kMicroSec_Sec = 1000000;

struct nano_t {
  int64_t val = 0;
  nano_t operator-(const nano_t &right) const {
    int64_t v = this->val - right.val;
    return nano_t{v};
  }
  bool operator<(const nano_t &right) const { return this->val < right.val; }
  bool operator>(const nano_t &right) const { return !this->operator<(right); }
  template <typename T = int64_t>
  T GetNanoSec() const {
    return static_cast<T>(val);
  }
  template <typename T = int64_t>
  T GetMicroSec() const {
    return static_cast<T>(val) / kNanoSec_MicroSec;
  }
  template <typename T = int64_t>
  T GetMilliSec() const {
    return static_cast<T>(val) / kNanoSec_MilliSec;
  }
  template <typename T = int64_t>
  T GetSec() const {
    return static_cast<T>(val) / kNanoSec_Sec;
  }
};

struct micro_t {
  int64_t val = 0;
  micro_t operator-(const micro_t &right) const {
    int64_t v = this->val - right.val;
    return micro_t{v};
  }
  bool operator<(const micro_t &right) const { return this->val < right.val; }
  bool operator>(const micro_t &right) const { return !this->operator<(right); }
  template <typename T = int64_t>
  T GetNanoSec() const {
    return static_cast<T>(val) * kNanoSec_MicroSec;
  }
  template <typename T = int64_t>
  T GetMicroSec() const {
    return static_cast<T>(val);
  }
  template <typename T = int64_t>
  T GetMilliSec() const {
    return static_cast<T>(val) / kMicroSec_MilliSec;
  }
  template <typename T = int64_t>
  T GetSec() const {
    return static_cast<T>(val) / kMicroSec_Sec;
  }
};

static inline nano_t GetNanoSecSinceEpoch() {
  timespec tv;
  clock_gettime(CLOCK_REALTIME, &tv);
  int64_t val = static_cast<int64_t>(tv.tv_sec) * kNanoSec_Sec + tv.tv_nsec;
  return nano_t{val};
}

static inline micro_t GetMicroSecSinceEpoch() {
  timeval tv;
  gettimeofday(&tv, NULL);
  int64_t val = static_cast<int64_t>(tv.tv_sec) * kMicroSec_Sec + tv.tv_usec;
  return micro_t{val};
}

class Timer {
 public:
  DISALLOW_COPY_AND_ASSIGN(Timer);
  Timer(double start_immediately = true) : started_(false) {
    if (start_immediately) {
      Start();
    }
  }

  void Start() {
    if (!started_) {
      start_ = GetMicroSecSinceEpoch();
      current_time_ = start_;
      started_ = true;
    }
  }
  bool IsStarted() const { return started_; }

  /** duration (from start) in seconds */
  double Duration() const {
    if (!started_) return 0.0;

    micro_t duration = GetMicroSecSinceEpoch() - start_;
    return duration.GetSec<double>();
  }

  /** duration (from start) in micro-seconds (i.e., us) */
  uint64_t DurationMicroseconds() const {
    if (!started_) return 0;

    micro_t duration = GetMicroSecSinceEpoch() - start_;
    return duration.GetMicroSec<uint64_t>();
  }

  /** interval (from previous call) in seconds */
  double Interval() {
    if (!started_) return 0.0;

    micro_t old_time = current_time_;
    current_time_ = GetMicroSecSinceEpoch();

    micro_t duration = current_time_ - old_time;

    return duration.GetSec<double>();
  }

  uint64_t IntervalMicroseconds() {
    if (!started_) return 0;

    micro_t old_time = current_time_;
    current_time_ = GetMicroSecSinceEpoch();

    micro_t duration = current_time_ - old_time;

    return duration.GetMicroSec<uint64_t>();
  }

 private:
  bool started_;
  micro_t start_;
  micro_t current_time_;
};
