/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/zmq/Context.h>

namespace fbzmq {

Context::Context(
    folly::Optional<uint16_t> numIoThreads,
    folly::Optional<uint16_t> numMaxSockets) noexcept
    : ptr_(zmq_ctx_new()) {
  CHECK(ptr_);

  if (numIoThreads) {
    const int rc = zmq_ctx_set(ptr_, ZMQ_IO_THREADS, numIoThreads.value());
    CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
  }

  if (numMaxSockets) {
    const int rc = zmq_ctx_set(ptr_, ZMQ_MAX_SOCKETS, numMaxSockets.value());
    CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
  }
}

Context::~Context() {
  // if we have been moved out
  if (not ptr_) {
    return;
  }

  const int rc = zmq_ctx_destroy(const_cast<void*>(ptr_));
  CHECK_EQ(0, rc) << zmq_strerror(zmq_errno());
}

Context::Context(Context&& other) noexcept : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

Context&
Context::operator=(Context&& other) noexcept {
  Context tmp(std::move(other));
  std::swap(ptr_, tmp.ptr_);
  return *this;
}

} // namespace fbzmq
