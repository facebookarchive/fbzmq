/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <chrono>

#include <fbzmq/async/AsyncSignalHandler.h>

const char* malloc_conf = "background_thread:false";

namespace fbzmq {

sig_atomic_t unregisteredSignalCaught = 0;

// handle signals not registered in AsyncSignalHandlerTest
void
unregisteredSignalHandler(int /* sig */) {
  ++unregisteredSignalCaught;
}

namespace { // anonymous namespace

class AsyncSignalHandlerTest final : public AsyncSignalHandler {
 public:
  explicit AsyncSignalHandlerTest(ZmqEventLoop* evl) : AsyncSignalHandler(evl) {
    count_.store(0, std::memory_order_relaxed);
    signalHandled_.store(false, std::memory_order_relaxed);
  }

  int
  getSignalCount() {
    return count_.load(std::memory_order_relaxed);
  }

  void
  signalReceived(int sig) noexcept override {
    LOG(INFO) << "Received signal: " << sig;
    ++count_;
    signalHandled_.store(true, std::memory_order_relaxed);
  }

  bool
  hasSignalHandled() {
    return signalHandled_.load(std::memory_order_relaxed);
  }

  void
  resetSignalHandled() {
    signalHandled_.store(false, std::memory_order_relaxed);
  }

 private:
  // number of signals caught
  std::atomic<uint8_t> count_;
  std::atomic<bool> signalHandled_;
};

} // namespace

TEST(AsyncSignalHandlerTest, Basic) {
  ZmqEventLoop evl;
  AsyncSignalHandlerTest signalHandler(&evl);
  EXPECT_EQ(&evl, signalHandler.getZmqEventLoop());

  // register a signal
  signalHandler.registerSignalHandler(SIGTERM);
  // register same signal again
  EXPECT_THROW(
      signalHandler.registerSignalHandler(SIGTERM), std::runtime_error);
  // register another signal
  signalHandler.registerSignalHandler(SIGINT);

  // Start thread
  std::thread evlThread([&]() {
    LOG(INFO) << "Starting event loop.";
    evl.run();
    LOG(INFO) << "Event loop stopped.";
  });
  evl.waitUntilRunning();

  // No signal should have been caught so far
  EXPECT_EQ(0, signalHandler.getSignalCount());

  // send unrelated signal
  // install a signal handler first
  signal(SIGABRT, unregisteredSignalHandler);
  kill(getpid(), SIGABRT);
  // wait for unrelated signal to be handled
  while (unregisteredSignalCaught != 1) {
    std::this_thread::yield();
  }
  EXPECT_EQ(0, signalHandler.getSignalCount());

  // send a registered signal
  kill(getpid(), SIGTERM);
  while (not signalHandler.hasSignalHandled()) {
    std::this_thread::yield();
  }
  signalHandler.resetSignalHandled();
  EXPECT_EQ(1, signalHandler.getSignalCount());

  // send signal again
  kill(getpid(), SIGTERM);
  // wait for signal to be handled
  while (not signalHandler.hasSignalHandled()) {
    std::this_thread::yield();
  }
  signalHandler.resetSignalHandled();
  EXPECT_EQ(2, signalHandler.getSignalCount());

  // send another registered signal
  kill(getpid(), SIGINT);
  while (not signalHandler.hasSignalHandled()) {
    std::this_thread::yield();
  }
  signalHandler.resetSignalHandled();
  EXPECT_EQ(3, signalHandler.getSignalCount());

  // unregister a signal
  signalHandler.unregisterSignalHandler(SIGTERM);
  // unregister same signal again
  EXPECT_THROW(
      signalHandler.unregisterSignalHandler(SIGTERM), std::runtime_error);

  // send unregistered signal
  // install a signal handler first
  signal(SIGTERM, unregisteredSignalHandler);
  kill(getpid(), SIGTERM);
  // wait for unregistered signal to be handled
  while (unregisteredSignalCaught != 2) {
    std::this_thread::yield();
  }
  // wait for signal to be handled
  EXPECT_EQ(3, signalHandler.getSignalCount());

  // Cleanup
  LOG(INFO) << "Stopping ...";
  evl.stop();
  evl.waitUntilStopped();
  evlThread.join();
  LOG(INFO) << "Stopped.....";
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
