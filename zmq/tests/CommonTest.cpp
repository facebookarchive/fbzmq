/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <gtest/gtest.h>

#include <fbzmq/zmq/Common.h>

namespace fbzmq {

TEST(Error, ErrorOutput) {
  std::stringstream ss;
  fbzmq::Error err = {1, "TEST"};
  ss << err;
  EXPECT_EQ("Error code: 1, 'TEST'", ss.str());
}

TEST(ZmqPollTest, EmptyPoll) {
  std::vector<fbzmq::PollItem> pollItems;
  const auto ret =
      fbzmq::poll(pollItems, std::chrono::milliseconds(100)).value();
  EXPECT_EQ(0, ret);
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
