/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/service/monitor/SystemMetrics.h>
#include <fbzmq/zmq/Zmq.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace fbzmq {

TEST(SystemMetricsTest, MemoryStats) {
  SystemMetrics systemMetrics_{};

  folly::Optional<uint64_t> rssMem1 = systemMetrics_.getRSSMemBytes();
  EXPECT_TRUE(rssMem1.hasValue());

  // check sanity of return value, check for > 1MB and < 100MB
  EXPECT_GT(rssMem1.value() / 1e6, 1);
  EXPECT_LT(rssMem1.value() / 1e6, 100);

  // fill about 100 Mbytes of memory and check if monitor reports the increase
  std::vector<int64_t> v(13 * 0x100000);
  fill(v.begin(), v.end(), 1);

  folly::Optional<uint64_t> rssMem2 = systemMetrics_.getRSSMemBytes();
  EXPECT_TRUE(rssMem2.hasValue());
  EXPECT_GT(rssMem2.value(), rssMem1.value() + 100);
}

} // namespace fbzmq

int
main(int argc, char* argv[]) {
  // Parse command line flags
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  // init sodium security library
  if (::sodium_init() == -1) {
    LOG(ERROR) << "Failed initializing sodium";
    return -1;
  }

  // Run the tests
  return RUN_ALL_TESTS();
}
