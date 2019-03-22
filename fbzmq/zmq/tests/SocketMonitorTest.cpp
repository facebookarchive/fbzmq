/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <thread>

#include <folly/Format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <fbzmq/zmq/SocketMonitor.h>

namespace fbzmq {

std::string
getSocketUrl(const std::string& urlPrefix) {
  auto tid = std::hash<std::thread::id>()(std::this_thread::get_id());
  return folly::sformat("ipc://{}_{}", urlPrefix, tid);
}

//
// Validate that monitoring socket can receive CONNECT event
//
TEST(SocketMonitor, ConnectSync) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);
  const std::string kServerUrl{getSocketUrl("test_connect_sync")};

  // atomic barriers to synchronize among threads
  std::atomic<bool> isRunning{false};
  std::atomic<bool> isConnected{false};

  // accumulate messages here
  std::set<fbzmq::SocketMonitorMessage> messages;

  // run monitor in this thread
  std::thread t(
      [kServerUrl, &isRunning, &isConnected, &ctx, &client, &messages] {
        // invoke this callback on every monitor event
        fbzmq::SocketMonitor::CallbackT f =
            [kServerUrl, &messages, &isConnected](
                fbzmq::SocketMonitorMessage msg, fbzmq::SocketUrl url) {
              LOG(INFO) << static_cast<int>(msg) << " : "
                        << static_cast<std::string>(url);
              if (msg == fbzmq::SocketMonitorMessage::CONNECTED) {
                EXPECT_EQ(fbzmq::SocketUrl{kServerUrl}, url);
                isConnected = true;
              }
              messages.insert(msg);
            };
        fbzmq::SocketMonitor monitor(
            client, fbzmq::SocketUrl{"inproc://monitor"}, std::move(f));
        isRunning = true;
        auto ret = monitor.runForever();
        EXPECT_TRUE(ret.hasValue());
      });

  // wait for the monitor thread to start
  while (not isRunning) {
    std::this_thread::yield();
  }

  server.bind(fbzmq::SocketUrl{kServerUrl}).value();
  client.connect(fbzmq::SocketUrl{kServerUrl}).value();

  // wait for the connect event
  while (not isConnected) {
    std::this_thread::yield();
  }

  server.close();
  client.close();
  t.join();

  EXPECT_EQ(1, messages.count(fbzmq::SocketMonitorMessage::CONNECTED));
  EXPECT_EQ(1, messages.count(fbzmq::SocketMonitorMessage::STARTED));
}

//
// Validate that monitoring socket can receive ACCEPT event
//
TEST(SocketMonitor, AcceptSync) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);
  const std::string kServerUrl{getSocketUrl("test_accept_sync")};

  // atomic barriers to synchronize among threads
  std::atomic<bool> isRunning{false};
  std::atomic<bool> isBound{false};

  // accumulate messages here
  std::set<fbzmq::SocketMonitorMessage> messages;

  // run monitor in this thread
  std::thread t([kServerUrl, &isRunning, &isBound, &server, &messages] {
    // invoke this callback for monitoring messages
    fbzmq::SocketMonitor::CallbackT f = [kServerUrl, &messages, &isBound](
                                            fbzmq::SocketMonitorMessage msg,
                                            fbzmq::SocketUrl url) {
      LOG(INFO) << static_cast<int>(msg) << " : "
                << static_cast<std::string>(url);
      if (msg == fbzmq::SocketMonitorMessage::ACCEPTED) {
        EXPECT_EQ(fbzmq::SocketUrl{kServerUrl}, url);
        isBound = true;
      }
      messages.insert(msg);
    };
    fbzmq::SocketMonitor monitor(
        server, fbzmq::SocketUrl{"inproc://monitor"}, std::move(f));
    isRunning = true;
    auto ret = monitor.runForever();
    EXPECT_TRUE(ret.hasValue());
  });

  // wait for the monitor thread to start
  while (not isRunning) {
    std::this_thread::yield();
  }

  // bind and connect
  server.bind(fbzmq::SocketUrl{kServerUrl}).value();
  client.connect(fbzmq::SocketUrl{kServerUrl}).value();

  // wait for the connect event
  while (not isBound) {
    std::this_thread::yield();
  }

  server.close();
  t.join();

  EXPECT_EQ(1, messages.count(fbzmq::SocketMonitorMessage::ACCEPTED));
  EXPECT_EQ(1, messages.count(fbzmq::SocketMonitorMessage::STARTED));
}

//
// Monitor socket in same thread using async polling
//
TEST(SocketMonitor, ConnectAsync) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);
  const std::string kServerUrl{getSocketUrl("test_connect_async")};

  // we accumulate monitoring messages here
  std::set<fbzmq::SocketMonitorMessage> messages;

  // the callback to call on evenry message
  fbzmq::SocketMonitor::CallbackT f = [kServerUrl, &messages](
                                          fbzmq::SocketMonitorMessage msg,
                                          fbzmq::SocketUrl url) {
    LOG(INFO) << static_cast<int>(msg) << " : "
              << static_cast<std::string>(url);
    messages.insert(msg);
  };
  fbzmq::SocketMonitor monitor(
      server, fbzmq::SocketUrl{"inproc://monitor"}, std::move(f));

  // pull raw pointer from monitoring socket and add to polling
  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*monitor), 0, ZMQ_POLLIN, 0}};

  // bind and connect, notice the server is already monitored
  server.bind(fbzmq::SocketUrl{kServerUrl}).value();
  client.connect(fbzmq::SocketUrl{kServerUrl}).value();

  // wait for monitoring event - server accepting connection
  while (messages.count(fbzmq::SocketMonitorMessage::ACCEPTED) == 0) {
    fbzmq::poll(pollItems);
    monitor.runOnce();
  }

  messages.clear();
  client.close();

  LOG(INFO) << "Closing client socket";

  // server would report disconnect
  while (messages.count(fbzmq::SocketMonitorMessage::DISCONNECTED) == 0) {
    fbzmq::poll(pollItems);
    monitor.runOnce();
  }

  messages.clear();
  server.close();

  LOG(INFO) << "Closing server socket";

  // wait for monitor to stop
  bool ret = true;
  while (true) {
    fbzmq::poll(pollItems);
    ret = monitor.runOnce().value();
    if (not ret) {
      break;
    }
  }
  EXPECT_FALSE(ret);
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
