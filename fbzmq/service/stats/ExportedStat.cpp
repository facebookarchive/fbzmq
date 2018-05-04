/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ExportedStat.h"

#include <vector>

#include <folly/Format.h>

namespace fbzmq {

namespace {

/**
 * Number of buckets to use for folly::BucketedTimeSeries
 */
static const uint64_t kTsBuckets{60};

/**
 * Statically defined levels for multi level timeseries.
 */
static const std::vector<std::chrono::seconds> kLevelDurations = {
    std::chrono::seconds(60), // One minute
    std::chrono::seconds(600), // Ten minutes
    std::chrono::seconds(3600), // One hour
    std::chrono::seconds(0), // All time
};

/**
 * Utility function to get the current timestamp in seconds since epoch.
 */
std::chrono::seconds
getTsInSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch());
}

} // namespace

ExportedStat::ExportedStat(std::string const& key) : key_(key) {
  multiTs_.reset(new folly::MultiLevelTimeSeries<int64_t>(
      kTsBuckets, kLevelDurations.size(), &kLevelDurations[0]));
}

void
ExportedStat::setExportType(ExportType type) {
  exportTypeBits_ |= type;
}

void
ExportedStat::unsetExportType(ExportType type) {
  exportTypeBits_ &= ~type;
}

/**
 * Add new value to this stats. We use current timestamp using steady_clock
 */
void
ExportedStat::addValue(int64_t value) {
  multiTs_->addValue(getTsInSeconds(), value);
}

/**
 * API to get the counters for exported stat-types among all the levels.
 * Counters are named as "<key>.<export-type>.<level>". e.g. "foo.avg.60"
 */
void
ExportedStat::getCounters(std::unordered_map<std::string, int64_t>& counters) {
  // Update timeseries
  multiTs_->update(getTsInSeconds());

  for (size_t i = 0; i < kLevelDurations.size(); i++) {
    auto const& level = multiTs_->getLevel(i);
    auto const interval = level.duration().count();

    if (exportTypeBits_ & SUM) {
      auto key = folly::sformat("{}.sum.{}", key_, interval);
      counters[key] = level.sum();
    }

    if (exportTypeBits_ & AVG) {
      auto key = folly::sformat("{}.avg.{}", key_, interval);
      counters[key] = level.avg();
    }

    if (exportTypeBits_ & RATE) {
      auto key = folly::sformat("{}.rate.{}", key_, interval);
      counters[key] = level.rate();
    }

    if (exportTypeBits_ & COUNT) {
      auto key = folly::sformat("{}.count.{}", key_, interval);
      counters[key] = level.count();
    }

    if (exportTypeBits_ & COUNT_RATE) {
      auto key = folly::sformat("{}.count_rate.{}", key_, interval);
      counters[key] = level.countRate();
    }
  }
}
} // namespace fbzmq
