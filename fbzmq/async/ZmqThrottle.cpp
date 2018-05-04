/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/async/ZmqThrottle.h>

namespace fbzmq {

ZmqThrottle::ZmqThrottle(
    ZmqEventLoop* evl,
    std::chrono::milliseconds timeout,
    TimeoutCallback callback)
    : ZmqTimeout(evl),
      evl_(evl),
      timeout_(timeout),
      callback_(std::move(callback)) {
  CHECK(callback_);
}

void
ZmqThrottle::operator()() noexcept {
  // Must be called from within event loop
  CHECK(evl_->isInEventLoop());

  // Return immediately as callback is already scheduled.
  if (isScheduled()) {
    return;
  }

  // Special case to handle immediate timeouts
  if (timeout_ <= std::chrono::milliseconds(0)) {
    callback_();
    return;
  }

  scheduleTimeout(timeout_);
}

void
ZmqThrottle::timeoutExpired() noexcept {
  CHECK(evl_->isInEventLoop());
  callback_();
}

} // namespace fbzmq
