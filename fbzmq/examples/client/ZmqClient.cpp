/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#include <fbzmq/examples/client/ZmqClient.h>

#include <folly/Random.h>

#include <fbzmq/examples/common/Constants.h>
#include <fbzmq/examples/if/gen-cpp2/Example_types.h>

namespace fbzmq {
namespace example {

ZmqClient::ZmqClient(
    fbzmq::Context& zmqContext,
    const std::string& primitiveCmdUrl,
    const std::string& stringCmdUrl,
    const std::string& thriftCmdUrl,
    const std::string& multipleCmdUrl,
    const std::string& pubUrl)
    : zmqContext_(zmqContext),
      primitiveCmdUrl_(primitiveCmdUrl),
      stringCmdUrl_(stringCmdUrl),
      thriftCmdUrl_(thriftCmdUrl),
      multipleCmdUrl_(multipleCmdUrl),
      pubUrl_(pubUrl),
      // init sockets
      subSock_(zmqContext) {
  prepare();
}

void
ZmqClient::startRequests() noexcept {
  makePrimitiveRequest();
  makeStringRequest();
  makeThriftRequest();
  makeMultipleRequest();
}

void
ZmqClient::prepare() noexcept {
  LOG(INFO) << "Client connecting pubUrl_ '" << pubUrl_ << "'";
  subSock_.connect(fbzmq::SocketUrl{pubUrl_}).value();
}

void
ZmqClient::makePrimitiveRequest() noexcept {
  uint32_t request = folly::Random::rand32() % 100;
  auto const msg = fbzmq::Message::from(request).value();

  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> reqSock{zmqContext_};
  reqSock.connect(fbzmq::SocketUrl{primitiveCmdUrl_}).value();

  LOG(INFO) << "<primitive message> sending request :" << request;
  auto rc = reqSock.sendOne(msg);
  if (rc.hasError()) {
    LOG(ERROR) << "sending request failed: " << rc.error();
    return;
  }

  auto maybeMsg = reqSock.recvOne(Constants::kReadTimeout);
  if (maybeMsg.hasError()) {
    LOG(ERROR) << "receiving reply failed: " << maybeMsg.error();
    return;
  }

  auto maybeUint32t = maybeMsg.value().read<uint32_t>();
  if (maybeUint32t.hasError()) {
    LOG(ERROR) << "read uint32_t failed: " << maybeUint32t.error();
    return;
  }

  uint32_t reply = maybeUint32t.value();
  LOG(INFO) << "<primitive message> received reply: " << reply;
}

void
ZmqClient::makeStringRequest() noexcept {
  std::string request = "hello";
  auto const msg = fbzmq::Message::from(request).value();

  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> reqSock{zmqContext_};
  reqSock.connect(fbzmq::SocketUrl{stringCmdUrl_}).value();

  LOG(INFO) << "<string message> sending request :" << request;
  auto rc = reqSock.sendOne(msg);
  if (rc.hasError()) {
    LOG(ERROR) << "sending request failed: " << rc.error();
    return;
  }

  auto maybeMsg = reqSock.recvOne(Constants::kReadTimeout);
  if (maybeMsg.hasError()) {
    LOG(ERROR) << "receiving reply failed: " << maybeMsg.error();
    return;
  }

  auto maybeString = maybeMsg.value().read<std::string>();
  if (maybeString.hasError()) {
    LOG(ERROR) << "read string failed: " << maybeString.error();
    return;
  }

  const auto& reply = maybeString.value();
  LOG(INFO) << "<string message> received reply: " << reply;
}

void
ZmqClient::makeMultipleRequest() noexcept {
  uint32_t request1 = 42;
  auto const msg1 = fbzmq::Message::from(request1).value();

  std::string request2 = "a String";
  auto const msg2 = fbzmq::Message::from(request2).value();

  thrift::StrValue request3;
  *request3.value_ref() = "a Thrift Object";

  auto const msg3 =
      fbzmq::Message::fromThriftObj(request3, serializer_).value();

  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> reqSock{zmqContext_};
  reqSock.connect(fbzmq::SocketUrl{multipleCmdUrl_}).value();

  LOG(INFO) << "<multi string message> sending request : " << request1 << ", "
            << request2 << ", " << *request3.value_ref();
  auto rc = reqSock.sendMultiple(msg1, msg2, msg3);
  if (rc.hasError()) {
    LOG(ERROR) << "sending request failed: " << rc.error();
    return;
  }

  auto maybeMsg = reqSock.recvOne(Constants::kReadTimeout);
  if (maybeMsg.hasError()) {
    LOG(ERROR) << "receiving reply failed: " << maybeMsg.error();
    return;
  }

  auto maybeString = maybeMsg.value().read<std::string>();
  if (maybeString.hasError()) {
    LOG(ERROR) << "read string failed: " << maybeString.error();
    return;
  }

  const auto& reply = maybeString.value();
  LOG(INFO) << "<multi string message> received reply: " << reply;
}

bool
ZmqClient::setKeyValue(const std::string& key, int64_t value) noexcept {
  thrift::Request request;
  request.cmd = thrift::Command::KEY_SET;
  request.key = key;
  request.value_ref() = value;

  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> reqSock{zmqContext_};
  reqSock.connect(fbzmq::SocketUrl{thriftCmdUrl_}).value();

  auto rc = reqSock.sendThriftObj(request, serializer_);
  if (rc.hasError()) {
    LOG(ERROR) << "send thrift request failed: " << rc.error();
    return false;
  }
  VLOG(2) << "Sent KEY_SET command (" << request.key << ": "
          << *request.value_ref() << ")";

  auto maybeThriftObj = reqSock.recvThriftObj<thrift::Response>(
      serializer_, Constants::kReadTimeout);
  if (maybeThriftObj.hasError()) {
    LOG(ERROR) << "recv thrfit response failed: " << maybeThriftObj.error();
    return false;
  }

  const auto& response = maybeThriftObj.value();
  if (!response.success) {
    return false;
  }

  return true;
}

bool
ZmqClient::getKey(const std::string& key, int64_t& value) noexcept {
  thrift::Request request;
  request.cmd = thrift::Command::KEY_GET;
  request.key = key;

  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> reqSock{zmqContext_};
  reqSock.connect(fbzmq::SocketUrl{thriftCmdUrl_}).value();

  auto rc = reqSock.sendThriftObj(request, serializer_);
  if (rc.hasError()) {
    LOG(ERROR) << "send thrift request failed: " << rc.error();
    return false;
  }
  VLOG(2) << "Sent KEY_GET command (" << request.key << ")";

  auto maybeThriftObj = reqSock.recvThriftObj<thrift::Response>(
      serializer_, Constants::kReadTimeout);
  if (maybeThriftObj.hasError()) {
    LOG(ERROR) << "recv thrfit response failed: " << maybeThriftObj.error();
    return false;
  }

  const auto& response = maybeThriftObj.value();
  if (!response.success) {
    return false;
  }

  value = *response.value_ref();
  return true;
}

void
ZmqClient::makeThriftRequest() noexcept {
  std::string key = "test";

  for (int i = 0; i < 3; ++i) {
    // set key-value request
    {
      int64_t value = folly::Random::rand32() % 100;
      bool success = setKeyValue(key, value);
      if (success) {
        LOG(INFO) << "<thrift message> "
                  << "setKey (" << key << ", " << value << ")"
                  << " OK";
      } else {
        LOG(INFO) << "<thrift message> "
                  << "setKey (" << key << ", " << value << ")"
                  << " FAIL";
      }
    }

    // get key request
    {
      int64_t value;
      bool success = getKey(key, value);
      if (success) {
        LOG(INFO) << "<thrift message> "
                  << "getKey (" << key << ") = " << value << " OK";
      } else {
        LOG(INFO) << "<thrift message> "
                  << "getKey (" << key << ") "
                  << " FAIL";
      }
    }
  }
}

} // namespace example
} // namespace fbzmq
