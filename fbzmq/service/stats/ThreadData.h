/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <map>
#include <unordered_map>

#include <boost/noncopyable.hpp>

#include "ExportedStat.h"

namespace fbzmq {

/**
 * Thread storage for storing flat counters, timeseries counters and stats. This
 * can be extended to store histograms and string values.
 *
 * The idea has been borrowed from the fbcode:ServiceData however we have no
 * singleton here. The ThreadData is not thread safe and it is assumed to be
 * used within a single thread only.
 *
 * If you want to use within multiple threads then use your own locks :P
 */
class ThreadData : public boost::noncopyable {
 public:
  ThreadData() = default;
  ~ThreadData() = default;

  /**
   * Clear all counters and exported stats etc. You must call
   * addStatExportType/addStatExports/addHistogram again to
   * re-add statistics.
   */
  void resetAllData();

  /**
   * Exports the given stat value to the counters, using the given export type.
   * In other words, after calling for key = "foo", calls to getCounters() will
   * contains several counters of the form.
   *
   * type SUM: foo.SUM, foo.SUM.60, foo.SUM.600, foo.SUM.3600
   * type COUNT: foo.COUNT, foo.COUNT.60, foo.COUNT.600, foo.COUNT.3600
   * type AVG: foo.avg, foo.avg.60, foo.avg.600, foo.avg.3600
   * so on for other types
   */
  void addStatExportType(std::string const& key, ExportType type);
  void clearStatExportType(std::string const& key, ExportType type);

  /**
   * Adds a value to the historical statistics for a given key. Checkout the
   * documentation of `addStatExportType` to see how to make these statistics
   * available via calls to getCounters().
   */
  void addStatValue(std::string const& key, int64_t value);

  /**
   * Same as above but allows you to specify the export type on the fly.
   */
  void addStatValue(std::string const& key, int64_t value, ExportType type);

  /**
   * API to set a flat counter.
   */
  void setCounter(std::string const& key, int64_t value);

  /**
   * Clear/Remove the counter from internal map.
   */
  void clearCounter(std::string const& key);

  /**
   * Utility function to increment the flat-counter
   */
  int64_t incrementCounter(std::string const& key, int64_t amount = 1);

  /**
   * Returns all the counters (flat + exportedStats) as a map of key, vals.
   */
  std::unordered_map<std::string, int64_t> getCounters();

 private:
  // Exported stats
  std::unordered_map<std::string /* key */, ExportedStat> stats_{};

  // Simple flat counters.
  std::unordered_map<std::string /* key */, int64_t> counters_{};
};
} // namespace fbzmq
