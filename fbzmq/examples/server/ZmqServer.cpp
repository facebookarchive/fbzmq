/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#include <fbzmq/examples/server/ZmqServer.h>

#include <fbzmq/examples/common/Constants.h>
#include <fbzmq/examples/if/gen-cpp2/Example_types.h>

namespace fbzmq {
namespace example {

ZmqServer::ZmqServer(
    fbzmq::Context& zmqContext,
    const std::string& primitiveCmdUrl,
    const std::string& stringCmdUrl,
    const std::string& thriftCmdUrl,
    const std::string& multipleCmdUrl,
    const std::string& pubUrl)
    : primitiveCmdUrl_(primitiveCmdUrl),
      stringCmdUrl_(stringCmdUrl),
      thriftCmdUrl_(thriftCmdUrl),
      multipleCmdUrl_(multipleCmdUrl),
      pubUrl_(pubUrl),
      // init sockets
      primitiveCmdSock_(zmqContext),
      stringCmdSock_(zmqContext),
      thriftCmdSock_(zmqContext),
      multipleCmdSock_(zmqContext),
      pubSock_(zmqContext) {
  prepare();
}

void
ZmqServer::prepare() noexcept {
  LOG(INFO) << "Server: Binding primitiveCmdUrl_ '" << primitiveCmdUrl_ << "'";
  primitiveCmdSock_.bind(fbzmq::SocketUrl{primitiveCmdUrl_}).value();

  LOG(INFO) << "Server: Binding stringCmdUrl_ '" << stringCmdUrl_ << "'";
  stringCmdSock_.bind(fbzmq::SocketUrl{stringCmdUrl_}).value();

  LOG(INFO) << "Server: Binding thriftCmdUrl_ '" << thriftCmdUrl_ << "'";
  thriftCmdSock_.bind(fbzmq::SocketUrl{thriftCmdUrl_}).value();

  LOG(INFO) << "Server: Binding multipleCmdUrl_ '" << multipleCmdUrl_ << "'";
  multipleCmdSock_.bind(fbzmq::SocketUrl{multipleCmdUrl_}).value();

  // attach callbacks for primitive type command
  addSocket(
      fbzmq::RawZmqSocketPtr{*primitiveCmdSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {
        LOG(INFO) << "Received command request on primitiveCmdSock_";
        processPrimitiveCommand();
      });

  // attach callbacks for string type command
  addSocket(
      fbzmq::RawZmqSocketPtr{*stringCmdSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {
        LOG(INFO) << "Received command request on stringCmdSock_";
        processStringCommand();
      });

  // attach callbacks for thrift type command
  addSocket(
      fbzmq::RawZmqSocketPtr{*thriftCmdSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {
        LOG(INFO) << "Received command request on thriftCmdSock_";
        processThriftCommand();
      });

  // attach callbacks for multiple command
  addSocket(
      fbzmq::RawZmqSocketPtr{*multipleCmdSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {
        LOG(INFO) << "Received command request on multipleCmdSock_";
        processMultipleCommand();
      });
}

void
ZmqServer::processPrimitiveCommand() noexcept {
  // recv request
  auto maybeMsg = primitiveCmdSock_.recvOne();
  if (maybeMsg.hasError()) {
    LOG(ERROR) << "recv primitive request failed: " << maybeMsg.error();
    return;
  }

  // read out primitive (uin32_t in this example) request
  auto maybeUint32t = maybeMsg->template read<uint32_t>();
  if (maybeUint32t.hasError()) {
    LOG(ERROR) << "read primitive request failed: " << maybeUint32t.error();
    return;
  }

  uint32_t request = maybeUint32t.value();
  VLOG(2) << "received request: " << request;

  // process request (simply add one)
  uint32_t reply = request + 1;

  // send back reply message
  auto replyMsg = fbzmq::Message::from(reply).value();
  VLOG(2) << "sending reply: " << reply;
  auto rc = primitiveCmdSock_.sendOne(replyMsg);
  if (rc.hasError()) {
    LOG(ERROR) << "send reply failed: " << rc.error();
  }
}

void
ZmqServer::processStringCommand() noexcept {
  // recv request
  auto maybeMsg = stringCmdSock_.recvOne();
  if (maybeMsg.hasError()) {
    LOG(ERROR) << "recv string request failed: " << maybeMsg.error();
    return;
  }

  // read out string request
  auto maybeString = maybeMsg.value().template read<std::string>();
  if (maybeString.hasError()) {
    LOG(ERROR) << "read string request failed: " << maybeString.error();
    return;
  }

  const auto& request = maybeString.value();
  VLOG(2) << "received request: " << request;

  // process request (simply append " world")
  std::string reply = request + " world";
  // send back reply message
  auto replyMsg = fbzmq::Message::from(reply).value();
  VLOG(2) << "sending reply: " << reply;
  auto rc = stringCmdSock_.sendOne(replyMsg);
  if (rc.hasError()) {
    LOG(ERROR) << "send reply failed: " << rc.error();
  }
}

void
ZmqServer::processMultipleCommand() noexcept {
  fbzmq::Message msg1, msg2, msg3;

  // recv request
  multipleCmdSock_.recvMultiple(msg1, msg2, msg3).value();

  auto intMsg = msg1.read<uint32_t>();
  if (intMsg.hasError()) {
    LOG(ERROR) << "read int request failed: " << intMsg.error();
    return;
  }

  auto stringMsg = msg2.read<std::string>();
  if (stringMsg.hasError()) {
    LOG(ERROR) << "read string request failed: " << stringMsg.error();
    return;
  }

  auto thriftMsg = msg3.readThriftObj<thrift::StrValue>(serializer_);
  if (thriftMsg.hasError()) {
    LOG(ERROR) << "read thrift request failed: " << thriftMsg.error();
    return;
  }

  std::string reply = folly::sformat(
      "Commands received are: Number `{}`, String `{}` and ThriftObj `{}`",
      intMsg.value(),
      stringMsg.value(),
      *thriftMsg.value().value_ref());

  // send back reply message
  auto replyMsg = fbzmq::Message::from(reply).value();
  VLOG(2) << "sending reply: " << reply;
  auto rc = multipleCmdSock_.sendOne(replyMsg);
  if (rc.hasError()) {
    LOG(ERROR) << "send reply failed: " << rc.error();
  }
}

void
ZmqServer::processThriftCommand() noexcept {
  // read out thrift command
  auto maybeThriftObj =
      thriftCmdSock_.recvThriftObj<thrift::Request>(serializer_);
  if (maybeThriftObj.hasError()) {
    LOG(ERROR) << "read thrift request failed: " << maybeThriftObj.error();
    return;
  }

  const auto& request = maybeThriftObj.value();
  const auto& key = request.key;
  const auto value = request.value_ref();

  switch (request.cmd) {
  case thrift::Command::KEY_SET: {
    VLOG(2) << "Received KEY_SET command (" << key << ": " << *value << ")";
    kvStore_[key] = *value;

    thrift::Response response;
    response.success = true;
    auto rc = thriftCmdSock_.sendThriftObj(response, serializer_);
    if (rc.hasError()) {
      LOG(ERROR) << "Sent response failed: " << rc.error();
      return;
    }
    break;
  }

  case thrift::Command::KEY_GET: {
    VLOG(2) << "Received KEY_GET command (" << key << ")";
    auto it = kvStore_.find(key);

    thrift::Response response;
    if (it == kvStore_.end()) {
      response.success = false;
    } else {
      response.success = true;
      response.value_ref() = it->second;
    }

    auto rc = thriftCmdSock_.sendThriftObj(response, serializer_);
    if (rc.hasError()) {
      LOG(ERROR) << "Sent response failed: " << rc.error();
      return;
    }
    break;
  }

  default: {
    LOG(ERROR) << "Unknown thrift command: " << static_cast<int>(request.cmd);
    break;
  }
  }

  for (const auto& keyVal : kvStore_) {
    VLOG(2) << "key: " << keyVal.first << " -> value: " << keyVal.second;
  }
}

} // namespace example
} // namespace fbzmq
