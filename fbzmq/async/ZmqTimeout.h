/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>

#include <folly/executors/ScheduledExecutor.h>

namespace fbzmq {

/**
 * ZmqTimeout wraps some basic functionality of scheduling async calls. It is
 * very similar to folly::AsyncTimeout but instead of running in EventBase this
 * run in ZmqEventLoop.
 *
 * There are multiple ways you can use this
 * 1. Inherit this class and implement the timeoutExpired
 * 2. Use `make` function to create and schedule multiple timeouts
 *
 */
class ZmqTimeout {
 public:
  /**
   * Construct a ZmqTimeout with eventLoop instance. All callbacks/timeouts will
   * be invoked in eventLoop's main loop. Sub-class must implement
   * `timeoutExpired`.
   */
  explicit ZmqTimeout(folly::ScheduledExecutor* eventLoop);

  /**
   * This construct and returns you the ZmqTimeout with specified function as
   * a callback.
   *
   *  timeout_ = ZmqTimeout::make(thread_, [] () {
   *    LOG(INFO) << "Timeout got expired.";
   *  });
   *
   *  timeout_->scheduleTimeout(std::chrono::seconds(1));   // or
   *  timeout_->schedulePeriodic(std::chrono::seconds(1));
   */
  static std::unique_ptr<ZmqTimeout> make(
      folly::ScheduledExecutor* eventLoop, TimeoutCallback callback);

  /**
   * Timeout will be automatically cancelled if it is running.
   */
  virtual ~ZmqTimeout();

  /**
   * non-copyable and non-movable
   */
  ZmqTimeout(ZmqTimeout const&) = delete;
  ZmqTimeout& operator=(ZmqTimeout const&) = delete;

  /**
   * This function must be implemented by sub-classes.
   */
  virtual void timeoutExpired() noexcept = 0;

  /**
   * Schedule timeout which will be invoked after specified duration in the
   * eventLoop's loop.
   *
   * If timeout is already scheduled then it will be re-scheduled with new
   * timeout value.
   *
   * If periodic flag is passed then timeoutExpired will be invoked periodically
   * with specified timeout until timeout is cancelled.
   */
  void scheduleTimeout(
      std::chrono::milliseconds timeoutPeriod, bool isPeriodic = false);

  /**
   * Cancel already scheduled timeout, if it is running.
   */
  void cancelTimeout();

  /**
   * Is this function currently scheduled. Will always be true if scheduled
   * periodically.
   */
  bool
  isScheduled() const {
    return state_ != TimeoutState::NONE;
  }

  /**
   * Is this timeout scheduled periodically.
   */
  bool
  isPeriodic() const {
    return state_ == TimeoutState::PERIODIC;
  }

 private:
  /**
   * Helper function to attach timeout
   */
  void scheduleTimeoutHelper() noexcept;

  /**
   * Helper function which calls timeoutExpired.
   */
  void timeoutExpiredHelper() noexcept;

  enum class TimeoutState {
    NONE = 1,
    SCHEDULED = 2,
    PERIODIC = 3,
  };

  // ScheduledExecutor instance in which to run/schedule the timeouts
  folly::ScheduledExecutor* eventLoop_{nullptr};

  // Current timeout state
  TimeoutState state_{TimeoutState::NONE};

  // Token associated with the scheduled timeout.
  std::shared_ptr<size_t> token_;

  // Timeout duration associated with periodic timeout
  std::chrono::milliseconds timeoutPeriod_{0};
};

} // namespace fbzmq
