/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/zmq/Socket.h>

#ifdef FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/Baton.h>
#endif
#include <folly/io/async/EventHandler.h>

namespace {

#ifdef FOLLY_HAS_COROUTINES
//
// ZMQ exposes special FD that in a sense similar to eventFd. The ZMQ
// library will post events on the FD making it readable or writable,
// to indicate that corresponding ZMQ socket is ready for reading or writing.
//
// The following EventHandler grabs that special FD for ZMQ socket, then
// adds it to EventBase for tracking. The event handler fires when socket
// state changes.
//
// We integrate the handler with co-routines by posting on coro::baton when
// the corresponding event happens.
//
class ZmqSocketReadyHandler : public folly::EventHandler {
 public:
  ZmqSocketReadyHandler(
      folly::EventBase* evb,
      int fd,
      // pointer to raw ZMQ socket, so we can zmq_getsockopt()
      void* zmqRawSocket,
      folly::coro::Baton& baton,
      bool isRead)
      : EventHandler(evb, folly::NetworkSocket::fromFd(fd)),
        isRead_{isRead},
        zmqRawSocket_{zmqRawSocket},
        baton_{baton} {
    registerHandler(
        (isRead_ ? EventHandler::READ : EventHandler::WRITE) |
        EventHandler::PERSIST);
  }

  // we set this if ZMQ layer passes us an error
  int errorCode{0};

  void
  handlerReady(uint16_t events) noexcept override {
    if (!(events & (EventHandler::READ | EventHandler::WRITE))) {
      return;
    }

    uint32_t zmqEvents{0};
    size_t zmqEventsLen = sizeof(zmqEvents);

    errorCode =
        zmq_getsockopt(zmqRawSocket_, ZMQ_EVENTS, &zmqEvents, &zmqEventsLen);

    // if ready or got error, notify the sleepr
    if ((errorCode != 0) ||
        (zmqEvents & (isRead_ ? ZMQ_POLLIN : ZMQ_POLLOUT))) {
      unregisterHandler();
      baton_.get().post();
    }
    // otherwise, we keep handler registered
  }

 private:
  bool isRead_{true};
  void* zmqRawSocket_{nullptr};
  std::reference_wrapper<folly::coro::Baton> baton_;
};
#endif

} // namespace

namespace fbzmq {
namespace detail {

SocketImpl::SocketImpl(
    int type,
    bool isServer,
    Context& ctx,
    folly::Optional<IdentityString> identity,
    folly::Optional<KeyPair> keyPair,
    NonblockingFlag isNonblocking)
    : SocketImpl(
          type,
          isServer,
          ctx.ptr_,
          std::move(identity),
          std::move(keyPair),
          isNonblocking) {}

SocketImpl::SocketImpl(
    int type,
    bool isServer,
    void* ctxPtr,
    folly::Optional<IdentityString> identity,
    folly::Optional<KeyPair> keyPair,
    NonblockingFlag isNonblocking)
    : ptr_(zmq_socket(ctxPtr, type)),
      ctxPtr_{ctxPtr},
      keyPair_(std::move(keyPair)) {
  CHECK(ctxPtr);
  CHECK(ptr_) << Error();

  if (isNonblocking) {
    baseFlags_ |= ZMQ_DONTWAIT;
  }

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
    : baseFlags_(other.baseFlags_),
      ptr_(other.ptr_),
      ctxPtr_(other.ctxPtr_),
      keyPair_(std::move(other.keyPair_)) {
  other.ptr_ = nullptr;
}

SocketImpl&
SocketImpl::operator=(SocketImpl&& other) noexcept {
  baseFlags_ = other.baseFlags_;
  ptr_ = other.ptr_;
  ctxPtr_ = other.ctxPtr_;
  keyPair_ = std::move(other.keyPair_);

  other.ptr_ = nullptr;

  return *this;
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
  const int rc = zmq_close(ptr_);
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
  ptr_ = nullptr;
}

#ifdef FOLLY_HAS_COROUTINES
folly::coro::Task<folly::Expected<folly::Unit, Error>>
SocketImpl::waitToRecv(folly::EventBase* evb) {
  co_return co_await waitImpl(evb, WaitReason::RECV);
}

folly::coro::Task<folly::Expected<folly::Unit, Error>>
SocketImpl::waitToSend(folly::EventBase* evb) {
  co_return co_await waitImpl(evb, WaitReason::SEND);
}

folly::coro::Task<folly::Expected<folly::Unit, Error>>
SocketImpl::waitImpl(folly::EventBase* evb, WaitReason reason) {
  folly::coro::Baton baton;
  int zmqFd{-1};
  size_t fdLen = sizeof(zmqFd);

  const auto rc = zmq_getsockopt(ptr_, ZMQ_FD, &zmqFd, &fdLen);

  if (rc != 0) {
    co_return folly::makeUnexpected(Error());
  }

  ZmqSocketReadyHandler zh(evb, zmqFd, ptr_, baton, reason == WaitReason::RECV);
  // when zmqFd becomes readable, the event handler will post
  // on the baton. Until then, this coroutine will be suspended
  co_await baton;
  if (zh.errorCode) {
    co_return folly::makeUnexpected(Error(zh.errorCode));
  }
  co_return folly::unit;
}
#endif

folly::Expected<size_t, Error>
SocketImpl::sendOne(Message msg) const noexcept {
  return send(std::move(msg), baseFlags_);
}

folly::Expected<size_t, Error>
SocketImpl::sendMore(Message msg) const noexcept {
  return send(std::move(msg), baseFlags_ | ZMQ_SNDMORE);
}

folly::Expected<size_t, Error>
SocketImpl::sendMultiple(std::vector<Message> const& msgs, bool hasMore) const {
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
SocketImpl::recvOne(folly::Optional<std::chrono::milliseconds>
                        timeout /* = folly::none */) const noexcept {
  // ignore timeout if non-blocking
  if (baseFlags_ & ZMQ_DONTWAIT) {
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
SocketImpl::send(Message msg, int flags) const noexcept {
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
SocketImpl::recv(int flags) const noexcept {
  Message msg;
  while (true) {
    const int n = zmq_msg_recv(&(msg.msg_), ptr_, flags);
    if (n >= 0) {
      return std::move(msg);
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
