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

#include <fbzmq/service/monitor/ZmqMonitorClient.h>

using namespace std;
using namespace fbzmq;

TEST(ZmqMonitorClientTest, BasicOperation) {
  SCOPE_EXIT {
    LOG(INFO) << "ZmqMonitorClient test/basic operations is done";
  };

  LOG(INFO) << "ZmqMonitorClient test/basic operations starts...";
  Context context;

  auto zmqMonitor = make_shared<ZmqMonitor>(
      std::string{"inproc://monitor-rep"},
      std::string{"inproc://monitor-pub"},
      context);

  auto monitorThread = std::make_unique<std::thread>([zmqMonitor]() {
    LOG(INFO) << "ZmqMonitor thread starting";
    zmqMonitor->run();
    LOG(INFO) << "ZmqMonitor thread finished";
  });

  // this will be invoked before subscriberThread's d-tor
  SCOPE_EXIT {
    LOG(INFO) << "Stopping the monitor thread";
    zmqMonitor->stop();
    monitorThread->join();
  };

  zmqMonitor->waitUntilRunning();
  LOG(INFO) << "ZmqMonitor running...";

  // Test with a ZmqMonitor client
  auto zmqMonitorClient = std::make_unique<ZmqMonitorClient>(
      context, std::string{"inproc://monitor-rep"});

  LOG(INFO) << "monitor client created...";
  // sleep for 6s to ensure querying twice for calculating the cpu% counter
  std::this_thread::sleep_for(std::chrono::seconds(6));

  thrift::Counter counterBar;
  *counterBar.value_ref() = 1234;
  thrift::Counter counterFoo;
  *counterFoo.value_ref() = 5678;
  const CounterMap initCounters = {{"bar", counterBar}, {"foo", counterFoo}};
  zmqMonitorClient->setCounters(initCounters);
  LOG(INFO) << "done setting counters...";

  auto counters = zmqMonitorClient->dumpCounters();
  LOG(INFO) << "got counter values...";

  EXPECT_EQ(1234, *zmqMonitorClient->getCounter("bar")->value_ref());
  EXPECT_EQ(5678, *zmqMonitorClient->getCounter("foo")->value_ref());
  EXPECT_FALSE(bool(zmqMonitorClient->getCounter("foobar")));

  const auto counterNames = zmqMonitorClient->dumpCounterNames();
  LOG(INFO) << "got counter names...";
  std::set<std::string> s(counterNames.begin(), counterNames.end());
  EXPECT_EQ(
      std::set<std::string>({"bar",
                             "foo",
                             "process.cpu.pct",
                             "process.memory.rss",
                             "process.uptime.seconds"}),
      s);

  counters = zmqMonitorClient->dumpCounters();
  LOG(INFO) << "got counter values...";

  EXPECT_EQ(5, counters.size());
  EXPECT_EQ(1234, *counters["bar"].value_ref());
  EXPECT_EQ(5678, *counters["foo"].value_ref());

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
    // NOTE: `sub.recv` is blocking call
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
      EXPECT_EQ(1, updateCounters.size());
      EXPECT_EQ(1235, *updateCounters["bar"].value_ref());
    }

    {
      auto publication =
          sub.recvThriftObj<thrift::MonitorPub>(serializer).value();
      EXPECT_EQ(thrift::PubType::COUNTER_PUB, *publication.pubType_ref());
      auto& updateCounters = *publication.counterPub_ref()->counters_ref();
      EXPECT_EQ(1, updateCounters.size());
      EXPECT_EQ(1, *updateCounters["baz"].value_ref());
    }

    {
      auto publication =
          sub.recvThriftObj<thrift::MonitorPub>(serializer).value();
      EXPECT_EQ(thrift::PubType::EVENT_LOG_PUB, *publication.pubType_ref());
      EXPECT_EQ("log_category", *publication.eventLogPub_ref()->category_ref());
      vector<string> expectedSamples = {"log1", "log2"};
      EXPECT_EQ(expectedSamples, *publication.eventLogPub_ref()->samples_ref());
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

  // Add sth extra to the monitor.
  thrift::Counter counterFoobar;
  *counterFoobar.value_ref() = 9012;
  zmqMonitorClient->setCounter("foobar", counterFoobar);
  LOG(INFO) << "done setting counters again...";

  counters = zmqMonitorClient->dumpCounters();
  LOG(INFO) << "got counter values...";

  EXPECT_EQ(6, counters.size());
  EXPECT_EQ(1234, *counters["bar"].value_ref());
  EXPECT_EQ(5678, *counters["foo"].value_ref());
  EXPECT_EQ(9012, *counters["foobar"].value_ref());

  // bump some counters
  zmqMonitorClient->bumpCounter("bar");
  zmqMonitorClient->bumpCounter("baz");
  LOG(INFO) << "done bumping counters ...";

  counters = zmqMonitorClient->dumpCounters();
  LOG(INFO) << "got counter values...";

  EXPECT_EQ(7, counters.size());
  // bumped existing counter
  EXPECT_EQ(1235, *counters["bar"].value_ref());
  // unbumped existing counter
  EXPECT_EQ(9012, *counters["foobar"].value_ref());
  // bumped new counter
  EXPECT_EQ(1, *counters["baz"].value_ref());

  // publish some logs
  thrift::EventLog eventLog;
  *eventLog.category_ref() = "log_category";
  *eventLog.samples_ref() = {"log1", "log2"};
  zmqMonitorClient->addEventLog(eventLog);
  LOG(INFO) << "done publishing logs...";

  auto lastEventLogs = zmqMonitorClient->getLastEventLogs();
  // number of eventLogs
  EXPECT_EQ(1, lastEventLogs->size());
  EXPECT_EQ("log_category", *lastEventLogs->at(0).category_ref());
  EXPECT_EQ("log1", lastEventLogs->at(0).samples_ref()[0]);
  EXPECT_EQ("log2", lastEventLogs->at(0).samples_ref()[1]);
  LOG(INFO) << "done with last event logs...";
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
