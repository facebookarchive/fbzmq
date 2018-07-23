/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <syslog.h>

#include "ResourceMonitor.h"

using apache::thrift::FRAGILE;

namespace fbzmq {

ResourceMonitor::ResourceMonitor() {
  pid_ = ::getpid();
  initSigar();
}

ResourceMonitor::~ResourceMonitor() noexcept {
  if (sigar_) {
    sigar_close(sigar_);
  }
}

int ResourceMonitor::initSigar() {
  int sigarStatus = SIGAR_OK;
  if ((sigarStatus = sigar_open(&sigar_)) != SIGAR_OK) {
      LOG(ERROR) << "sigar_open failed with code " << sigarStatus;
  }
  return sigarStatus;
}

folly::Optional<uint64_t>
ResourceMonitor::getRSSMemBytes() const {
  int sigarStatus = SIGAR_OK;
  sigar_proc_mem_t mem;

  if ((sigarStatus = sigar_proc_mem_get(sigar_, pid_, &mem)) != SIGAR_OK) {
    LOG(ERROR) << "sigar_proc_mem_get failed with code " << sigarStatus;
    return folly::none;
  }
  return mem.resident;
}

folly::Optional<float>
ResourceMonitor::getCPUpercentage() const {
  int sigarStatus = SIGAR_OK;
  sigar_proc_cpu_t cpu;

  if ((sigarStatus = sigar_proc_cpu_get(sigar_, pid_, &cpu)) != SIGAR_OK) {
    LOG(ERROR) << "sigar_proc_cpu_get failed with code " << sigarStatus;
    return folly::none;
  }
  return cpu.percent;
}

} // namespace fbzmq
