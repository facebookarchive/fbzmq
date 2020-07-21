/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <thread>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fbzmq/service/monitor/ZmqMonitor.h>

using namespace std;
using namespace fbzmq;

TEST(ZmqMonitorTest, BasicOperation) {
  SCOPE_EXIT {
    LOG(INFO) << "ZmqMonitor test/basic operations is done";
  };

  LOG(INFO) << "ZmqMonitor test/basic operations starts...";
  Context context;

  // Create the serializer for write/read
  apache::thrift::CompactSerializer serializer;

  fbzmq::LogSample sampleToMerge;
  sampleToMerge.addString("domain", "terragraph");
  auto monitor = make_shared<ZmqMonitor>(
      std::string{"inproc://monitor-rep"}, // monitorSubmitUrl
      std::string{"inproc://monitor-pub"}, // monitorPubUrl_
      context, // zmqContext
      sampleToMerge, // logSampleToMerge
      std::chrono::seconds(2), // alivenessCheckInterval
      100, // maxLogEvents
      std::chrono::seconds(1) // profilingStatInterval
  );

  auto monitorThread = std::make_unique<std::thread>([monitor]() {
    LOG(INFO) << "ZmqMonitor thread starting";
    monitor->run();
    LOG(INFO) << "ZmqMonitor thread finished";
  });

  // this will be invoked before subscriberThread's d-tor
  SCOPE_EXIT {
    LOG(INFO) << "Stopping the monitor thread";
    monitor->stop();
    monitorThread->join();
  };

  monitor->waitUntilRunning();
  LOG(INFO) << "ZmqMonitor running...";

  // Test with a DEALER socket
  Socket<ZMQ_DEALER, ZMQ_CLIENT> dealer(context);
  dealer.connect(SocketUrl{"inproc://monitor-rep"}).value();
  LOG(INFO) << "dealer sock connected...";
  // sleep for 6s to ensure querying twice for calculating the cpu% counter
  std::this_thread::sleep_for(std::chrono::seconds(6));

  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::SET_COUNTER_VALUES;
  thrift::Counter counterBar;
  *counterBar.value_ref() = 1234;
  thriftReq.counterSetParams_ref()->counters_ref()["bar"] = counterBar;
  thrift::Counter counterFoo;
  *counterFoo.value_ref() = 5678;
  thriftReq.counterSetParams_ref()->counters_ref()["foo"] = counterFoo;
  dealer.sendThriftObj(thriftReq, serializer).value();
  LOG(INFO) << "done setting counters...";

  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_NAMES;
  dealer.sendThriftObj(thriftReq, serializer).value();

  auto thriftNamesRep =
      dealer.recvThriftObj<thrift::CounterNamesResponse>(serializer).value();
  LOG(INFO) << "got counter names...";

  std::set<std::string> s(
      thriftNamesRep.counterNames_ref()->begin(),
      thriftNamesRep.counterNames_ref()->end());
  EXPECT_EQ(
      std::set<std::string>({"bar",
                             "foo",
                             "process.cpu.pct",
                             "process.memory.rss",
                             "process.uptime.seconds"}),
      s);

  *thriftReq.cmd_ref() = thrift::MonitorCommand::GET_COUNTER_VALUES;
  *thriftReq.counterGetParams_ref()->counterNames_ref() = {"bar", "foo"};
  dealer.sendThriftObj(thriftReq, serializer).value();
  auto thriftValuesRep =
      dealer.recvThriftObj<thrift::CounterValuesResponse>(serializer).value();
  LOG(INFO) << "got counter values...";

  EXPECT_EQ(1234, *thriftValuesRep.counters_ref()["bar"].value_ref());
  EXPECT_EQ(5678, *thriftValuesRep.counters_ref()["foo"].value_ref());

  // Check the new api of DUMP_ALL_COUNTER_DATA and PUB/SUB as well.
  // First put subscriber in a separate thread to avoid control-flow blocking.
  auto subscriberThread = std::make_unique<std::thread>([&context]() {
    LOG(INFO) << "subscriber thread running";
    auto serializer = apache::thrift::CompactSerializer();

    // Create a subscribe socket.
    Socket<ZMQ_SUB, ZMQ_CLIENT> sub(context);
    sub.connect(SocketUrl{"inproc://monitor-pub"}).value();
    sub.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();
    LOG(INFO) << "sub socket connected...";

    // Test with the SUB socket.
    // `sub.recv` is blocking call
    {
      auto publication =
          sub.recvThriftObj<thrift::MonitorPub>(serializer).value();
      EXPECT_EQ(thrift::PubType::COUNTER_PUB, *publication.pubType_ref());
      auto& updateCounters = *publication.counterPub_ref()->counters_ref();
      EXPECT_EQ(1, updateCounters.size());
      EXPECT_EQ(9012, *updateCounters["foobar"].value_ref());
    }

    {
      auto publication =
          sub.recvThriftObj<thrift::MonitorPub>(serializer).value();
      EXPECT_EQ(thrift::PubType::COUNTER_PUB, *publication.pubType_ref());
      auto& updateCounters = *publication.counterPub_ref()->counters_ref();
      EXPECT_EQ(3, updateCounters.size());
      EXPECT_EQ(1235, *updateCounters["bar"].value_ref());
      EXPECT_EQ(5679, *updateCounters["foo"].value_ref());
      EXPECT_EQ(1, *updateCounters["baz"].value_ref());
    }

    {
      auto publication =
          sub.recvThriftObj<thrift::MonitorPub>(serializer).value();
      EXPECT_EQ(thrift::PubType::EVENT_LOG_PUB, *publication.pubType_ref());
      EXPECT_EQ("log_category", *publication.eventLogPub_ref()->category_ref());
      auto ls1 = LogSample::fromJson(
          publication.eventLogPub_ref()->samples_ref()->at(0));
      auto ls2 = LogSample::fromJson(
          publication.eventLogPub_ref()->samples_ref()->at(1));
      EXPECT_EQ(ls1.getString("key"), "first sample");
      EXPECT_EQ(ls1.getString("domain"), "terragraph");
      EXPECT_EQ(ls2.getString("key"), "second sample");
      EXPECT_EQ(ls2.getString("domain"), "terragraph");
    }

    LOG(INFO) << "subscriber thread finishing";
    sub.close();
  });

  // this will be invoked before monitorThread's d-tor
  SCOPE_EXIT {
    LOG(INFO) << "Stopping the subscriber thread";
    subscriberThread->join();
  };

  // Wait a few seconds for subscriber thread to run.
  LOG(INFO) << "main thread pause briefly to let subscriber thread start up...";
  sleep(1);
  LOG(INFO) << "main thread resume...";

  // Add sth extra to the monitor
  *thriftReq.cmd_ref() = thrift::MonitorCommand::SET_COUNTER_VALUES;
  thrift::Counter counterFoobar;
  *counterFoobar.value_ref() = 9012;
  thriftReq.counterSetParams_ref()->counters_ref()->clear();
  thriftReq.counterSetParams_ref()->counters_ref()["foobar"] = counterFoobar;
  dealer.sendThriftObj(thriftReq, serializer).value();
  LOG(INFO) << "done setting counters again...";

  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_DATA;
  dealer.sendThriftObj(thriftReq, serializer).value();
  auto thriftNameValuesRep =
      dealer.recvThriftObj<thrift::CounterValuesResponse>(serializer).value();
  LOG(INFO) << "got all counters dumped from dealer sock...";

  auto& keyValueMap = *thriftNameValuesRep.counters_ref();
  EXPECT_EQ(6, keyValueMap.size());
  EXPECT_EQ(1234, *keyValueMap["bar"].value_ref());
  EXPECT_EQ(5678, *keyValueMap["foo"].value_ref());
  EXPECT_EQ(9012, *keyValueMap["foobar"].value_ref());

  // bump some counters
  *thriftReq.cmd_ref() = thrift::MonitorCommand::BUMP_COUNTER;
  *thriftReq.counterBumpParams_ref()->counterNames_ref() = {
      "bar", "foo", "baz"};
  dealer.sendThriftObj(thriftReq, serializer).value();
  LOG(INFO) << "done bumping counters ...";

  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_DATA;
  dealer.sendThriftObj(thriftReq, serializer).value();
  thriftNameValuesRep =
      dealer.recvThriftObj<thrift::CounterValuesResponse>(serializer).value();
  LOG(INFO) << "got all counters dumped from dealer sock...";

  keyValueMap = *thriftNameValuesRep.counters_ref();
  EXPECT_EQ(7, keyValueMap.size());
  // bumped existing counter
  EXPECT_EQ(1235, *keyValueMap["bar"].value_ref());
  EXPECT_EQ(5679, *keyValueMap["foo"].value_ref());
  // unbumped existing counter
  EXPECT_EQ(9012, *keyValueMap["foobar"].value_ref());
  // bumped new counter
  EXPECT_EQ(1, *keyValueMap["baz"].value_ref());

  // wait until counters expire
  sleep(4);

  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_NAMES;
  dealer.sendThriftObj(thriftReq, serializer).value();
  thriftNamesRep =
      dealer.recvThriftObj<thrift::CounterNamesResponse>(serializer).value();
  LOG(INFO) << "got counter names...";
  for (auto counter : *thriftNamesRep.counterNames_ref()) {
    LOG(INFO) << "counter after expiration: " << counter << '\n';
  }
  EXPECT_EQ(thriftNamesRep.counterNames_ref()->size(), 3);

  // publish some logs
  *thriftReq.cmd_ref() = thrift::MonitorCommand::LOG_EVENT;
  fbzmq::LogSample sample1, sample2;
  sample1.addString("key", "first sample");
  sample2.addString("key", "second sample");
  *thriftReq.eventLog_ref()->category_ref() = "log_category";
  *thriftReq.eventLog_ref()->samples_ref() = {sample1.toJson(),
                                              sample2.toJson()};
  dealer.sendThriftObj(thriftReq, serializer).value();
  LOG(INFO) << "done publishing logs...";
}

int
main(int argc, char* argv[]) {
  // Parse command line flags
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  // Run the tests
  return RUN_ALL_TESTS();
}
