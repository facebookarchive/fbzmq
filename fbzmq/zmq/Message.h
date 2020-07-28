/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Expected.h>
#include <folly/Range.h>

#include <fbzmq/zmq/Common.h>

namespace fbzmq {

// forward declaration of detail::SocketImpl
namespace detail {
class SocketImpl;
}

/**
 * Wrapper over zmq_msg_t with lot of convenience methods for creating from
 * various data types and reading binary blob into various data types.
 *
 * Supported convenience types are
 * - primitive types (int, double, bool)
 * - string
 * - thrift objects
 */
class Message {
 public:
  Message() noexcept;
  ~Message() noexcept;

  /**
   * Factory methods - contruct message by various means
   */

  /**
   * Allocate message, content undefined (to be written by user)
   */
  static folly::Expected<Message, Error> allocate(size_t size) noexcept;

  /**
   * Wrap existing IOBuf. Notice that this does not copy buffer, rather adopts
   * its content. The IOBuf pointer will be released when Message destructs.
   */
  static folly::Expected<Message, Error> wrapBuffer(
      std::unique_ptr<folly::IOBuf> buf) noexcept;

  /**
   * construct message from Thrift object using supplied serializer
   */
  template <typename ThriftType, typename Serializer>
  static folly::Expected<Message, Error>
  fromThriftObj(ThriftType const& obj, Serializer& serializer) noexcept {
    return wrapBuffer(util::writeThriftObj(obj, serializer));
  }

  /**
   * Construct message from fundamental type
   */
  template <
      typename T,
      std::enable_if_t<std::is_fundamental<std::decay_t<T>>::value>* = nullptr>
  static folly::Expected<Message, Error>
  from(T obj) noexcept {
    return allocate(sizeof(T)).then([&obj](Message&& msg) {
      ::memcpy(msg.writeableData().data(), &obj, sizeof(T));
      return std::move(msg);
    });
  }

  /**
   * Construct message by copying string contents
   */
  static folly::Expected<Message, Error>
  from(std::string const& str) noexcept {
    return Message::wrapBuffer(folly::IOBuf::copyBuffer(str));
  }

  /**
   * Read concrete type from message. All methods treat message atomically.
   * Thus, if you try to read<bool> and msg.size() != sizeof(bool) you will
   * get back EPROTO error
   */

  /**
   * Read fundamental type. Message size must match sizeof(T)
   */
  template <
      typename T,
      std::enable_if_t<std::is_fundamental<std::decay_t<T>>::value>* = nullptr>
  folly::Expected<T, Error>
  read() const noexcept {
    if (sizeof(T) != size()) {
      return folly::makeUnexpected(Error(EPROTO));
    }
    T obj;
    ::memcpy(&obj, reinterpret_cast<const void*>(data().data()), size());
    return obj;
  }

  /**
   * Read into string
   */
  template <
      typename T,
      std::enable_if_t<std::is_same<std::decay_t<T>, std::string>::value>* =
          nullptr>
  folly::Expected<T, Error>
  read() noexcept {
    return std::string(reinterpret_cast<const char*>(data().data()), size());
  }

  /**
   * Read thrift object from message payload
   */
  template <typename ThriftType, typename Serializer>
  folly::Expected<ThriftType, Error>
  readThriftObj(Serializer& serializer) const noexcept {
    auto buf = folly::IOBuf::wrapBufferAsValue(data());
    try {
      return util::readThriftObj<ThriftType>(buf, serializer);
    } catch (std::exception const& e) {
      LOG(ERROR) << "Failed to serialize thrift object. "
                 << "Exception: " << folly::exceptionStr(e) << "Received: "
                 << folly::humanify(std::string(
                        reinterpret_cast<const char*>(data().data()), size()));
    }
    return folly::makeUnexpected(Error(EPROTO));
  }

  /**
   * Message is movable and copyable
   */

  Message& operator=(Message&& other) noexcept;
  Message(Message&& other) noexcept;

  Message(Message const& other) noexcept;
  Message& operator=(Message const&) noexcept;

  /**
   * True if last message in sequence. Only valid for message received
   * from the wire, not locally constructed
   */
  bool isLast() const noexcept;

  /**
   * Grab pointer to raw data (mutable)
   */
  folly::MutableByteRange writeableData() noexcept;

  /**
   * Ditto but read-only, yo
   */
  folly::ByteRange data() const noexcept;

  /**
   * Size of data contained within message
   */
  size_t size() const noexcept;

  /**
   * Convenient function to make check message.size() == 0
   */
  bool empty() const noexcept;

  /**
   * Retrieve the int value of a property for this message
   */
  folly::Expected<int, Error> getProperty(int property) const noexcept;

  /**
   * Retrieve the string value of a metadata property for this message
   */
  folly::Expected<std::string, Error> getMetadataProperty(
      std::string const& property) const noexcept;

 private:
  friend class detail::SocketImpl;

  // we wrap zmq message
  zmq_msg_t msg_;
};

} // namespace fbzmq
