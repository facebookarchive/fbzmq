/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <thread>

#include <fbzmq/zmq/Common.h>
#include <fbzmq/zmq/Context.h>
#include <fbzmq/zmq/Socket.h>

using namespace std;

namespace fbzmq {

TEST(Error, ErrorOutput) {
  std::stringstream ss;
  fbzmq::Error err = {1, "TEST"};
  ss << err;
  EXPECT_EQ("TEST (errno=1)", ss.str());
}

TEST(ZmqPollTest, EmptyPoll) {
  std::vector<fbzmq::PollItem> pollItems;
  const auto ret =
      fbzmq::poll(pollItems, std::chrono::milliseconds(100)).value();
  EXPECT_EQ(0, ret);
}

TEST(ZmqProxy, EmptyProxy) {
  bool proxyExit = false;

  auto proxyThread = std::thread([&proxyExit]() {
    fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> frontend;
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> backend;
    frontend.bind(fbzmq::SocketUrl{"tcp://*:5555"});
    backend.connect(fbzmq::SocketUrl{"tcp://*:5556"});
    fbzmq::proxy(
        reinterpret_cast<void*>(*frontend),
        reinterpret_cast<void*>(*backend),
        nullptr);
    proxyExit = true;
  });

  proxyThread.join();
  EXPECT_TRUE(proxyExit);
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
