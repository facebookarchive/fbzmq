/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/zmq/Common.h>

namespace fbzmq {

namespace detail {
// Forward declaration of SocketImpl friend class
class SocketImpl;
} // namespace detail

/**
 * RAII over zmq_context
 */
class Context {
 public:
  /**
   * Can optionally be configured with
   * - custom number of IO threads
   * - custom cap on number of sockets supported by context
   *
   * Default values are defined by libzmq which is 1 for IO threads and
   * 1024 for max number of sockets.
   */
  explicit Context(
      folly::Optional<uint16_t> numIoThreads = folly::none,
      folly::Optional<uint16_t> numMaxSockets = folly::none) noexcept;

  // non-copyable
  Context(Context const&) = delete;
  Context& operator=(Context const&) = delete;

  // movable
  Context(Context&&) noexcept;
  Context& operator=(Context&&) noexcept;
  ~Context();

 private:
  friend class detail::SocketImpl;

  // pointer to zmq context object
  void* ptr_{nullptr};
};

} // namespace fbzmq
