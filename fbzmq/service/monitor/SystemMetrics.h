// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <folly/Optional.h>
#include <glog/logging.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <chrono>
#include <fstream>
#include <regex>

namespace fbzmq {

/**
 * This class provides the API to get the system usage for monitoring,
 * including the CPU, memory usage, etc.
 */
class SystemMetrics {
 public:
  // get RSS memory the process used
  folly::Optional<size_t> getRSSMemBytes();

  // get CPU% the process used
  folly::Optional<double> getCPUpercentage();

 private:
  /**
  / To record CPU used time of current process (in nanoseconds)
  */
  typedef struct ProcCpuTime {
    uint64_t userTime = 0; /* CPU time used in user mode */
    uint64_t sysTime = 0; /*  CPU time used in system mode*/
    uint64_t totalTime = 0; /* total CPU time used */
    uint64_t timestamp = 0; /* timestamp for current record */
    ProcCpuTime(){}; // for initializing the prevCpuTime
    ProcCpuTime(struct rusage& usage)
        : userTime(
              usage.ru_utime.tv_sec * 1.0e9 + usage.ru_utime.tv_usec * 1.0e3),
          sysTime(
              usage.ru_stime.tv_sec * 1.0e9 + usage.ru_stime.tv_usec * 1.0e3),
          totalTime(userTime + sysTime),
          timestamp(getCurrentNanoTime()) {}
  } ProcCpuTime;

  // cache for CPU used time of previous query
  ProcCpuTime prevCpuTime;

  // get current timestamp (in nanoseconds)
  uint64_t static getCurrentNanoTime();
};

} // namespace fbzmq
