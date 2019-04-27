/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/zmq/SocketMonitor.h>

namespace fbzmq {

SocketMonitor::SocketMonitor(
    detail::SocketImpl const& sock, SocketUrl monitorUrl, CallbackT cb) noexcept
    : pairSock_{const_cast<void*>(sock.ctxPtr_)}, cb_{std::move(cb)} {
  auto ptr = const_cast<detail::SocketImpl&>(sock).ptr_;
  int rc = zmq_socket_monitor(
      ptr, static_cast<std::string>(monitorUrl).c_str(), ZMQ_EVENT_ALL);

  CHECK_EQ(0, rc) << "Failed attaching monitor: " << Error();
  pairSock_.connect(SocketUrl{monitorUrl});
  cb_(SocketMonitorMessage::STARTED, SocketUrl{});
}

// public
folly::Expected<folly::Unit, Error>
SocketMonitor::runForever() noexcept {
  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*pairSock_), 0, ZMQ_POLLIN, 0}};

  while (true) {
    fbzmq::poll(pollItems);

    const auto ret = runOnce();
    // error while trying to get monitoing message
    if (ret.hasError()) {
      return folly::makeUnexpected(ret.error());
    }
    // has monitoring finished?
    if (not ret.value()) {
      break;
    }
  } // while

  return folly::unit;
}

folly::Expected<bool, Error>
SocketMonitor::runOnce() noexcept {
  EventT event;
  std::string address;

  {
    auto maybeMsg = pairSock_.recvOne();
    if (maybeMsg.hasError()) {
      return folly::makeUnexpected(maybeMsg.error());
    }
    ::memcpy(
        static_cast<void*>(&event),
        maybeMsg->writeableData().begin(),
        sizeof(EventT));
  }

  {
    auto maybeMsg = pairSock_.recvOne();
    if (maybeMsg.hasError()) {
      return folly::makeUnexpected(maybeMsg.error());
    }
    address = maybeMsg->read<std::string>().value();
  }

  if (event.event == ZMQ_EVENT_MONITOR_STOPPED) {
    return false;
  }

  const auto url = SocketUrl{address};

  switch (event.event) {
  case ZMQ_EVENT_CONNECTED:
    cb_(SocketMonitorMessage::CONNECTED, url);
    break;
  case ZMQ_EVENT_CONNECT_DELAYED:
    cb_(SocketMonitorMessage::CONNECT_DELAYED, url);
    break;
  case ZMQ_EVENT_CONNECT_RETRIED:
    cb_(SocketMonitorMessage::CONNECT_RETRIED, url);
    break;
  case ZMQ_EVENT_LISTENING:
    cb_(SocketMonitorMessage::LISTENING, url);
    break;
  case ZMQ_EVENT_BIND_FAILED:
    cb_(SocketMonitorMessage::BIND_FAILED, url);
    break;
  case ZMQ_EVENT_ACCEPTED:
    cb_(SocketMonitorMessage::ACCEPTED, url);
    break;
  case ZMQ_EVENT_ACCEPT_FAILED:
    cb_(SocketMonitorMessage::ACCEPT_FAILED, url);
    break;
  case ZMQ_EVENT_CLOSED:
    cb_(SocketMonitorMessage::CLOSED, url);
    break;
  case ZMQ_EVENT_CLOSE_FAILED:
    cb_(SocketMonitorMessage::CLOSE_FAILED, url);
    break;
  case ZMQ_EVENT_DISCONNECTED:
    cb_(SocketMonitorMessage::DISCONNECTED, url);
    break;
#if ZMQ_VERSION >= ZMQ_MAKE_VERSION(4, 3, 0)
  case ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
    cb_(SocketMonitorMessage::HANDSHAKE_FAILED_NO_DETAIL, url);
    break;
  case ZMQ_EVENT_HANDSHAKE_SUCCEEDED:
    cb_(SocketMonitorMessage::HANDSHAKE_SUCCEEDED, url);
    break;
  case ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL:
    cb_(SocketMonitorMessage::HANDSHAKE_FAILED_PROTOCOL, url);
    break;
  case ZMQ_EVENT_HANDSHAKE_FAILED_AUTH:
    cb_(SocketMonitorMessage::HANDSHAKE_FAILED_AUTH, url);
    break;
#endif
  default:
    LOG(ERROR) << "Unknown event: " << event.event;
    break;
  } // switch

  return true;
}

} // namespace fbzmq
