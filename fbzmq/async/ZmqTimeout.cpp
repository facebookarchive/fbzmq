/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <fbzmq/async/ZmqTimeout.h>

namespace fbzmq {

namespace {

/**
 * Wraps a function object as an ZmqTimeout instance.
 */
struct ZmqTimeoutWrapper : public ZmqTimeout {
 public:
  ZmqTimeoutWrapper(ZmqEventLoop* eventLoop, TimeoutCallback callback)
      : ZmqTimeout(eventLoop), callback_(std::move(callback)) {}

  void
  timeoutExpired() noexcept override {
    callback_();
  }

 private:
  TimeoutCallback callback_;
};

} // anonymous namespace

ZmqTimeout::ZmqTimeout(ZmqEventLoop* eventLoop) : eventLoop_(eventLoop) {
  CHECK(eventLoop);
}

std::unique_ptr<ZmqTimeout>
ZmqTimeout::make(ZmqEventLoop* eventLoop, TimeoutCallback callback) {
  return std::unique_ptr<ZmqTimeout>(
      new ZmqTimeoutWrapper(eventLoop, std::move(callback)));
}

ZmqTimeout::~ZmqTimeout() {
  if (isScheduled()) {
    cancelTimeout();
  }
}

void
ZmqTimeout::scheduleTimeout(
    std::chrono::milliseconds timeoutPeriod, bool isPeriodic) {
  // Cancel already scheduled timeout if any
  if (isScheduled()) {
    cancelTimeout();
  }

  state_ = isPeriodic ? TimeoutState::PERIODIC : TimeoutState::SCHEDULED;
  timeoutPeriod_ = timeoutPeriod;
  token_ = eventLoop_->scheduleTimeout(
      timeoutPeriod_, [this]() noexcept { timeoutExpiredHelper(); });
}

void
ZmqTimeout::cancelTimeout() {
  if (state_ == TimeoutState::NONE) {
    LOG(WARNING) << "Trying to cancel timeout which is not scheduled.";
    return;
  }

  state_ = TimeoutState::NONE;
  eventLoop_->cancelTimeout(token_);
}

void
ZmqTimeout::timeoutExpiredHelper() noexcept {
  if (state_ == TimeoutState::PERIODIC) {
    token_ = eventLoop_->scheduleTimeout(
        timeoutPeriod_, [this]() noexcept { timeoutExpiredHelper(); });
  } else {
    state_ = TimeoutState::NONE;
  }

  // Invoke timeoutExpired callback.
  timeoutExpired();
}

} // namespace fbzmq
