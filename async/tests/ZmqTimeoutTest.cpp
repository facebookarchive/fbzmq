/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <gtest/gtest.h>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>

namespace fbzmq {

using namespace std::chrono_literals;

namespace {

class TestThread final : public ZmqEventLoop {
 public:
  TestThread() {
    prepare();
  }

  int
  getCount() {
    return count_.load(std::memory_order_relaxed);
  }

 private:
  void
  prepare() {
    auto start = std::chrono::steady_clock::now();

    // Schedule first timeout
    timeout_ = ZmqTimeout::make(this, [&, start ]() noexcept {
      auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      LOG(INFO) << "Executing callback after " << diff.count() << "ms.";

      // fetch_add returns previous value
      int oldCount = count_.fetch_add(1, std::memory_order_relaxed);

      EXPECT_EQ(1, getNumPendingTimeouts());

      // Destruct the timeout on 4th time, this will cancel it.
      if (oldCount == 3) {
        timeout_.reset();
        EXPECT_EQ(0, getNumPendingTimeouts());
      }
    });

    EXPECT_EQ(0, getNumPendingTimeouts());
    EXPECT_NO_THROW(timeout_->cancelTimeout());
    timeout_->scheduleTimeout(100ms, true /* periodic */);
    EXPECT_TRUE(timeout_->isScheduled());
    // can schedule twice with no issue
    timeout_->scheduleTimeout(100ms, true /* periodic */);
    EXPECT_TRUE(timeout_->isScheduled());
    EXPECT_TRUE(timeout_->isPeriodic());
    EXPECT_EQ(1, getNumPendingTimeouts());
  }

  std::atomic<int> count_{0};

  std::unique_ptr<ZmqTimeout> timeout_;
};

} // namespace

TEST(ZmqTimeoutTest, TimeoutTest) {
  TestThread testThread;
  EXPECT_EQ(0, testThread.getCount());

  std::thread thread([&]() {
    LOG(INFO) << "Starting zmq thread.";
    testThread.run();
    LOG(INFO) << "Stopping zmq thread.";
  });

  // Busy spin until callback has been executed 4 times
  while (testThread.getCount() != 4) {
    std::this_thread::yield();
  }

  // Cleanup
  testThread.stop();
  thread.join();
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
