/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
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
