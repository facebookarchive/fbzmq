/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/Memory.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>

using namespace std;

namespace fbzmq {

namespace {

const string kRequestStr = "Hello";
const string kResponseStr = "World";

class ZmqEventLoopTest final : public ZmqEventLoop {
 public:
  ZmqEventLoopTest(Context& context, std::string const& repSockUrl)
      : repSock_(context), repSockUrl_(repSockUrl) {
    count_.store(0, std::memory_order_relaxed);
    timerExecuted_.store(false, std::memory_order_relaxed);
    prepare();
  }

  int
  getCallbackCount() {
    return count_.load(std::memory_order_relaxed);
  }

  bool
  hasTimerExecuted() {
    return timerExecuted_.load(std::memory_order_relaxed);
  }

 private:
  void
  prepare() {
    EXPECT_TRUE(isInEventLoop());

    EXPECT_FALSE(repSock_.bind(SocketUrl{repSockUrl_}).hasError());

    // we expect it to throw if we try to add a callback to the same socket
    // twice
    EXPECT_NO_THROW(
        addSocket(RawZmqSocketPtr{*repSock_}, ZMQ_POLLIN, [](int) noexcept {}));
    EXPECT_THROW(
        addSocket(RawZmqSocketPtr{*repSock_}, ZMQ_POLLIN, [](int) noexcept {}),
        std::runtime_error);
    removeSocket(RawZmqSocketPtr{*repSock_});
    // Finally, Attach the callback we aactually want
    EXPECT_NO_THROW(addSocket(
        RawZmqSocketPtr{*repSock_}, ZMQ_POLLIN, [this](int revents) noexcept {
          // revents must be equal to ZMQ_POLLIN
          EXPECT_EQ(ZMQ_POLLIN, revents);

          // receive request
          auto ret = repSock_.recvOne().value().read<string>().value();
          EXPECT_EQ(kRequestStr, ret);
          LOG(INFO) << "Received request: " << ret;

          // send response back
          EXPECT_NO_THROW(
              repSock_.sendOne(Message::from(kResponseStr).value()).value());
        }));

    // we expect it to throw if we try to add another callback
    // to the same socket
    EXPECT_THROW(
        addSocket(RawZmqSocketPtr{*repSock_}, ZMQ_POLLIN, [](int) noexcept {}),
        std::runtime_error);

    // test adding socketFd twice
    EXPECT_NO_THROW(addSocketFd(-1, ZMQ_POLLIN, [](int) noexcept {}));
    EXPECT_THROW(
        addSocketFd(-1, ZMQ_POLLIN, [](int) noexcept {}), std::runtime_error);
    removeSocketFd(-1);

    // Schedule another async-timeout
    auto token =
        scheduleTimeout(std::chrono::milliseconds(110), [this]() noexcept {
          // This should never get advertised as we are going to cancel this
          // timeout
          ADD_FAILURE(); // This should fail the test
          count_ += 1;
        });
    EXPECT_EQ(1, getNumPendingTimeouts());

    // Schedule another async-timeout which will cancel the first one. This
    // should get executed before the first one
    auto now = std::chrono::steady_clock::now();
    scheduleTimeout(
        std::chrono::milliseconds(105), [this, now, token]() noexcept {
          count_ += 1;
          auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - now);
          EXPECT_LE(std::chrono::milliseconds(105), diff);
          LOG(INFO) << "Executed timeout after " << diff.count() << "ms.";

          // Remove the existing timeout
          EXPECT_EQ(2, getNumPendingTimeouts());
          EXPECT_TRUE(cancelTimeout(token));
          EXPECT_EQ(1, getNumPendingTimeouts());
        });
    EXPECT_EQ(2, getNumPendingTimeouts());
    // schedule final timeout after the one we were supposed to cancel
    // to make sure it didn't execute
    scheduleTimeout(std::chrono::milliseconds(115), [this, now]() noexcept {
      count_ += 1;
      auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - now);
      EXPECT_LE(std::chrono::milliseconds(115), diff);
      LOG(INFO) << "Executed last timeout after " << diff.count() << "ms.";
      EXPECT_EQ(0, getNumPendingTimeouts());

      timerExecuted_.store(true, std::memory_order_relaxed);
    });
    EXPECT_EQ(3, getNumPendingTimeouts());
  }

  Socket<ZMQ_REP, ZMQ_SERVER> repSock_;
  const std::string repSockUrl_;

  std::atomic<int32_t> count_;
  std::atomic<bool> timerExecuted_;
};

} // namespace

