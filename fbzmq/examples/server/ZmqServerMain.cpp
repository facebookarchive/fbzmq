/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#include <glog/logging.h>

#include <fbzmq/async/StopEventLoopSignalHandler.h>
#include <fbzmq/examples/common/Constants.h>
#include <fbzmq/examples/server/ZmqServer.h>

int
main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  // start signal handler before any thread
  fbzmq::ZmqEventLoop mainEventLoop;
  fbzmq::StopEventLoopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  std::vector<std::thread> allThreads{};

  // Zmq Context
  fbzmq::Context ctx;

  // start ZmqServer
  fbzmq::example::ZmqServer server(
      ctx,
      fbzmq::example::Constants::kPrimitiveCmdUrl.str(),
      fbzmq::example::Constants::kStringCmdUrl.str(),
      fbzmq::example::Constants::kThriftCmdUrl.str(),
      fbzmq::example::Constants::kMultipleCmdUrl.str(),
      fbzmq::example::Constants::kPubUrl.str());
  std::thread serverThread([&server]() noexcept {
    LOG(INFO) << "Starting Server thread ...";
    server.run();
    LOG(INFO) << "Server stopped.";
  });
  server.waitUntilRunning();
  allThreads.emplace_back(std::move(serverThread));

  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";

  server.stop();
  server.waitUntilStopped();

  for (auto& t : allThreads) {
    t.join();
  }

  return 0;
}
