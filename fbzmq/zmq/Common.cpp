/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <fbzmq/zmq/Common.h>

namespace fbzmq {

Error::Error()
    : errNum(zmq_errno()), errString(folly::errnoStr(errNum).toStdString()) {}

Error::Error(int errNum)
    : errNum(errNum), errString(folly::errnoStr(errNum).toStdString()) {}

Error::Error(int errNum, std::string errString)
    : errNum(errNum), errString(errString) {}

std::ostream&
operator<<(std::ostream& out, Error const& err) {
  out << "Error code: " << err.errNum << ", '" << err.errString << "'";
  return out;
}

folly::Expected<int, Error>
poll(zmq_pollitem_t const* items_, int nitems_, long timeout_ = -1) {
  while (true) {
    auto rc = zmq_poll(const_cast<zmq_pollitem_t*>(items_), nitems_, timeout_);
    if (rc >= 0) {
      return rc;
    }
    const auto errNum = zmq_errno();
    if (errNum == EINTR) {
      continue;
    }
    return folly::makeUnexpected(Error(errNum));
  }
}

folly::Expected<int, Error>
poll(
    std::vector<PollItem> const& items,
    folly::Optional<std::chrono::milliseconds> timeout /* = folly::none */) {
  return poll(items.data(), items.size(), timeout ? timeout->count() : -1);
}

folly::Expected<folly::Unit, Error> proxy(
    void *frontend, void *backend, void *capture) {
  auto rc = zmq_proxy(frontend, backend, capture);
  if (rc == 0) {
    return folly::Unit();
  }
  return folly::makeUnexpected(Error(zmq_errno()));
}

namespace util {

KeyPair
genKeyPair() {
  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];

  ::crypto_sign_keypair(pk, sk);

  auto publicKey = std::string(
      reinterpret_cast<const char*>(pk), crypto_sign_PUBLICKEYBYTES);
  auto privateKey = std::string(
      reinterpret_cast<const char*>(sk), crypto_sign_SECRETKEYBYTES);

  return KeyPair{privateKey, publicKey};
}

} // namespace util
} // namespace fbzmq
