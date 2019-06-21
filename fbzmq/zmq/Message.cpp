/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/zmq/Message.h>

namespace {

/**
 * ptr points to data in IOBuf; hint points to IOBuf object
 */
void
freeIOBuf(void* /* ptr */, void* hint) {
  auto* buf = reinterpret_cast<folly::IOBuf*>(hint);
  delete buf;
}

} // namespace

namespace fbzmq {

Message::Message() noexcept {
  const int rc = zmq_msg_init(&msg_);
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
}

folly::Expected<Message, Error>
Message::allocate(size_t size) noexcept {
  Message msg;
  zmq_msg_close(&(msg.msg_));
  const int rc = zmq_msg_init_size(&(msg.msg_), size);
  if (rc != 0) {
    return folly::makeUnexpected(Error());
  }
  return msg;
}

folly::Expected<Message, Error>
Message::wrapBuffer(std::unique_ptr<folly::IOBuf> buf) noexcept {
  Message msg;
  zmq_msg_close(&(msg.msg_));
  buf->coalesce();
  auto ptr = buf.release();
  const auto rc = zmq_msg_init_data(
      &msg.msg_,
      reinterpret_cast<void*>(ptr->writableData()),
      ptr->length(),
      freeIOBuf,
      reinterpret_cast<void*>(ptr));
  if (rc != 0) {
    // here we delete the buffer as it has been passed to us
    // via unique_ptr, and there is no way to return it back on error
    // if the buffer was unshared, the underlying wrapped payload
    // may also get released
    delete ptr;
    return folly::makeUnexpected(Error());
  }
  return msg;
}

Message&
Message::operator=(Message&& other) noexcept {
  Message tmp(std::move(other));
  std::swap(msg_, tmp.msg_);
  return *this;
}

Message::Message(Message&& other) noexcept {
  zmq_msg_init(&msg_);
  const int rc = zmq_msg_move(&msg_, &(other.msg_));
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
}

Message::~Message() noexcept {
  const int rc = zmq_msg_close(&msg_);
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
}

Message::Message(Message const& other) noexcept {
  zmq_msg_init(&msg_);
  const int rc = zmq_msg_copy(&msg_, const_cast<zmq_msg_t*>(&(other.msg_)));
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
}

Message&
Message::operator=(Message const& other) noexcept {
  const int rc = zmq_msg_copy(&msg_, const_cast<zmq_msg_t*>(&(other.msg_)));
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
  ;
  return *this;
}

bool
Message::isLast() const noexcept {
  const int rc = zmq_msg_more(const_cast<zmq_msg_t*>(&msg_));
  return rc == 0;
}

folly::ByteRange
Message::data() const noexcept {
  const auto start = reinterpret_cast<const uint8_t*>(
      zmq_msg_data(const_cast<zmq_msg_t*>(&msg_)));
  return folly::Range<const uint8_t*>(start, start + size());
}

folly::MutableByteRange
Message::writeableData() noexcept {
  auto start = reinterpret_cast<uint8_t*>(zmq_msg_data(&msg_));
  return folly::Range<uint8_t*>(start, start + size());
}

size_t
Message::size() const noexcept {
  return zmq_msg_size(const_cast<zmq_msg_t*>(&msg_));
}

bool
Message::empty() const noexcept {
  return (size() == 0);
}

folly::Expected<int, Error>
Message::getProperty(int property) const noexcept {
  const int value = zmq_msg_get(const_cast<zmq_msg_t*>(&msg_), property);
  if (value == -1) {
    return folly::makeUnexpected(Error());
  }
  return value;
}

folly::Expected<std::string, Error>
Message::getMetadataProperty(std::string const& property) const noexcept {
  const char* value =
      zmq_msg_gets(const_cast<zmq_msg_t*>(&msg_), property.c_str());
  if (value == nullptr) {
    return folly::makeUnexpected(Error());
  }
  return std::string(value);
}

} // namespace fbzmq
