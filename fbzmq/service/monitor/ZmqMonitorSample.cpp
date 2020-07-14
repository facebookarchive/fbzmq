/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ZmqMonitor.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <thread>

#include <thrift/lib/cpp2/protocol/Serializer.h>

using namespace std;
using namespace fbzmq;

DEFINE_string(ip, "::1", "Controller ip we talk to");
DEFINE_int32(monitor_router_port, 60008, "The monitor router port we talk to");
DEFINE_bool(periodic, true, "Whether to do periodic monitoring");
DEFINE_int32(period, 5, "Period in second");

// send DUMP_ALL_COUNTER_DATA request to monitor
// then print reponse values
void
oneShot(Context& context) noexcept {
  Socket<ZMQ_DEALER, ZMQ_CLIENT> dealer(context);

  // enable ipv6 on the socket
  const int ipv6Enable = 1;
  dealer.setSockOpt(ZMQ_IPV6, &ipv6Enable, sizeof(int)).value();

  // connect to router socket of monitor
  std::string routerSockUrl =
      folly::sformat("tcp://[{}]:{}", FLAGS_ip, FLAGS_monitor_router_port);
  LOG(INFO) << "Connecting to monitor on " << routerSockUrl;
  dealer.connect(SocketUrl(routerSockUrl)).value();

  apache::thrift::CompactSerializer serializer;
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_DATA;

  // send request

  LOG(INFO) << "Sending DUMP_ALL_COUNTER_DATA request.";
  dealer.sendThriftObj(thriftReq, serializer).value();

  // receive and process response
  auto thriftNameValuesRep =
      dealer.recvThriftObj<thrift::CounterValuesResponse>(serializer).value();
  LOG(INFO) << "Current monitor Key Value Pairs";
  for (auto const& it : *thriftNameValuesRep.counters_ref()) {
    LOG(INFO) << "        " << it.first << ": " << *it.second.value_ref();
  }
}

int
main(int argc, char* argv[]) {
  // Parse command line flags
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;

  // use single IO thread
  Context context;

  if (!FLAGS_periodic) {
    oneShot(context);
    return 0;
  }

  while (true) {
    oneShot(context);
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::seconds(FLAGS_period));
  }

  return 0;
}
