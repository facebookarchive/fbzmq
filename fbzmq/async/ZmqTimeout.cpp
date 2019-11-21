/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/async/ZmqTimeout.h>

namespace fbzmq {

namespace {

/**
 * Wraps a function object as an ZmqTimeout instance.
 */
struct ZmqTimeoutWrapper : public ZmqTimeout {
 public:
  ZmqTimeoutWrapper(
      folly::ScheduledExecutor* eventLoop, TimeoutCallback callback)
      : ZmqTimeout(eventLoop), callback_(std::move(callback)) {}

  void
  timeoutExpired() noexcept override {
    callback_();
  }

 private:
  TimeoutCallback callback_;
};

} // anonymous namespace

ZmqTimeout::ZmqTimeout(folly::ScheduledExecutor* eventLoop)
    : eventLoop_(eventLoop) {
  token_ = std::make_shared<size_t>(0);
  CHECK(eventLoop);
}

std::unique_ptr<ZmqTimeout>
ZmqTimeout::make(
    folly::ScheduledExecutor* eventLoop, TimeoutCallback callback) {
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
  scheduleTimeoutHelper();
}

void
ZmqTimeout::cancelTimeout() {
  if (state_ == TimeoutState::NONE) {
    LOG(WARNING) << "Trying to cancel timeout which is not scheduled.";
    return;
  }

  state_ = TimeoutState::NONE;
  ++(*token_); // Increment token
}

void
ZmqTimeout::scheduleTimeoutHelper() noexcept {
  ++(*token_); // Increment token
  eventLoop_->scheduleAt(
      // NOTE: copy absolute as well as reference to token. This is to guarantee
      // that token is valid memory even if ZmqTimeout is destroyed
      [this, tokenAbs = *token_, token = token_]() noexcept {
        if (tokenAbs == *token) {
          timeoutExpiredHelper();
        }
      },
      std::chrono::steady_clock::now() + timeoutPeriod_);
}

void
ZmqTimeout::timeoutExpiredHelper() noexcept {
  if (state_ == TimeoutState::PERIODIC) {
    scheduleTimeoutHelper();
  } else {
    state_ = TimeoutState::NONE;
  }

  // Invoke timeoutExpired callback.
  timeoutExpired();
}

} // namespace fbzmq
