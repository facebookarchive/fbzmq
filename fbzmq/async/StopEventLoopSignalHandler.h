/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <fbzmq/async/AsyncSignalHandler.h>

namespace fbzmq {

/**
 * A commom signal handler which stops the underlying zmq event loop upon signal
 * catching for graceful exit. Use AsyncSignalHandler directly if you intend to
 * do more.
 */
class StopEventLoopSignalHandler final : public AsyncSignalHandler {
 public:
  explicit StopEventLoopSignalHandler(ZmqEventLoop* evl)
      : AsyncSignalHandler(evl) {}

 private:
  void
  signalReceived(int sig) noexcept override {
    LOG(INFO) << "Received signal: " << sig << ". Stopping event loop ...";
    auto evl = getZmqEventLoop();
    evl->stop();
  }
};

} // namespace fbzmq