TEST(ZmqEventLoopTest, BasicCommunication) {
  Context context;
  const std::string kCmdSockUrl{"inproc://cmd_sock_url"};
  ZmqEventLoopTest eventLoopTest(context, kCmdSockUrl);
  EXPECT_TRUE(eventLoopTest.isInEventLoop());

  LOG(INFO) << "Starting Test......";

  // No timeout should have been executed so far
  EXPECT_EQ(0, eventLoopTest.getCallbackCount());

  // Start thread
  thread eventLoop([&eventLoopTest]() {
    LOG(INFO) << "Starting test zmq-thread.";
    EXPECT_TRUE(eventLoopTest.isInEventLoop());
    eventLoopTest.run();
    EXPECT_TRUE(eventLoopTest.isInEventLoop());
  });

  LOG(INFO) << "Started System...";
  eventLoopTest.waitUntilRunning();
  EXPECT_FALSE(eventLoopTest.isInEventLoop());

  // Socket to send/recv request/reply to zmq-thread
  Socket<ZMQ_REQ, ZMQ_CLIENT> reqSock(context);
  EXPECT_FALSE(reqSock.connect(SocketUrl{kCmdSockUrl}).hasError());

  // Send/recv message
  EXPECT_FALSE(reqSock.sendOne(Message::from(kRequestStr).value()).hasError());
  EXPECT_EQ(kResponseStr, reqSock.recvOne().value().read<string>().value());

  // Sleep until timer has been executed
  while (not eventLoopTest.hasTimerExecuted()) {
    std::this_thread::yield();
  }

  // The two timeouts must have been executed by now
  EXPECT_EQ(2, eventLoopTest.getCallbackCount());

  // Cleanup
  LOG(INFO) << "Stopping ...";
  eventLoopTest.stop();
  eventLoopTest.waitUntilStopped();
  eventLoop.join();
  LOG(INFO) << "Stopped.....";
}

/**
 * This test is to ensure that callbacks can be passed as copy to be executed
 * asynchronously. (Tests compilation is possible).
 */
TEST(ZmqEventLoopTest, CopyCapture) {
  ZmqEventLoop evl;
  auto const& now = std::chrono::steady_clock::now();

  auto callback = std::function<void()>([&]() noexcept {
    SUCCEED();
    evl.stop();
  });
  evl.scheduleTimeout(std::chrono::seconds(0), callback);

  LOG(INFO) << "Starting loop...";
  evl.run();
  EXPECT_GE(
      evl.getTimestamp().time_since_epoch().count(),
      now.time_since_epoch().count());

  LOG(INFO) << "Stopping loop...";
  SUCCEED();
}

TEST(ZmqEventLoopTest, RunInEventLoopApi) {
  ZmqEventLoop evl(10);

  // Start and wait until evl loop is running
  std::thread evlThread([&]() { evl.run(); });
  evl.waitUntilRunning();

  // Make sure loop is running and we are not in the same thread as of evl
  EXPECT_TRUE(evl.isRunning());
  EXPECT_FALSE(evl.isInEventLoop());

  // Enqueue 100 (> 10) events into event-loop
  LOG(INFO) << "Enqueueing callback into event loop thread.";
  int count = 0;
  for (int i = 0; i < 100; i++) {
    evl.runInEventLoop([&, i]() noexcept {
      EXPECT_EQ(i, count); // Must be same by when callback is executed.
      ++count;
      VLOG(1) << "count: " << count;
      EXPECT_TRUE(evl.isRunning());
      EXPECT_TRUE(evl.isInEventLoop());
    });
  }

  // Enqueue stop event
  evl.runInEventLoop([&]() noexcept {
    EXPECT_EQ(100, count);
    EXPECT_TRUE(evl.isRunning());
    EXPECT_TRUE(evl.isInEventLoop());

    LOG(INFO) << "Stopping event loop. Count: " << count;
    evl.stop();
  });

  // Wait for evl loop to terminate. It must terminate.
  evlThread.join();
  EXPECT_EQ(100, count);
}

TEST(ZmqEventLoopTest, RunImmediatelyOrInEventLoopApi) {
  ZmqEventLoop evl(10);

  int counter = 0;
  auto incrementCb = std::function<void()>([&]() noexcept { counter++; });

  // Case-1: Thread is not running
  evl.runImmediatelyOrInEventLoop(incrementCb);
  EXPECT_EQ(1, counter);

  // Start event loop thread
  std::thread evlThread([&]() { evl.run(); });
  evl.waitUntilRunning();

  // Case-2: Schedule a callback when thread is running
  EXPECT_FALSE(evl.isInEventLoop());
  evl.runImmediatelyOrInEventLoop([&]() noexcept {
    EXPECT_TRUE(evl.isInEventLoop());
    EXPECT_EQ(1, counter);

    // Case-3: Schedule a callback from within event loop. Shoule execute
    // immediately.
    evl.runImmediatelyOrInEventLoop(incrementCb);
    EXPECT_EQ(2, counter);

    // Stop event loop
    evl.stop();
  });

  evlThread.join();
}

