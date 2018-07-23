/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <sigar.h>
#include <sigar_private.h>
#include <sigar_util.h>

#include <folly/Optional.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace fbzmq {

class ResourceMonitor {
 public:
  ResourceMonitor();

  ~ResourceMonitor() noexcept;

  // non-copyable
  ResourceMonitor(ResourceMonitor const&) = delete;
  ResourceMonitor& operator=(ResourceMonitor const&) = delete;

  // return RSS memory in use
  folly::Optional<uint64_t> getRSSMemBytes() const;

  // return CPU%
  folly::Optional<float> getCPUpercentage() const;

 private:
  // process ID
  pid_t pid_;

  // sigar instance
  sigar_t* sigar_{nullptr};

  // initialize sigar
  int initSigar();
};

} // namespace fbzmq
