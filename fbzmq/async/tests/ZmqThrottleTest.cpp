/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqThrottle.h>

namespace chrono = std::chrono;

namespace fbzmq {

TEST(ZmqThrottleTest, ThrottleTest) {
  ZmqEventLoop evl;

  int count = 0;
  ZmqThrottle throttledFn(&evl, chrono::milliseconds(100), [&count]() noexcept {
    count++;
    LOG(INFO) << "Incremented counter. New value: " << count;
  });

  evl.scheduleTimeout(chrono::milliseconds(0), [&]() noexcept {
    LOG(INFO) << "First timeout ... ";
    EXPECT_FALSE(throttledFn.isActive());
    EXPECT_EQ(0, count);
    for (int i = 0; i < 100; i++) {
      throttledFn();
      EXPECT_EQ(0, count);
      EXPECT_TRUE(throttledFn.isActive());
    }
  });

  evl.scheduleTimeout(chrono::milliseconds(200), [&]() noexcept {
    EXPECT_EQ(1, count);
    auto callbackFn = [&]() noexcept {
      EXPECT_EQ(1, count);
      throttledFn();
      EXPECT_TRUE(throttledFn.isActive());
    };

    // All the below ones should lead to only one increment
    EXPECT_FALSE(throttledFn.isActive());
    evl.scheduleTimeout(chrono::milliseconds(10), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(20), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(30), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(40), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(50), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(60), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(70), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(80), callbackFn);
    evl.scheduleTimeout(chrono::milliseconds(90), callbackFn);
    EXPECT_FALSE(throttledFn.isActive());

    // Validate count
    evl.scheduleTimeout(chrono::milliseconds(200), [&]() noexcept {
      EXPECT_EQ(2, count); // Count must be 2
    });

    // Below shouldn't lead to any increment
    evl.scheduleTimeout(chrono::milliseconds(210), [&]() noexcept {
      EXPECT_FALSE(throttledFn.isActive());
      throttledFn();
      EXPECT_TRUE(throttledFn.isActive());
    });
    evl.scheduleTimeout(chrono::milliseconds(220), [&]() noexcept {
      EXPECT_TRUE(throttledFn.isActive());
      throttledFn.cancel();
      EXPECT_FALSE(throttledFn.isActive());
    });

    // Schedule stop and validate final coun
    evl.scheduleTimeout(chrono::milliseconds(400), [&]() noexcept {
      EXPECT_EQ(2, count); // Count must be 2
      evl.stop();
    });
  });

  // Loop thread
  LOG(INFO) << "Starting event loop.";
  EXPECT_EQ(0, count);
  evl.run();
  EXPECT_EQ(2, count);
  LOG(INFO) << "Stopping event loop.";
}

TEST(ZmqThrottleTest, ImmediateExecution) {
  ZmqEventLoop evl;

  int count = 0;
  ZmqThrottle throttledFn(&evl, chrono::milliseconds(0), [&count]() noexcept {
    count++;
    LOG(INFO) << "Incremented counter. New value: " << count;
  });

  evl.runInEventLoop([&]() noexcept {
    EXPECT_EQ(0, count);
    throttledFn();
    EXPECT_EQ(1, count);
    evl.stop();
  });

  // Loop thread
  LOG(INFO) << "Starting event loop.";
  EXPECT_EQ(0, count);
  evl.run();
  EXPECT_EQ(1, count);
  LOG(INFO) << "Stopping event loop.";
}

} // namespace fbzmq

int
main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  return RUN_ALL_TESTS();
}
