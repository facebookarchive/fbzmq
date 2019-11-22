/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/io/async/EventBase.h>
#include <folly/synchronization/Baton.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>

namespace fbzmq {

using namespace std::chrono_literals;

TEST(ZmqTimeoutTest, ZmqEventLoop) {
  fbzmq::ZmqEventLoop evl;

  auto start = std::chrono::steady_clock::now();
  size_t count{0};
  folly::Baton<> waitBaton;

  // Schedule first timeout
  auto timeout = ZmqTimeout::make(&evl, [&]() noexcept {
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    LOG(INFO) << "Executing callback after " << diff.count() << "ms.";

    // fetch_add returns previous value
    ++count;
    if (count >= 4) {
      waitBaton.post();
    }

    EXPECT_EQ(1, evl.getNumPendingTimeouts());
  });

  EXPECT_EQ(0, evl.getNumPendingTimeouts());
  EXPECT_NO_THROW(timeout->cancelTimeout());
  timeout->scheduleTimeout(100ms, true /* periodic */);
  EXPECT_TRUE(timeout->isScheduled());

  // can schedule twice with no issue
  timeout->scheduleTimeout(100ms, true /* periodic */);
  EXPECT_TRUE(timeout->isScheduled());
  EXPECT_TRUE(timeout->isPeriodic());
  EXPECT_LE(1, evl.getNumPendingTimeouts());

  // Start event loop
  std::thread evlThread([&]() {
    LOG(INFO) << "Starting zmq thread.";
    evl.run();
    LOG(INFO) << "Stopping zmq thread.";
  });

  // Wait for completion
  waitBaton.wait();
  timeout.reset();
  EXPECT_EQ(4, count);

  // Cleanup
  evl.stop();
  evlThread.join();
}

TEST(ZmqTimeoutTest, FollyEventBase) {
  folly::EventBase evb;

  auto start = std::chrono::steady_clock::now();
  size_t count{0};
  folly::Baton<> waitBaton;

  // Schedule first timeout
  auto timeout = ZmqTimeout::make(&evb, [&]() noexcept {
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    LOG(INFO) << "Executing callback after " << diff.count() << "ms.";

    // fetch_add returns previous value
    ++count;
    if (count >= 4) {
      waitBaton.post();
    }
  });

  EXPECT_NO_THROW(timeout->cancelTimeout());
  timeout->scheduleTimeout(100ms, true /* periodic */);
  EXPECT_TRUE(timeout->isScheduled());

  // can schedule twice with no issue
  timeout->scheduleTimeout(100ms, true /* periodic */);
  EXPECT_TRUE(timeout->isScheduled());
  EXPECT_TRUE(timeout->isPeriodic());

  // Start event loop
  std::thread evbThread([&]() {
    LOG(INFO) << "Starting event-base thread.";
    evb.loopForever();
    LOG(INFO) << "Stopping event-base thread.";
  });

  // Wait for completion
  waitBaton.wait();
  timeout.reset();
  EXPECT_EQ(4, count);

  // Cleanup
  evb.terminateLoopSoon();
  evbThread.join();
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
