/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <boost/serialization/strong_typedef.hpp>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Optional.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "ZmqMonitor.h"

namespace fbzmq {

/**
 * This class abstracts out many client side operations of ZmqMonitor into
 * very simple APIs to use.
 * 1. Advertise name/counter into local ZmqMonitor
 * 2. Access content of ZmqMonitor (name-counters)
 *
 */
class ZmqMonitorClient {
 public:
  /**
   * Creates and initializes all necessary sockets for communicating with
   * ZmqMonitor.
   */
  ZmqMonitorClient(
      fbzmq::Context& zmqContext,
      std::string const& monitorCmdUrl,
      std::string const& socketId = "");

  //
  // Synchronous wrapper calls around ZmqMonitor
  // throw zmq exception upon error
  //

  /**
   * Add name-counter(s) into ZmqMonitor
   */
  void setCounter(std::string const& name, thrift::Counter const& counter);
  void setCounters(const CounterMap& counters);

  /**
   * Get name from ZmqMonitor.
   */
  folly::Optional<thrift::Counter> getCounter(std::string const& name);

  /**
   * Dump all counter names in ZmqMonitor.
   */
  std::vector<std::string /* name */> dumpCounterNames();

  /**
   * Dump ZmqMonitor.
   */
  CounterMap dumpCounters();

  /**
   * Bump counter.
   */
  void bumpCounter(std::string const& name);

  /**
   * Add an event log.
   */
  void addEventLog(thrift::EventLog const& eventLog);

  /**
   * Get last event logs from ZmqMonitor
   */
  folly::Optional<std::vector<thrift::EventLog>> getLastEventLogs();

 private:
  //
  // Mutable state
  //

  const std::string monitorCmdUrl_;

  // DEALER socket
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> monitorCmdSock_;

  // Serializer object for thrift-obj <-> string conversion
  apache::thrift::CompactSerializer serializer_;
};

} // namespace fbzmq
