/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#pragma once

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fbzmq/zmq/Zmq.h>

namespace fbzmq {
namespace example {

class ZmqClient {
 public:
  ZmqClient(
      fbzmq::Context& zmqContext,
      const std::string& primitiveCmdUrl,
      const std::string& stringCmdUrl,
      const std::string& thriftCmdUrl,
      const std::string& multipleCmdUrl,
      const std::string& pubUrl);

  // start making requests of various types (primitive, string, thrift)
  void startRequests() noexcept;

 private:
  // Initialize ZMQ sockets
  void prepare() noexcept;

  // make primitive type request
  void makePrimitiveRequest() noexcept;

  // make string type request
  void makeStringRequest() noexcept;

  // make multiple request
  void makeMultipleRequest() noexcept;

  // make a KEY_SET request
  // return true on success, false on failure
  bool setKeyValue(const std::string& key, int64_t value) noexcept;

  // make a KEY_GET request
  // return true on success, false on failure
  bool getKey(const std::string& key, int64_t& value) noexcept;

  // make thrift type request
  void makeThriftRequest() noexcept;

  // ZMQ context reference
  fbzmq::Context& zmqContext_;

  // ZMQ communication urls
  const std::string primitiveCmdUrl_;
  const std::string stringCmdUrl_;
  const std::string thriftCmdUrl_;
  const std::string multipleCmdUrl_;
  const std::string pubUrl_;

  // subscriber socket
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> subSock_;

  // used for serialize/deserialize thrift obj
  apache::thrift::CompactSerializer serializer_;
};

} // namespace example
} // namespace fbzmq
