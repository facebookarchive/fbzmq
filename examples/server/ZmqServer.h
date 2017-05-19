/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#pragma once

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>

namespace fbzmq {
namespace example {

class ZmqServer final : public fbzmq::ZmqEventLoop {
 public:
  ZmqServer(
      fbzmq::Context& zmqContext,
      const std::string& primitiveCmdUrl,
      const std::string& stringCmdUrl,
      const std::string& thriftCmdUrl,
      const std::string& multipleCmdUrl,
      const std::string& pubUrl);

  // disable copying
  ZmqServer(const ZmqServer&) = delete;
  ZmqServer& operator=(const ZmqServer&) = delete;

 private:
  // Initialize ZMQ sockets
  void prepare() noexcept;

  // process received primitive type command
  void processPrimitiveCommand() noexcept;

  // process received string type command
  void processStringCommand() noexcept;

  // process received thrift type command
  void processThriftCommand() noexcept;

  // process received multiple command
  void processMultipleCommand() noexcept;

  // communication urls
  const std::string primitiveCmdUrl_;
  const std::string stringCmdUrl_;
  const std::string thriftCmdUrl_;
  const std::string multipleCmdUrl_;
  const std::string pubUrl_;

  // command socket for primitive type message
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> primitiveCmdSock_;
  // command socket for string type message
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> stringCmdSock_;
  // command socket for thrift type message
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> thriftCmdSock_;
  // command socket for multiple message
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> multipleCmdSock_;

  // publication socket
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pubSock_;

  // used for serialize/deserialize thrift obj
  apache::thrift::CompactSerializer serializer_;

  // key-value store
  std::unordered_map<std::string, int64_t> kvStore_;
};

} // namespace example
} // namespace fbzmq
