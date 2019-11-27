/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace fbzmq {

class Runnable {
 public:
  virtual ~Runnable() = default;

  /**
   * Start thread execution. Ideally this function blocks and returns only when
   * stop is called .
   */
  virtual void run() = 0;

  /**
   * Implementation of signalling to running thread for stopping. This should
   */
  virtual void stop() = 0;

  /**
   * Indicates if thread is running or not
   */
  virtual bool isRunning() const = 0;

  /**
   * Busy-spin or wait until thread is running.
   */
  virtual void waitUntilRunning() = 0;

  /**
   * Busy-spin or wait until thread is stopped. Useful when you issued a stop
   * signal and want to wait before proceeding.
   */
  virtual void waitUntilStopped() = 0;
};

} // namespace fbzmq
