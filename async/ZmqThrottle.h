/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <chrono>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>

namespace fbzmq {

/**
 * This class provides you capability to rate-limit certain events which might
 * happen rapidly in the system and processing of an event is expensive.
 *
 * For e.g. you want to `saveState()` on every `addKey` and `removeKey` but
 * saving state is expensive operation. You can do
 *
 *  auto throttledSaveState = ZmqThrottle(evl, 1_s, [this] () noexcept {
 *    saveState();
 *  });
 *
 *  And then call `throttledSaveState()` on every `addKey` and `removeKey` but
 *  internally `saveState()` will be execute at max once per second.
 */
class ZmqThrottle final : private ZmqTimeout {
 public:
  ZmqThrottle(
      ZmqEventLoop* evl,
      std::chrono::milliseconds timeout,
      TimeoutCallback callback);

  ~ZmqThrottle() = default;

  /**
   * Overload function operator. This method exposes throttled version of
   * callback passed in.
   */
  void operator()() noexcept;

  /**
   * Tells you if this is currently active ?
   */
  bool isActive() const {
    CHECK(evl_->isInEventLoop());
    return isScheduled();
  }

 private:

  /**
   * Overrides ZmqTimeout's timeout callback
   */
  void timeoutExpired() noexcept override;

  const ZmqEventLoop* evl_{nullptr};
  const std::chrono::milliseconds timeout_{0};
  TimeoutCallback callback_{nullptr};
};

} // namespace fbzmq
