/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <unistd.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <fbzmq/service/stats/ThreadData.h>
#include <gflags/gflags.h>

TEST(ThreadDataTest, ApiTest) {
  fbzmq::ThreadData tData;

  tData.setCounter("counter_key", 0);
  tData.addStatExportType("stats_key", fbzmq::AVG);
  tData.addStatExportType("stats_key", fbzmq::SUM);

  // counters must have 9 (1 + 4 + 4) keys
  auto counters = tData.getCounters();
  EXPECT_EQ(counters.size(), 9);
  for (auto const& kv : counters) {
    EXPECT_EQ(kv.second, 0);
  }
  EXPECT_EQ(1, counters.count("counter_key"));
  EXPECT_EQ(1, counters.count("stats_key.avg.60"));
  EXPECT_EQ(1, counters.count("stats_key.avg.600"));
  EXPECT_EQ(1, counters.count("stats_key.avg.3600"));
  EXPECT_EQ(1, counters.count("stats_key.avg.0"));
  EXPECT_EQ(1, counters.count("stats_key.sum.60"));
  EXPECT_EQ(1, counters.count("stats_key.sum.600"));
  EXPECT_EQ(1, counters.count("stats_key.sum.3600"));
  EXPECT_EQ(1, counters.count("stats_key.sum.0"));

  // Add some values
  tData.setCounter("counter_key", 10);
  CHECK_EQ(11, tData.incrementCounter("counter_key", 1));
  tData.addStatValue("stats_key", 10);
  tData.addStatValue("stats_key", 20);

  // counters must have 9 keys as before and expected values
  counters = tData.getCounters();
  EXPECT_EQ(9, counters.size());
  EXPECT_EQ(11, counters["counter_key"]);
  EXPECT_EQ(15, counters["stats_key.avg.60"]);
  EXPECT_EQ(15, counters["stats_key.avg.600"]);
  EXPECT_EQ(15, counters["stats_key.avg.3600"]);
  EXPECT_EQ(15, counters["stats_key.avg.0"]);
  EXPECT_EQ(30, counters["stats_key.sum.60"]);
  EXPECT_EQ(30, counters["stats_key.sum.600"]);
  EXPECT_EQ(30, counters["stats_key.sum.3600"]);
  EXPECT_EQ(30, counters["stats_key.sum.0"]);

  // Add a new and remove export types
  tData.addStatExportType("stats_key", fbzmq::COUNT);
  tData.clearStatExportType("stats_key", fbzmq::AVG);
  tData.clearCounter("counter_key");

  // counters should have 8 keys and appropriate values
  counters = tData.getCounters();
  EXPECT_EQ(8, counters.size());
  EXPECT_EQ(30, counters["stats_key.sum.60"]);
  EXPECT_EQ(30, counters["stats_key.sum.600"]);
  EXPECT_EQ(30, counters["stats_key.sum.3600"]);
  EXPECT_EQ(30, counters["stats_key.sum.0"]);
  EXPECT_EQ(2, counters["stats_key.count.60"]);
  EXPECT_EQ(2, counters["stats_key.count.600"]);
  EXPECT_EQ(2, counters["stats_key.count.3600"]);
  EXPECT_EQ(2, counters["stats_key.count.0"]);
}

int
main(int argc, char** argv) {
  // Basic initialization
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  // Run the tests
  return RUN_ALL_TESTS();
}
