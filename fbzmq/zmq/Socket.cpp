/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/zmq/Socket.h>

#include <folly/fibers/EventBaseLoopController.h>
#include <folly/net/NetworkSocket.h>

namespace fbzmq {
namespace detail {

SocketImpl::SocketImpl(
    int type,
    bool isServer,
    Context& ctx,
    folly::Optional<IdentityString> identity,
    folly::Optional<KeyPair> keyPair,
    NonblockingFlag isNonblocking,
    folly::EventBase* evb)
    : SocketImpl(
          type,
          isServer,
          ctx.ptr_,
          std::move(identity),
          std::move(keyPair),
          isNonblocking,
          evb) {}

SocketImpl::SocketImpl(
    int type,
    bool isServer,
    void* ctxPtr,
    folly::Optional<IdentityString> identity,
    folly::Optional<KeyPair> keyPair,
    NonblockingFlag isNonblocking,
    folly::EventBase* evb)
    : folly::EventHandler(),
      ptr_(zmq_socket(ctxPtr, type)),
      ctxPtr_{ctxPtr},
      keyPair_(std::move(keyPair)),
      evb_(evb) {
  CHECK(ctxPtr);
  CHECK(ptr_) << Error();

  if (isNonblocking) {
    baseFlags_ |= ZMQ_DONTWAIT;
  }

  initHandlerHelper();

  // Enable ZMQ_IPV6 by default on the sockets
  const int ipv6Enable = 1;
  setSockOpt(ZMQ_IPV6, &ipv6Enable, sizeof(int)).value();

  // Apply identity if supplied
  if (identity) {
    std::string id = *identity;
    VLOG(4) << "Setting socket identity to `" << id << "`";
    setSockOpt(ZMQ_IDENTITY, id.data(), id.size()).value();
  }

  // For ZMQ router socket, always drop unknown identifies with error to peer
  if (type == ZMQ_ROUTER) {
    const int mandatory = 1;
    setSockOpt(ZMQ_ROUTER_MANDATORY, &mandatory, sizeof(int)).value();
  }

  // Always disable lingering on shutdown
  const int linger = 0;
  setSockOpt(ZMQ_LINGER, &linger, sizeof(int)).value();

  // Crypto settings
  if (keyPair_) {
    applyKeyPair(keyPair_.value());
    if (isServer) {
      static const int curveServer = 1;
      setSockOpt(ZMQ_CURVE_SERVER, &curveServer, sizeof(curveServer)).value();
    }
  }
}

SocketImpl::SocketImpl(int /* type */, bool /* isServer */) : ptr_{nullptr} {}

SocketImpl::~SocketImpl() noexcept {
  close();
}

SocketImpl::SocketImpl(SocketImpl&& other) noexcept
    : folly::EventHandler(),
      baseFlags_(other.baseFlags_),
      ptr_(other.ptr_),
      ctxPtr_(other.ctxPtr_),
      keyPair_(std::move(other.keyPair_)),
      evb_(other.evb_) {
  other.ptr_ = nullptr;
  initHandlerHelper();
}

SocketImpl&
SocketImpl::operator=(SocketImpl&& other) noexcept {
  baseFlags_ = other.baseFlags_;
  ptr_ = other.ptr_;
  ctxPtr_ = other.ctxPtr_;
  keyPair_ = std::move(other.keyPair_);
  evb_ = other.evb_;
  other.ptr_ = nullptr;
  initHandlerHelper();
  return *this;
}

void
SocketImpl::initHandlerHelper() noexcept {
  if (not evb_) {
    return;
  }

  CHECK(baseFlags_ & ZMQ_DONTWAIT)
      << "Socket must be set in non-blocking mode for async read/writes";
  // Initialize event handler
  int socketFd{-1};
  size_t fdLen = sizeof(socketFd);
  const auto rc = zmq_getsockopt(ptr_, ZMQ_FD, &socketFd, &fdLen);
  CHECK_EQ(0, rc) << "Can't get fd for socket. " << Error();
  initHandler(evb_, folly::NetworkSocket::fromFd(socketFd));
}

folly::Expected<folly::Unit, Error>
SocketImpl::setSockOpt(int option, const void* optval, size_t len) noexcept {
  const int rc = zmq_setsockopt(ptr_, option, optval, len);
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return folly::unit;
}

folly::Expected<folly::Unit, Error>
SocketImpl::setKeepAlive(
    int keepAlive,
    int keepAliveIdle,
    int keepAliveCnt,
    int keepAliveIntvl) noexcept {
  {
    auto res = setSockOpt(ZMQ_TCP_KEEPALIVE, &keepAlive, sizeof(int));
    if (res.hasError()) {
      return folly::makeUnexpected(res.error());
    }
  }

  // Apply rest of the socket options only if keepAlive is set
  if (keepAlive != 1) {
    return folly::unit;
  }

  {
    auto res = setSockOpt(ZMQ_TCP_KEEPALIVE_IDLE, &keepAliveIdle, sizeof(int));
    if (res.hasError()) {
      return folly::makeUnexpected(res.error());
    }
  }

  {
    auto res = setSockOpt(ZMQ_TCP_KEEPALIVE_CNT, &keepAliveCnt, sizeof(int));
    if (res.hasError()) {
      return folly::makeUnexpected(res.error());
    }
  }

  {
    auto res =
        setSockOpt(ZMQ_TCP_KEEPALIVE_INTVL, &keepAliveIntvl, sizeof(int));
    if (res.hasError()) {
      return folly::makeUnexpected(res.error());
    }
  }

  return folly::unit;
}

folly::Expected<folly::Unit, Error>
SocketImpl::getSockOpt(int option, void* optval, size_t* len) noexcept {
  const int rc = zmq_getsockopt(ptr_, option, optval, len);
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return folly::unit;
}

void
SocketImpl::close() noexcept {
  if (not ptr_) {
    return;
  }
  // Unregister handler from event base
  unregisterHandler();

  // Unblock awaited reads or writes if any
#if FOLLY_HAS_COROUTINES
  coroReadBaton_.post();
  coroWriteBaton_.post();
#endif
  fiberReadBaton_.post();
  fiberWriteBaton_.post();

  const int rc = zmq_close(ptr_);
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
  ptr_ = nullptr;
}

#if FOLLY_HAS_COROUTINES

folly::coro::Task<folly::Expected<Message, Error>>
SocketImpl::recvOneCoro() {
  CHECK(evb_);

  // Try immediate recv
  auto ret = recv(baseFlags_);
  if (not ret.hasError() or ret.error().errNum != EAGAIN) {
    co_return ret;
  }

  // Error was EAGAIN - Wait for socket to become readable, then perform read
  co_await coroWaitImpl(true /* isReadElseWrite */);
  co_return recv(baseFlags_);
}

folly::coro::Task<folly::Expected<size_t, Error>>
SocketImpl::sendOneCoro(Message msg) {
  CHECK(evb_);

  // Wait for socket to become writable, then perform write
  // NOTE: This is different from read path as we will always
  // get write event on epoll if we haven't written before.
  co_await coroWaitImpl(false /* isReadElseWrite */);
  co_return send(std::move(msg), baseFlags_);
}

folly::coro::Task<void>
SocketImpl::coroWaitImpl(bool isReadElseWrite) noexcept {
  folly::coro::Baton* waitBaton{nullptr};
  if (isReadElseWrite) {
    waitBaton = &coroReadBaton_;
    waitEvents_ |= EventHandler::READ;
  } else {
    waitBaton = &coroWriteBaton_;
    waitEvents_ |= EventHandler::WRITE;
  }
  CHECK(waitBaton);
  waitBaton->reset(); // Reset baton
  registerHandler(waitEvents_ | EventHandler::PERSIST);
  co_await* waitBaton;
  co_return;
}
#endif

bool
SocketImpl::fiberWaitImpl(
    bool isReadElseWrite,
    folly::Optional<std::chrono::milliseconds> timeout) noexcept {
  CHECK(folly::fibers::onFiber()) << "Not on fiber!";

  folly::fibers::Baton* waitBaton{&fiberReadBaton_};
  uint16_t waitEvent{EventHandler::READ};
  if (not isReadElseWrite) {
    waitBaton = &fiberWriteBaton_;
    waitEvent = EventHandler::WRITE;
  }
  CHECK(waitBaton);
  waitEvents_ |= waitEvent;
  waitBaton->reset(); // Reset baton
  registerHandler(waitEvents_ | EventHandler::PERSIST);
  if (timeout.has_value()) {
    const auto hasEvent = waitBaton->timed_wait(timeout.value());
    if (not hasEvent) {
      // Unregister wait-event
      waitEvents_ &= ~waitEvent;
      if (not waitEvents_) {
        unregisterHandler();
      } else {
        registerHandler(waitEvents_);
      }
      return false;
    }
  } else {
    waitBaton->wait();
  }
  return true;
}

void
SocketImpl::handlerReady(uint16_t events) noexcept {
  // Event must be of our interest
  CHECK(events & EventHandler::READ_WRITE)
      << "Received unknown event(s): " << events;

  // Retrieve zmq events
  uint32_t zmqEvents{0};
  size_t zmqEventsLen = sizeof(zmqEvents);
  auto err = zmq_getsockopt(ptr_, ZMQ_EVENTS, &zmqEvents, &zmqEventsLen);
  CHECK_EQ(0, err) << "Got error while reading events from zmq socket";

  const auto prevWaitEvents = waitEvents_;

  // If read events, wake up reader
  if ((waitEvents_ & EventHandler::READ) and (zmqEvents & ZMQ_POLLIN)) {
    waitEvents_ &= ~EventHandler::READ; // Remove read event
    fiberReadBaton_.post();
#if FOLLY_HAS_COROUTINES
    coroReadBaton_.post();
#endif
  }

  // If write events, wake up writer
  if ((waitEvents_ & EventHandler::WRITE) and (zmqEvents & ZMQ_POLLOUT)) {
    waitEvents_ &= ~EventHandler::WRITE; // Remove write event
    fiberWriteBaton_.post();
#if FOLLY_HAS_COROUTINES
    coroWriteBaton_.post();
#endif
  }

  // Un-register or update handler for waiting on remaining event
  if (not waitEvents_) {
    unregisterHandler();
  } else if (waitEvents_ != prevWaitEvents) {
    registerHandler(waitEvents_); // Update handler with new wait events
  }
}

folly::Expected<size_t, Error>
SocketImpl::sendOne(Message msg) noexcept {
  // Wait only if sending first part and evb is present
  if (not isSendingMore_ && evb_) {
    fiberWaitImpl(false /* isReadElseWrite */, folly::none);
  }
  return send(std::move(msg), baseFlags_);
}

folly::Expected<size_t, Error>
SocketImpl::sendMore(Message msg) noexcept {
  // Wait only if sending first part and evb is present
  if (not isSendingMore_ && evb_) {
    fiberWaitImpl(false /* isReadElseWrite */, folly::none);
  }
  return send(std::move(msg), baseFlags_ | ZMQ_SNDMORE);
}

folly::Expected<size_t, Error>
SocketImpl::sendMultiple(std::vector<Message> const& msgs, bool hasMore) {
  size_t size{0};
  if (msgs.empty()) {
    return 0;
  }
  for (size_t i = 0; i < msgs.size() - 1; i++) {
    auto ret = sendMore(msgs[i]);
    if (not ret.hasValue()) {
      return folly::makeUnexpected(ret.error());
    }
    size += ret.value();
  }

  auto last = hasMore ? sendMore(msgs.back()) : sendOne(msgs.back());

  if (not last.hasValue()) {
    return folly::makeUnexpected(last.error());
  }
  return size += last.value();
}

folly::Expected<Message, Error>
SocketImpl::recvAsync(
    folly::Optional<std::chrono::milliseconds> timeout) noexcept {
  auto ret = recv(baseFlags_);
  if (not ret.hasError() or ret.error().errNum != EAGAIN) {
    return ret;
  }

  // Error was EAGAIN - Wait for socket to become readable, then perform read
  if (not fiberWaitImpl(true /* isReadElseWrite */, timeout)) {
    return folly::makeUnexpected(Error(EAGAIN, "fiber recv timeout"));
  }
  return recv(baseFlags_);
}

folly::Expected<Message, Error>
SocketImpl::recvOne(folly::Optional<std::chrono::milliseconds>
                        timeout /* = folly::none */) noexcept {
  // ignore timeout if non-blocking
  if (baseFlags_ & ZMQ_DONTWAIT) {
    if (evb_) {
      return recvAsync(timeout);
    }
    return recv(baseFlags_);
  }

  std::vector<fbzmq::PollItem> pollItems = {{ptr_, 0, ZMQ_POLLIN, 0}};
  auto ret = fbzmq::poll(pollItems, timeout);
  if (ret.hasError()) {
    return folly::makeUnexpected(ret.error());
  }
  //  If we got a reply, process it
  if (pollItems.front().revents & ZMQ_POLLIN) {
    // Receive raw message from socket
    return recv(baseFlags_ & ZMQ_DONTWAIT);
  }
  return folly::makeUnexpected(Error());
}

folly::Expected<std::vector<Message>, Error>
SocketImpl::recvMultiple(
    folly::Optional<std::chrono::milliseconds> timeout /* = folly::none */) {
  std::vector<Message> result;

  auto msg = recvOne(timeout);
  if (not msg.hasValue()) {
    return folly::makeUnexpected(msg.error());
  }

  result.emplace_back(std::move(msg.value()));

  // as long as there is more message, we expect
  // them to be received. this will throw if
  // the condition does not hold
  while (hasMore()) {
    // if the first message arrives, the rest shall have arrived, so no timeout
    result.emplace_back(recvOne(std::chrono::milliseconds(0)).value());
  }

  return result;
}

bool
SocketImpl::hasMore() noexcept {
  int more;
  size_t size = sizeof(more);
  while (true) {
    const int rc = zmq_getsockopt(ptr_, ZMQ_RCVMORE, &more, &size);
    if (rc == 0) {
      break;
    }
    const int err = zmq_errno();
    if (err == EINTR) {
      continue;
    }
    CHECK(false);
  }
  return (more == 1);
}

folly::Expected<std::vector<Message>, Error>
SocketImpl::drain(
    folly::Optional<std::chrono::milliseconds> timeout /* = folly::none */) {
  std::vector<Message> result;
  // read till socket throws EAGAIN error
  while (true) {
    auto msg = recvOne(timeout);
    if (msg.hasValue()) {
      result.emplace_back(std::move(msg.value()));
      continue;
    }
    if (msg.error().errNum == EAGAIN) {
      break;
    }
    return folly::makeUnexpected(msg.error());
  }
  return result;
}

folly::Expected<folly::Unit, Error>
SocketImpl::bind(SocketUrl addr) noexcept {
  const int rc = zmq_bind(ptr_, static_cast<std::string>(addr).c_str());
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return folly::unit;
}

folly::Expected<folly::Unit, Error>
SocketImpl::unbind(SocketUrl addr) noexcept {
  const int rc = zmq_unbind(ptr_, static_cast<std::string>(addr).c_str());
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return folly::unit;
}

folly::Expected<folly::Unit, Error>
SocketImpl::connect(SocketUrl addr) noexcept {
  if (keyPair_) {
    try {
      auto const serverKey = serverKeys_.at(addr);
      setCurveServerSocketKey(serverKey);
    } catch (std::out_of_range const& err) {
      VLOG(2) << "Crypto key for " << std::string(addr) << " not found";
      return folly::makeUnexpected(Error(EINVAL));
    }
  }
  const int rc = zmq_connect(ptr_, static_cast<std::string>(addr).c_str());
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return folly::unit;
}

folly::Expected<folly::Unit, Error>
SocketImpl::disconnect(SocketUrl addr) noexcept {
  const int rc = zmq_disconnect(ptr_, static_cast<std::string>(addr).c_str());
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return folly::unit;
}

folly::Expected<size_t, Error>
SocketImpl::send(Message msg, int flags) noexcept {
  isSendingMore_ = flags & ZMQ_SNDMORE;
  while (true) {
    const int n = zmq_msg_send(&(msg.msg_), ptr_, flags);
    if (n >= 0) {
      return n;
    }

    const int err = zmq_errno();

    if (err == EINTR) {
      continue;
    }
    return folly::makeUnexpected(Error(err));
  }
}

folly::Expected<folly::Unit, Error>
SocketImpl::addServerKey(SocketUrl server, PublicKey serverPubKey) noexcept {
  serverKeys_[server] = serverPubKey;
  return folly::unit;
}

folly::Expected<folly::Unit, Error>
SocketImpl::delServerKey(SocketUrl server) noexcept {
  serverKeys_.erase(server);
  return folly::unit;
}

folly::Expected<Message, Error>
SocketImpl::recv(int flags) noexcept {
  Message msg;
  while (true) {
    const int n = zmq_msg_recv(&(msg.msg_), ptr_, flags);
    if (n >= 0) {
      return msg;
    }

    const int err = zmq_errno();

    if (err == EINTR) {
      continue;
    }
    return folly::makeUnexpected(Error(err));
  }
}

folly::Expected<folly::Unit, Error>
SocketImpl::applyKeyPair(const KeyPair& keyPair) noexcept {
  CHECK_EQ(crypto_sign_ed25519_PUBLICKEYBYTES, keyPair.publicKey.length());
  CHECK_EQ(crypto_sign_ed25519_SECRETKEYBYTES, keyPair.privateKey.length());

  // 1) string -> byte
  std::array<uint8_t, crypto_sign_ed25519_PUBLICKEYBYTES> ed25519Pk;
  std::array<uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> ed25519Sk;
  ::memcpy(
      ed25519Pk.data(), keyPair.publicKey.data(), keyPair.publicKey.length());
  ::memcpy(
      ed25519Sk.data(), keyPair.privateKey.data(), keyPair.privateKey.length());

  // 2) convert signature ed25519 key to encryption curve25519 key
  std::array<uint8_t, crypto_scalarmult_curve25519_BYTES> curve25519Pk;
  std::array<uint8_t, crypto_scalarmult_curve25519_BYTES> curve25519Sk;
  if (::crypto_sign_ed25519_pk_to_curve25519(
          curve25519Pk.data(), ed25519Pk.data()) != 0) {
    return folly::makeUnexpected(Error());
  }
  if (::crypto_sign_ed25519_sk_to_curve25519(
          curve25519Sk.data(), ed25519Sk.data()) != 0) {
    return folly::makeUnexpected(Error());
  }

  // Apply secrete-key on the socket
  setSockOpt(ZMQ_CURVE_SECRETKEY, curve25519Sk.data(), curve25519Sk.size())
      .value();

  // Apply public-key on the socket
  setSockOpt(ZMQ_CURVE_PUBLICKEY, curve25519Pk.data(), curve25519Pk.size())
      .value();

  return folly::unit;
}

void
SocketImpl::setCurveServerSocketKey(const std::string& publicKey) noexcept {
  CHECK_EQ(crypto_sign_ed25519_PUBLICKEYBYTES, publicKey.length());
  // first convert signature ed25519 key to encoded encryption curve25519 key

  // 1) string -> byte
  std::array<uint8_t, crypto_sign_ed25519_PUBLICKEYBYTES> ed25519Pk;
  ::memcpy(ed25519Pk.data(), publicKey.data(), publicKey.length());

  // 2) convert signature ed25519 key to encryption curve25519 key
  std::array<uint8_t, crypto_scalarmult_curve25519_BYTES> curve25519Pk;
  if (::crypto_sign_ed25519_pk_to_curve25519(
          curve25519Pk.data(), ed25519Pk.data()) != 0) {
    return;
  }

  // 3) set server's public key
  setSockOpt(ZMQ_CURVE_SERVERKEY, curve25519Pk.data(), curve25519Pk.size())
      .value();
}

} // namespace detail
} // namespace fbzmq
