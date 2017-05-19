/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#include <glog/logging.h>

#include <fbzmq/examples/client/ZmqClient.h>
#include <fbzmq/examples/common/Constants.h>

int
main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  // Zmq Context
  fbzmq::Context ctx;

  // start ZmqClient
  fbzmq::example::ZmqClient client(
      ctx,
      fbzmq::example::Constants::kPrimitiveCmdUrl.str(),
      fbzmq::example::Constants::kStringCmdUrl.str(),
      fbzmq::example::Constants::kThriftCmdUrl.str(),
      fbzmq::example::Constants::kMultipleCmdUrl.str(),
      fbzmq::example::Constants::kPubUrl.str());

  client.startRequests();

  return 0;
}
