/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Function.h>

#include <fbzmq/zmq/Socket.h>

namespace fbzmq {

enum class SocketMonitorMessage {
  STARTED,
  CONNECTED,
  CONNECT_DELAYED,
  CONNECT_RETRIED,
  LISTENING,
  BIND_FAILED,
  ACCEPTED,
  ACCEPT_FAILED,
  CLOSED,
  CLOSE_FAILED,
  DISCONNECTED,
  HANDSHAKE_FAILED_NO_DETAIL,
  HANDSHAKE_SUCCEEDED,
  HANDSHAKE_FAILED_PROTOCOL,
  HANDSHAKE_FAILED_AUTH,
};

/**
 * Socket monitor creates new PAIR socket that observes even on monitored
 * socket and reports it down. It runs synchronous loop and invokes the
 * callback
 */
class SocketMonitor {
 public:
  // the function we call on events
  using CallbackT = folly::Function<void(SocketMonitorMessage, SocketUrl)>;

  /**
   * Monitor event on socket. The intended use case it to have this running
   * in a different thread, hence the run() loop is blocking (per the zmq
   * man page recommendations)
   *
   * @param sock The socket to monitor for events
   * @param monitoUrl The URL to use for transient PAIR socket. This has to be
   *    inproc:// only
   * @param cb The callback to invoke on every event. Will be invoked in
   *    the monitoring thread
   */
  SocketMonitor(
      detail::SocketImpl const& sock,
      SocketUrl monitorUrl,
      CallbackT cb) noexcept;

  /**
   * non-copyable
   */
  SocketMonitor(SocketMonitor const&) = delete;
  SocketMonitor& operator=(SocketMonitor const&) = delete;

  /**
   * Starts the monitoring loop, until the monitored socket closes. this
   * should run in a new thread to avoid blocking
   */
  folly::Expected<folly::Unit, Error> runForever() noexcept;

  /**
   * Iterate monitoring loop to consume single event. Usually helpful with
   * external event loops. Return true if socket is still being monitored.
   * false if monitoring has finished (monitored socket has been closed)
   */
  folly::Expected<bool, Error> runOnce() noexcept;

  /**
   * return raw pointer to the pair socket so it could be added to event loops
   */
  uintptr_t operator*() {
    return reinterpret_cast<uintptr_t>(pairSock_.ptr_);
  }

 private:
  /**
   * event object passed down the PAIR socket to monitor class
   */
  using EventT = struct {
    uint16_t event;
    int32_t data;
  };

  // this socket will be used to report monitored socket events
  Socket<ZMQ_PAIR, ZMQ_CLIENT> pairSock_;

  // we'll call this method on any event
  CallbackT cb_;
};

} // namespace fbzmq
