/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <map>
#include <string>

#include <boost/noncopyable.hpp>
#include <folly/String.h>
#include <folly/stats/MultiLevelTimeSeries.h>

#include "ExportType.h"

namespace fbzmq {

/**
 * Class which stores the multi-level timeseries data for a certain key and
 * wraps up the logic for updating and building-counters.
 *
 * We are using most common levels (derived from fbcode::ServiceData) which are
 * - 1 minute (.60)
 * - 10 minutes (.600)
 * - 1 hour (.3600)
 * - all time (.0)
 */
class ExportedStat : public boost::noncopyable {
 public:
  explicit ExportedStat(std::string const& key);

  /**
   * Set/unset export-type for this statistic.
   */
  void setExportType(ExportType type);
  void unsetExportType(ExportType type);

  /**
   * Add new value to this stats. We use current timestamp using steady_clock
   */
  void addValue(int64_t value);

  /**
   * API to get the counters for exported stat-types among all the levels.
   * Counters are named as "<key>.<export-type>.<level>". e.g. "foo.avg.60"
   */
  void getCounters(std::unordered_map<std::string, int64_t>& counters);

 private:
  // The associated key
  std::string key_{""};

  // MultiLevelTimeSeries associated with this statistics
  std::unique_ptr<folly::MultiLevelTimeSeries<int64_t>> multiTs_;

  // Masked bitset for export types. #efficiency
  uint32_t exportTypeBits_{0};
};
} // namespace fbzmq
