/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <fbzmq/zmq/Context.h>

namespace fbzmq {

TEST(Context, Move) {
  // move construct and move assign
  fbzmq::Context ctx1(2 /* num-io-threads */, 32 /* num-max-sockets */);
  fbzmq::Context ctx2(std::move(ctx1));
  fbzmq::Context ctx3;
  ctx3 = std::move(ctx2);
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
