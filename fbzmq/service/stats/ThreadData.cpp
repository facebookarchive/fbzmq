/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ThreadData.h"

namespace fbzmq {

void
ThreadData::resetAllData() {
  counters_.clear();
  stats_.clear();
}

void
ThreadData::addStatExportType(std::string const& key, ExportType type) {
  auto it = stats_.find(key);
  if (it == stats_.end()) {
    std::tie(it, std::ignore) = stats_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(key));
  }

  it->second.setExportType(type);
}

void
ThreadData::clearStatExportType(std::string const& key, ExportType type) {
  auto it = stats_.find(key);
  if (it != stats_.end()) {
    it->second.unsetExportType(type);
  }
}

void
ThreadData::addStatValue(std::string const& key, int64_t value) {
  auto it = stats_.find(key);
  if (it == stats_.end()) {
    std::tie(it, std::ignore) = stats_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(key));
  }

  it->second.addValue(value);
}

void
ThreadData::addStatValue(
    const std::string& key, int64_t value, ExportType type) {
  auto it = stats_.find(key);
  if (it == stats_.end()) {
    std::tie(it, std::ignore) = stats_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(key));
  }

  it->second.setExportType(type);
  it->second.addValue(value);
}

void
ThreadData::setCounter(std::string const& key, int64_t value) {
  counters_[key] = value;
}

void
ThreadData::clearCounter(std::string const& key) {
  counters_.erase(key);
}

int64_t
ThreadData::incrementCounter(std::string const& key, int64_t amount) {
  return (counters_[key] += amount);
}

std::unordered_map<std::string, int64_t>
ThreadData::getCounters() {
  std::unordered_map<std::string, int64_t> counters;

  // Add all the flat counters
  counters.insert(counters_.begin(), counters_.end());

  // Add all stats
  for (auto& kv : stats_) {
    kv.second.getCounters(counters);
  }

  return counters;
}
} // namespace fbzmq