TEST(ZmqEventLoopTest, scheduleTimeoutApi) {
  const uint32_t kCount = 1024;
  ZmqEventLoop evl;

  //
  // Schedule multiple events with same time points and validate their execution
  // order (which should be in the same order as of their insertion)
  //

  uint32_t count = 0;
  auto now = std::chrono::steady_clock::now();
  for (uint32_t i = 1; i <= kCount; ++i) {
    evl.scheduleTimeoutAt(now, [i, kCount, &count, &evl]() noexcept {
      EXPECT_TRUE(evl.isRunning());
      ++count;
      EXPECT_EQ(i, count);
      if (count == kCount) {
        evl.stop();
      }
    });
  }

  EXPECT_FALSE(evl.isRunning());
  EXPECT_EQ(0, count);
  evl.run();
  EXPECT_EQ(kCount, count);
  EXPECT_GE(
      evl.getTimestamp().time_since_epoch().count(),
      now.time_since_epoch().count());
  EXPECT_FALSE(evl.isRunning());

  //
  // Schedule at (potentially) different time points which are monotonically
  // increasing and enforce validity on their execution order
  //

  count = 0;
  now = std::chrono::steady_clock::now();
  for (uint32_t i = 1; i <= kCount; ++i) {
    now = std::chrono::steady_clock::now();
    evl.scheduleTimeoutAt(now, [i, kCount, &count, &evl]() noexcept {
      EXPECT_TRUE(evl.isRunning());
      ++count;
      EXPECT_EQ(i, count);
      if (count == kCount) {
        evl.stop();
      }
    });
  }

  EXPECT_FALSE(evl.isRunning());
  EXPECT_EQ(0, count);
  EXPECT_EQ(0, count);
  evl.run();
  EXPECT_EQ(kCount, count);
  EXPECT_GE(
      evl.getTimestamp().time_since_epoch().count(),
      now.time_since_epoch().count());
  EXPECT_FALSE(evl.isRunning());
}

TEST(ZmqEventLoopTest, sendRecvMultipart) {
  Context context;
  ZmqEventLoop evl;
  auto now = std::chrono::steady_clock::now();
  const SocketUrl socketUrl{"inproc://server_url"};

  Socket<ZMQ_REP, ZMQ_SERVER> serverSock{context};
  serverSock.bind(socketUrl).value();
  evl.addSocket(RawZmqSocketPtr{*serverSock}, ZMQ_POLLIN, [&](int) noexcept {
    LOG(INFO) << "Received request on server socket.";
    Message msg1, msg2;
    serverSock.recvMultiple(msg1, msg2).value();
    LOG(INFO) << "Messages received .... "
              << "\n\t " << msg1.read<std::string>().value() << "\n\t "
              << msg2.read<std::string>().value();
    EXPECT_EQ(std::string("hello world"), msg1.read<std::string>().value());
    EXPECT_EQ(std::string("yolo"), msg2.read<std::string>().value());
    serverSock.sendMultiple(msg1, msg2).value();
    evl.stop();
  });

  std::thread evlThread([&]() noexcept {
    LOG(INFO) << "Starting event loop";
    evl.run();
    LOG(INFO) << "Event loop stopped";
  });
  evl.waitUntilRunning();

  Socket<ZMQ_REQ, ZMQ_CLIENT> clientSock{context};
  clientSock.connect(socketUrl).value();

  LOG(INFO) << "Sending messages.";
  clientSock.sendMultiple(
      Message::from(std::string("hello world")).value(),
      Message::from(std::string("yolo")).value());
  LOG(INFO) << "Receiving messages.";
  auto msgs = clientSock.recvMultiple().value();
  LOG(INFO) << "Received messages.";
  EXPECT_EQ(2, msgs.size());
  EXPECT_GE(
      evl.getTimestamp().time_since_epoch().count(),
      now.time_since_epoch().count());

  evlThread.join();
}

TEST(ZmqEventLoopTest, veriyHealthCheckDuration) {
  Context context;
  ZmqEventLoop evl(1e4, std::chrono::seconds(1));

  auto callback = std::function<void()>([&]() noexcept {
    // Verify that even timeout is scheduled far later, evl gets
    // lastestActivityTs_ updated every healthCheckDuration
    auto now = std::chrono::steady_clock::now();
    EXPECT_GE(
        1,
        std::chrono::duration_cast<std::chrono::seconds>(
            now - evl.getTimestamp())
            .count());
    evl.stop();
  });
  evl.scheduleTimeout(std::chrono::seconds(3), callback);

  std::thread evlThread([&]() noexcept {
    LOG(INFO) << "Starting event loop";
    evl.run();
    LOG(INFO) << "Event loop stopped";
  });
  evl.waitUntilRunning();

  evlThread.join();
}

TEST(ZmqEventLoopTest, emptyTimeout) {
  Context context;
  ZmqEventLoop evl(1e4, std::chrono::seconds(1));

  std::thread evlThread([&]() noexcept {
    LOG(INFO) << "Starting event loop";
    evl.run();
    LOG(INFO) << "Event loop stopped";
  });
  evl.waitUntilRunning();

  // Verify that even no timeout is scheduled, evl gets lastestActivityTs_
  // updated every healthCheckDuration
  auto now = std::chrono::steady_clock::now();
  EXPECT_GE(
      1,
      std::chrono::duration_cast<std::chrono::seconds>(now - evl.getTimestamp())
          .count());

  std::this_thread::sleep_for(std::chrono::seconds(3));

  now = std::chrono::steady_clock::now();
  EXPECT_GE(
      1,
      std::chrono::duration_cast<std::chrono::seconds>(now - evl.getTimestamp())
          .count());

  evl.stop();
  evlThread.join();
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
