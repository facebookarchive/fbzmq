/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <thread>

#include <folly/Random.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fbzmq/zmq/Socket.h>
#include <fbzmq/zmq/tests/gen-cpp2/Test_types.h>
#include <folly/fibers/Baton.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/init/Init.h>
#include <folly/io/async/AsyncTimeout.h>

#if FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/Sleep.h>
#endif

using namespace std::chrono_literals;

using apache::thrift::CompactSerializer;

namespace {

std::string
genRandomStr(const int len) {
  std::string s;
  s.resize(len);

  static const std::string alphanum =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";

  for (int i = 0; i < len; ++i) {
    s[i] = alphanum.at(folly::Random::rand32() % alphanum.size());
  }

  return s;
}

} // anonymous namespace

namespace fbzmq {

TEST(Socket, Moving) {
  // move construct and move assign
  fbzmq::Context ctx;
  const KeyPair keyPair = fbzmq::util::genKeyPair();
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub1(
      ctx, folly::none, keyPair, NonblockingFlag{true});
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub2(std::move(pub1));
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub3;
  pub3 = std::move(pub2);
  EXPECT_TRUE(pub3.isNonBlocking());
  const KeyPair sockKeyPair = pub3.getKeyPair().value();
  EXPECT_EQ(keyPair.privateKey, sockKeyPair.privateKey);
  EXPECT_EQ(keyPair.publicKey, sockKeyPair.publicKey);
}

TEST(Socket, SetKeepAlive) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(ctx);

  // -1, 0, 1 are the only valid first arguments for setKeepAlive
  EXPECT_TRUE(pub.setKeepAlive(-1).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(0).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1).hasValue());
  EXPECT_FALSE(pub.setKeepAlive(2).hasValue());

  // -1, >0 are the only valid arguments for keepAliveIdle
  EXPECT_FALSE(pub.setKeepAlive(1, -2, -1, -1).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1, 1, -1, -1).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1, 100, -1, -1).hasValue());

  // -1, >0 are the only valid arguments for keepAliveCnt
  EXPECT_FALSE(pub.setKeepAlive(1, -1, -2, -1).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1, -1, 1, -1).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1, -1, 1, -1).hasValue());

  // -1, >0 are the only valid arguments for keepAliveIntvl
  EXPECT_FALSE(pub.setKeepAlive(1, -1, -1, -2).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1, -1, -1, 1).hasValue());
  EXPECT_TRUE(pub.setKeepAlive(1, -1, -1, 100).hasValue());
}

TEST(Socket, RecvNonBlocking) {
  fbzmq::Context ctx;
  // make a non blocking socket
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(
      ctx, folly::none, folly::none, NonblockingFlag{true});

  // Move into another socket (use move copy)
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep2 = std::move(rep);

  // Move into another socket (use move constructor)
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep3(std::move(rep2));

  rep3.bind(fbzmq::SocketUrl{"inproc://test_basic_stuff"}).value();
  // expect error when receiving since nothing should be available to read
  // and it won't block
  EXPECT_TRUE(rep3.recvMultiple().hasError());
  rep3.unbind(fbzmq::SocketUrl{"inproc://test_basic_stuff"}).value();
}

TEST(Socket, BindUnbind) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(ctx);
  pub.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  pub.unbind(fbzmq::SocketUrl{"inproc://test"}).value();
}

TEST(Socket, ConnectDisconnect) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(ctx);
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(ctx);

  pub.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.connect(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.disconnect(fbzmq::SocketUrl{"inproc://test"}).value();
}

// see if we can close then destruct sockets
TEST(Socket, CloseThenDestruct) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(ctx);
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(ctx);

  pub.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.connect(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.disconnect(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.close();
  pub.close();
}

TEST(Socket, DefaultOptions) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> server(ctx);

  {
    LOG(INFO) << "v6Enable";
    int v6Enable{1};
    size_t size = sizeof(int);
    server.getSockOpt(ZMQ_IPV6, &v6Enable, &size).value();
    EXPECT_EQ(1, v6Enable);
  }

  {
    LOG(INFO) << "linger";
    int linger{1};
    size_t size = sizeof(int);
    server.getSockOpt(ZMQ_LINGER, &linger, &size).value();
    EXPECT_EQ(0, linger);
  }

  // alas, ZMQ_ROUTER_* options can not be peeked - not supported
  // by ZMQ
}

//
// Message passing test cases
//
TEST(Socket, SingleMessage) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(ctx);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(ctx);

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  const auto str = genRandomStr(1024);
  auto msg = fbzmq::Message::from(str).value();
  req.sendOne(msg);

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*rep), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems);
  auto rcvd = rep.recvOne();
  EXPECT_EQ(str, rcvd.value().read<std::string>().value());
}

#if FOLLY_HAS_COROUTINES
TEST(Socket, CoroSingleMessage) {
  folly::EventBase evb;
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  const auto str = genRandomStr(1024);

  auto writer = [&evb, &req, &str]() -> folly::coro::Task<folly::Unit> {
    auto msg = fbzmq::Message::from(str).value();
    auto ret = co_await req.sendOneCoro(msg);
    EXPECT_TRUE(ret.hasValue());
    co_return folly::unit;
  };

  auto reader = [&evb, &rep, &str]() -> folly::coro::Task<bool> {
    auto rcvd = co_await rep.recvOneCoro();
    co_return str == rcvd.value().read<std::string>().value();
  };

  auto futReader = reader().scheduleOn(&evb).start();
  auto futWriter = writer().scheduleOn(&evb).start();
  evb.loop();

  EXPECT_TRUE(futReader.isReady());
  EXPECT_TRUE(futReader.value());
}

TEST(Socket, CoroPubSub) {
  folly::EventBase evb;
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);

  pub.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.connect(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();

  const auto str = genRandomStr(1024);

  auto writer = [&pub, &str]() -> folly::coro::Task<void> {
    auto msg = fbzmq::Message::from(str).value();
    size_t count{128};
    while (count--) {
      VLOG(1) << "Writer sending message " << count;
      auto ret = co_await pub.sendOneCoro(msg);
      EXPECT_TRUE(ret.hasValue());
      co_await folly::coro::sleep(
          std::chrono::milliseconds(folly::Random::rand32(0, 10)));
    }
    auto ret =
        co_await pub.sendOneCoro(fbzmq::Message::from(std::string("")).value());
    EXPECT_TRUE(ret.hasValue());
    co_return;
  };

  auto reader = [&sub, &str]() -> folly::coro::Task<size_t> {
    size_t count{0};
    while (true) {
      auto rcvd = co_await sub.recvOneCoro();
      VLOG(1) << "Reader received message " << count;
      auto data = rcvd.value().read<std::string>().value();
      if (data.size() == 0) {
        break;
      }
      ++count;
      EXPECT_EQ(str, data);
    }
    co_return count;
  };

  auto futReader = reader().scheduleOn(&evb).start();
  auto futWriter = writer().scheduleOn(&evb).start();
  evb.loop();

  EXPECT_TRUE(futReader.isReady());
  EXPECT_EQ(128, futReader.value());
  EXPECT_TRUE(futWriter.isReady());
}
#endif

TEST(Socket, FiberSingleMessage) {
  using namespace folly::fibers;

  folly::EventBase evb;
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  auto fm = std::make_unique<FiberManager>(
      std::make_unique<EventBaseLoopController>());
  static_cast<EventBaseLoopController&>(fm->loopController())
      .attachEventBase(evb);

  const auto str = genRandomStr(1024);

  auto sender = fm->addTaskFuture([&req, &str]() {
    VLOG(1) << "Start sender fiber";
    auto msg = fbzmq::Message::from(str).value();
    req.sendOne(msg);
  });

  auto receiver = fm->addTaskFuture([&rep, &str]() {
    VLOG(1) << "Start receiver fiber";
    auto rcvd = rep.recvOne();
    EXPECT_EQ(str, rcvd.value().read<std::string>().value());
  });

  evb.loop();

  // wait for fiber to finish
  std::move(sender).get();
  std::move(receiver).get();
}

TEST(Socket, FiberPubSubMessage) {
  using namespace folly::fibers;

  folly::EventBase evb;
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(
      ctx, folly::none, folly::none, fbzmq::NonblockingFlag{true}, &evb);
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(
      ctx, folly::none, folly::none, fbzmq::NonblockingFlag{true}, &evb);

  pub.bind(fbzmq::SocketUrl{"inproc:///tmp/test"}).value();
  sub.connect(fbzmq::SocketUrl{"inproc:///tmp/test"}).value();
  sub.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();

  auto fm = std::make_unique<FiberManager>(
      std::make_unique<EventBaseLoopController>());
  static_cast<EventBaseLoopController&>(fm->loopController())
      .attachEventBase(evb);

  const auto str = genRandomStr(1024);

  // receiver needs to drain all messages before put back to wait
  auto receiver = fm->addTaskFuture([&sub]() noexcept {
    LOG(INFO) << "Receiver starting";
    auto rcvdMsgCnt = 0;
    while (true) {
      auto rcvd = sub.recvOne();
      // if sender sends too fast we will receive >1 messages here
      ++rcvdMsgCnt;
      VLOG(1) << "Receiver got msg " << rcvdMsgCnt;
      if (rcvdMsgCnt == 1024) {
        break;
      }
    }
    LOG(INFO) << "Receiver terminating";
    SUCCEED();
  });

  auto sender = fm->addTaskFuture([&pub, &str, &evb]() noexcept {
    LOG(INFO) << "Sender starting";
    for (int i = 0; i < 1024; i++) {
      auto msg = fbzmq::Message::from(str).value();
      VLOG(1) << "Sender sending msg " << (i + 1);
      pub.sendOne(msg);
      // random wait before send next msg
      folly::fibers::Baton bt;
      auto timeout = folly::AsyncTimeout::schedule(
          std::chrono::milliseconds(folly::Random::rand32(0, 10)),
          evb,
          [&bt]() noexcept { bt.post(); });
      bt.wait();
      LOG(INFO) << "Sender terminating";
    }
  });

  LOG(INFO) << "Starting eventbase loop";
  evb.loop();
  LOG(INFO) << "eventbase loop terminated";

  // wait for fiber to finish
  std::move(sender).get();
  std::move(receiver).get();
}

TEST(Socket, FiberSubSocketClose) {
  using namespace folly::fibers;

  folly::EventBase evb;
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(
      ctx, folly::none, folly::none, fbzmq::NonblockingFlag{true}, &evb);

  sub.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  auto fm = std::make_unique<FiberManager>(
      std::make_unique<EventBaseLoopController>());
  static_cast<EventBaseLoopController&>(fm->loopController())
      .attachEventBase(evb);

  // both will block on waitToRead
  auto client =
      fm->addTaskFuture([&sub]() { EXPECT_TRUE(sub.recvOne().hasError()); });

  fm->addTask([&sub, &evb]() {
    // wait for 50ms before close the socket
    folly::fibers::Baton bt;
    auto timeout = folly::AsyncTimeout::schedule(
        std::chrono::milliseconds(50), evb, [&bt]() noexcept { bt.post(); });
    bt.wait();
    // close socket. Reader will get unblocked
    sub.close();
  });

  evb.loop();

  // wait for fiber to finish
  std::move(client).get();
}

TEST(Socket, FiberSendRecvMultiple) {
  using namespace folly::fibers;

  folly::EventBase evb;
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> pub(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(
      ctx, folly::none, folly::none, NonblockingFlag{true}, &evb);

  pub.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.connect(fbzmq::SocketUrl{"inproc://test"}).value();
  sub.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();

  auto fm = std::make_unique<FiberManager>(
      std::make_unique<EventBaseLoopController>());
  static_cast<EventBaseLoopController&>(fm->loopController())
      .attachEventBase(evb);

  auto sender = fm->addTaskFuture([&evb, &pub]() {
    VLOG(1) << "Start sender fiber";
    std::vector<fbzmq::Message> msgs{
        fbzmq::Message::from(genRandomStr(1024)).value(),
        fbzmq::Message::from(genRandomStr(1024)).value(),
        fbzmq::Message::from(genRandomStr(1024)).value(),
        fbzmq::Message::from(genRandomStr(1024)).value()};

    // Templatized send
    {
      LOG(INFO) << "Sending multiple messages as variadic args";
      auto ret = pub.sendMultiple(msgs.at(0), msgs.at(1), msgs.at(2));
      EXPECT_TRUE(ret.hasValue());
    }

    // Wait
    LOG(INFO) << "Sender sleeping for 100ms";
    folly::fibers::Baton bt;
    auto timeout = folly::AsyncTimeout::schedule(
        std::chrono::milliseconds(100), evb, [&bt]() noexcept { bt.post(); });
    bt.wait();

    // Send multiple with vector
    {
      LOG(INFO) << "Sending multiple messages as vector - continued";
      auto ret = pub.sendMultiple(msgs, true /* hasMore */);
      EXPECT_TRUE(ret.hasValue());
    }
    {
      LOG(INFO) << "Sending multiple messages as vector - done";
      auto ret = pub.sendMultiple(msgs, false /* hasMore */);
      EXPECT_TRUE(ret.hasValue());
    }
    LOG(INFO) << "Sender terminating";
  });

  auto receiver = fm->addTaskFuture([&sub]() {
    VLOG(1) << "Start receiver fiber";

    fbzmq::Message msg1, msg2, msg3;
    auto ret = sub.recvMultiple(msg1, msg2, msg3);
    LOG(INFO) << "Received messages with variadic args";
    EXPECT_TRUE(ret.hasValue());
    EXPECT_EQ(1024, msg1.size());
    EXPECT_EQ(1024, msg2.size());
    EXPECT_EQ(1024, msg3.size());

    // NOTE: We will receive all 8 parts in a single shot
    auto msgs = sub.recvMultiple();
    LOG(INFO) << "Received messages with vector list";
    ASSERT_TRUE(msgs.hasValue());
    EXPECT_EQ(8, msgs.value().size());

    LOG(INFO) << "Receiver terminating";
  });

  evb.loop();

  // wait for fiber to finish
  std::move(sender).get();
  std::move(receiver).get();
}

TEST(Socket, FiberRecvTimeout) {
  using namespace folly::fibers;

  folly::EventBase evb;
  auto fm = std::make_unique<FiberManager>(
      std::make_unique<EventBaseLoopController>());
  static_cast<EventBaseLoopController&>(fm->loopController())
      .attachEventBase(evb);

  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> sub(
      ctx, folly::none, folly::none, fbzmq::NonblockingFlag{true}, &evb);
  sub.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  // both will block on waitToRead
  auto client = fm->addTaskFuture([&sub]() {
    auto recvd = sub.recvOne(std::chrono::milliseconds(100));
    ASSERT_TRUE(recvd.hasError());
    EXPECT_EQ(EAGAIN, recvd.error().errNum);
  });

  evb.loop();

  // wait for fiber to finish
  std::move(client).get();
}

//
// Receive within timeout: blocking
//
TEST(Socket, MessageRecvTimeout) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(
      ctx, folly::none, folly::none, fbzmq::NonblockingFlag{false});
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(
      ctx, folly::none, folly::none, fbzmq::NonblockingFlag{false});

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  const auto str = genRandomStr(1024);
  auto msg = fbzmq::Message::from(str).value();
  req.sendOne(msg);

  {
    // expect a message indefinitely
    auto rcvd = rep.recvOne();
    EXPECT_EQ(str, rcvd.value().read<std::string>().value());
  }
  {
    // no message coming
    auto rcvd = rep.recvOne(1000ms);
    EXPECT_TRUE(rcvd.hasError());
  }
}

//
// Bounce random messages b/w req/rep sockets
//
TEST(Socket, PingPong) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(ctx);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(ctx);

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  for (int i = 0; i < 1024; i++) {
    {
      auto size = folly::Random::rand32(8192);
      auto msg = fbzmq::Message::allocate(size);
      req.sendOne(msg.value());

      std::vector<fbzmq::PollItem> pollItems = {
          {reinterpret_cast<void*>(*rep), 0, ZMQ_POLLIN, 0}};
      fbzmq::poll(pollItems);
      auto rcvd = rep.recvOne();
      EXPECT_EQ(size, rcvd.value().size());
    }

    {
      auto size = folly::Random::rand32(8192);
      auto msg = fbzmq::Message::allocate(size);
      rep.sendOne(msg.value());

      std::vector<fbzmq::PollItem> pollItems = {
          {reinterpret_cast<void*>(*req), 0, ZMQ_POLLIN, 0}};
      fbzmq::poll(pollItems);
      auto rcvd = req.recvOne();
      EXPECT_EQ(size, rcvd.value().size());
    }
  } // for
}

//
// Test the "more" flag b/w two dealer sockets
//
TEST(Socket, SendMore) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  {
    auto msg = fbzmq::Message::from(std::string("test1234"));
    client.sendMore(msg.value());
  }
  {
    auto msg = fbzmq::Message::from(std::string("test5678"));
    client.sendOne(msg.value());
  }

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems);

  {
    auto rcvd = server.recvOne();
    EXPECT_TRUE(server.hasMore());
    EXPECT_FALSE(rcvd.value().isLast());
    EXPECT_EQ("test1234", rcvd.value().read<std::string>().value());
  }

  {
    auto rcvd = server.recvOne();
    EXPECT_FALSE(server.hasMore());
    EXPECT_TRUE(rcvd.value().isLast());
    EXPECT_EQ("test5678", rcvd.value().read<std::string>().value());
  }
}

//
// Test reading/writing fundamental type
//
TEST(Socket, SendRecvInteger) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(ctx);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(ctx);

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  uint32_t const value =
      folly::Random::rand32(std::numeric_limits<uint32_t>::max());

  fbzmq::Message::from(value).then(
      [&req](fbzmq::Message&& msg) { req.sendOne(std::move(msg)); });

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*rep), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).then([&rep, &value](int) {
    rep.recvOne().then([&value](fbzmq::Message&& rcvd) {
      auto rcvdValue = rcvd.read<uint32_t>();
      EXPECT_EQ(value, rcvdValue.value());
    });
  });
}

//
// Thrift object handling
//

// SerDeser thrift directly into IOBuf's
TEST(Socket, ThriftSerDeser) {
  auto serializer = CompactSerializer();

  // The thrift "value" object
  fbzmq::test::TestValue clientVal;
  *clientVal.value_ref() = "test1234";

  auto rawClientVal = fbzmq::util::writeThriftObj(clientVal, serializer);

  // The wrapper we use to store something in KV database
  fbzmq::test::WrapperValue wrapperVal;
  *wrapperVal.version_ref() = 1000;
  *wrapperVal.value_ref() = *rawClientVal;

  auto rawWrapperVal = fbzmq::util::writeThriftObj(wrapperVal, serializer);
  auto newWrapperVal = fbzmq::util::readThriftObj<fbzmq::test::WrapperValue>(
      *(rawWrapperVal.get()), serializer);

  EXPECT_EQ(*newWrapperVal.version_ref(), 1000);

  auto newClientVal = fbzmq::util::readThriftObj<fbzmq::test::TestValue>(
      *newWrapperVal.value_ref(), serializer);

  EXPECT_EQ(*newClientVal.value_ref(), "test1234");
}

//
// Test thrift object ser/deser
//
TEST(Socket, ThriftSerDeserStr) {
  CompactSerializer serializer;
  fbzmq::test::TestValue clientVal;
  *clientVal.value_ref() = "hello world";

  auto str = fbzmq::util::writeThriftObjStr(clientVal, serializer);
  auto obj =
      fbzmq::util::readThriftObjStr<fbzmq::test::TestValue>(str, serializer);

  EXPECT_EQ(clientVal, obj);
}

//
// Test reading/writing a string
//
TEST(Socket, SendRecvString) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(ctx);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(ctx);
  CompactSerializer serializer;

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  auto value = genRandomStr(16384);

  fbzmq::Message::from(value).then(
      [&req](fbzmq::Message&& msg) { req.sendOne(std::move(msg)); });

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*rep), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).then([&serializer, &rep, &value](int) {
    rep.recvOne().then([&serializer, &value](fbzmq::Message&& rcvd) {
      auto rcvdValue = rcvd.read<std::string>();
      EXPECT_EQ(value, rcvdValue.value());
    });
  });
}

//
// Test reading/writing thrift object via sockets
//
TEST(Socket, SendRecvThriftObj) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(ctx);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(ctx);
  CompactSerializer serializer;

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  // The thrift "value" object
  const auto str = genRandomStr(32768);
  fbzmq::test::TestValue testValue;
  *testValue.value_ref() = str;

  fbzmq::Message::fromThriftObj(testValue, serializer)
      .then([&req](fbzmq::Message&& msg) { req.sendOne(std::move(msg)); });

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*rep), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).then([&serializer, &rep, &str](int) {
    rep.recvOne().then([&serializer, &str](fbzmq::Message&& rcvd) {
      auto rcvdValue = rcvd.readThriftObj<fbzmq::test::TestValue>(serializer);
      EXPECT_EQ(str, *rcvdValue.value().value_ref());
    });
  });
}

//
// Send lotsa objects back and forth b/w sockets
//
TEST(Socket, ThriftObjPingPong) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_REQ, fbzmq::ZMQ_CLIENT> req(ctx);
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> rep(ctx);
  CompactSerializer serializer;

  rep.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  req.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  for (int i = 0; i < 1024; i++) {
    {
      const auto str = genRandomStr(32768);
      fbzmq::test::TestValue testValue;
      *testValue.value_ref() = str;

      fbzmq::Message::fromThriftObj(testValue, serializer)
          .then([&req](fbzmq::Message&& msg) { req.sendOne(std::move(msg)); });

      std::vector<fbzmq::PollItem> pollItems = {
          {reinterpret_cast<void*>(*rep), 0, ZMQ_POLLIN, 0}};
      fbzmq::poll(pollItems).then([&serializer, &rep, &str](int) {
        rep.recvOne().then([&serializer, &str](fbzmq::Message&& rcvd) {
          auto rcvdValue =
              rcvd.readThriftObj<fbzmq::test::TestValue>(serializer);
          EXPECT_EQ(str, *rcvdValue.value().value_ref());
        });
      });
    }

    {
      const auto str = genRandomStr(32768);
      fbzmq::test::TestValue testValue;
      *testValue.value_ref() = str;

      fbzmq::Message::fromThriftObj(testValue, serializer)
          .then([&rep](fbzmq::Message&& msg) { rep.sendOne(std::move(msg)); });

      std::vector<fbzmq::PollItem> pollItems = {
          {reinterpret_cast<void*>(*req), 0, ZMQ_POLLIN, 0}};
      fbzmq::poll(pollItems).then([&serializer, &req, &str](int) {
        req.recvOne().then([&serializer, &str](fbzmq::Message&& rcvd) {
          auto rcvdValue =
              rcvd.readThriftObj<fbzmq::test::TestValue>(serializer);
          EXPECT_EQ(str, *rcvdValue.value().value_ref());
        });
      });
    }
  } // for
}

//
// Same as before, but different threads - one source, another sink
//
TEST(Socket, ThriftObjectSendManyTwoThreads) {
  fbzmq::Context ctx;
  CompactSerializer serializer;

  // this will be receiving thrift objects
  std::thread serverThread([&ctx, &serializer] {
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);
    server.bind(fbzmq::SocketUrl{"inproc://test"}).value();

    std::vector<fbzmq::PollItem> pollItems = {
        {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
    for (int i = 0; i < 1024; i++) {
      fbzmq::poll(pollItems);
      auto obj = server.recvOne()
                     .value()
                     .readThriftObj<fbzmq::test::TestValue>(serializer)
                     .value();
      EXPECT_EQ(32768, obj.value_ref()->size());
    }
    server.unbind(fbzmq::SocketUrl{"inproc://test"}).value();
  });

  // this will be sending thrift objects
  std::thread clientThread([&ctx, &serializer] {
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
    client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

    for (int i = 0; i < 1024; i++) {
      const auto str = genRandomStr(32768);
      fbzmq::test::TestValue testValue;
      *testValue.value_ref() = str;

      fbzmq::Message::fromThriftObj(testValue, serializer)
          .then([&client](fbzmq::Message&& msg) {
            client.sendOne(std::move(msg));
          });
    }

    client.disconnect(fbzmq::SocketUrl{"inproc://test"}).value();
  });

  serverThread.join();
  clientThread.join();
}

//
// Test message exchange over router/dealer socket
//
TEST(Socket, DealerRouter) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(
      ctx, fbzmq::IdentityString{"client"});
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  client.sendOne(fbzmq::Message::from(std::string("test")).value());
  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems);
  // message has arrived
  auto id = server.recvOne().value();
  ASSERT_FALSE(id.isLast());
  EXPECT_EQ("client", id.read<std::string>().value());
  ASSERT_TRUE(server.hasMore());
  auto msg = server.recvOne().value();
  ASSERT_TRUE(msg.isLast());
  const auto str = msg.read<std::string>().value();
  EXPECT_EQ("test", str);
  ASSERT_FALSE(server.hasMore());
}

//
// Receive multiple message in a row
//
TEST(Socket, RecvMultipleGood) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  client.sendMore(fbzmq::Message::from(std::string("test1")).value());
  client.sendMore(fbzmq::Message::from(std::string("test2")).value());
  client.sendOne(fbzmq::Message::from(std::string("test3")).value());

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  fbzmq::Message msg1, msg2, msg3;
  server.recvMultiple(msg1, msg2, msg3).value();

  EXPECT_EQ("test1", msg1.read<std::string>().value());
  EXPECT_EQ("test2", msg2.read<std::string>().value());
  EXPECT_EQ("test3", msg3.read<std::string>().value());
}

//
// Receive multiple message in a row, mismatch
//
TEST(Socket, RecvMultipleBad) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  client.sendMore(fbzmq::Message::from(std::string("test1")).value());
  client.sendOne(fbzmq::Message::from(std::string("test2")).value());

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  fbzmq::Message msg1, msg2, msg3;
  EXPECT_FALSE(server.recvMultiple(msg1, msg2, msg3).hasValue());

  // first two message were received nonetheless
  EXPECT_EQ("test1", msg1.read<std::string>().value());
  EXPECT_EQ("test2", msg2.read<std::string>().value());
}

//
// Receive multiple message in a row, dynamic version
//
TEST(Socket, RecvMultipleDynamic) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  client.sendMore(fbzmq::Message::from(std::string("test1")).value());
  client.sendMore(fbzmq::Message::from(std::string("test2")).value());
  client.sendOne(fbzmq::Message::from(std::string("test3")).value());

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  auto msgs = server.recvMultiple().value();

  ASSERT_EQ(3, msgs.size());
  EXPECT_EQ("test3", msgs.back().read<std::string>().value());
  msgs.pop_back();
  EXPECT_EQ("test2", msgs.back().read<std::string>().value());
  msgs.pop_back();
  EXPECT_EQ("test1", msgs.back().read<std::string>().value());
}

//
// Send multiple messages in a row
//
TEST(Socket, SendMultipleGood) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  {
    auto msg1 = fbzmq::Message::from(std::string("test1")).value();
    auto msg2 = fbzmq::Message::from(std::string("test2")).value();
    auto msg3 = fbzmq::Message::from(std::string("test3")).value();

    client.sendMultiple(msg1, msg2, msg3);
  }

  // send multipole empty list
  std::vector<Message> messages;
  EXPECT_EQ(0, client.sendMultiple(messages, false).value());

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  {
    auto str1 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test1", str1);
    EXPECT_TRUE(server.hasMore());
    auto str2 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test2", str2);
    EXPECT_TRUE(server.hasMore());
    auto str3 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test3", str3);
    EXPECT_FALSE(server.hasMore());
  }
}

//
// Send multiple messages in a row, last one has more flag set
//
TEST(Socket, SendMultipleMore) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  {
    auto msg1 = fbzmq::Message::from(std::string("test1")).value();
    auto msg2 = fbzmq::Message::from(std::string("test2")).value();
    auto msg3 = fbzmq::Message::from(std::string("test3")).value();

    client.sendMultipleMore(msg1, msg2, msg3);
    auto msg4 = fbzmq::Message::from(std::string("test4")).value();
    client.sendOne(msg4);
  }

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  {
    auto str1 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test1", str1);
    EXPECT_TRUE(server.hasMore());
    auto str2 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test2", str2);
    EXPECT_TRUE(server.hasMore());
    auto str3 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test3", str3);
    EXPECT_TRUE(server.hasMore());
  }

  {
    auto str4 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test4", str4);
    EXPECT_FALSE(server.hasMore());
  }
}

//
// Send multiple messages in a row, dynamic version
//
TEST(Socket, SendMultipleDynamic) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  std::vector<fbzmq::Message> msgs;
  {
    msgs.emplace_back(fbzmq::Message::from(std::string("test1")).value());
    msgs.emplace_back(fbzmq::Message::from(std::string("test2")).value());
    msgs.emplace_back(fbzmq::Message::from(std::string("test3")).value());

    client.sendMultiple(msgs).value();
  }

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  {
    auto str1 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test1", str1);
    EXPECT_TRUE(server.hasMore());
    auto str2 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test2", str2);
    EXPECT_TRUE(server.hasMore());
    auto str3 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test3", str3);
    EXPECT_FALSE(server.hasMore());
  }
}

//
// Send multiple messages in a row, dynamic version, more flag set
//
TEST(Socket, SendMultipleMoreDynamic) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client(ctx);
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server(ctx);

  server.bind(fbzmq::SocketUrl{"inproc://test"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://test"}).value();

  std::vector<fbzmq::Message> msgs;
  {
    msgs.emplace_back(fbzmq::Message::from(std::string("test1")).value());
    msgs.emplace_back(fbzmq::Message::from(std::string("test2")).value());
    msgs.emplace_back(fbzmq::Message::from(std::string("test3")).value());

    client.sendMultiple(msgs, true);
    auto const msg = fbzmq::Message::from(std::string("test4")).value();
    client.sendOne(msg);
  }

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
  fbzmq::poll(pollItems).value();

  {
    auto str1 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test1", str1);
    EXPECT_TRUE(server.hasMore());
    auto str2 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test2", str2);
    EXPECT_TRUE(server.hasMore());
    auto str3 = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test3", str3);
    EXPECT_TRUE(server.hasMore());
  }

  {
    auto str = server.recvOne().value().read<std::string>().value();
    EXPECT_EQ("test4", str);
    EXPECT_FALSE(server.hasMore());
  }
}

//
// Router socket connecting to multiple routers over inproc
//
TEST(Socket, MultipleRouters) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_CLIENT> client(
      ctx, fbzmq::IdentityString{"client"});
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> server1(
      ctx, fbzmq::IdentityString{"server1"});
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> server2(
      ctx, fbzmq::IdentityString{"server2"});

  server1.bind(fbzmq::SocketUrl{"inproc://server1"}).value();
  server2.bind(fbzmq::SocketUrl{"inproc://server2"}).value();

  client.connect(fbzmq::SocketUrl{"inproc://server1"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://server2"}).value();

  client
      .sendMultiple(
          fbzmq::Message::from(std::string("server1")).value(),
          fbzmq::Message::from(std::string("test1")).value())
      .value();

  client
      .sendMultiple(
          fbzmq::Message::from(std::string("server2")).value(),
          fbzmq::Message::from(std::string("test2")).value())
      .value();

  EXPECT_EQ(
      std::string("client"),
      server1.recvOne().value().read<std::string>().value());
  EXPECT_EQ(
      std::string("test1"),
      server1.recvOne().value().read<std::string>().value());

  EXPECT_EQ(
      std::string("client"),
      server2.recvOne().value().read<std::string>().value());
  EXPECT_EQ(
      std::string("test2"),
      server2.recvOne().value().read<std::string>().value());
}

//
// ROUTER socket failure to route unknown id
//
TEST(Socket, RouterFailure) {
  fbzmq::Context ctx;
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_CLIENT> client(
      ctx, fbzmq::IdentityString{"client"});
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> server1(
      ctx, fbzmq::IdentityString{"server1"});
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> server2(
      ctx, fbzmq::IdentityString{"server2"});

  server1.bind(fbzmq::SocketUrl{"inproc://server1"}).value();
  server2.bind(fbzmq::SocketUrl{"inproc://server2"}).value();

  client.connect(fbzmq::SocketUrl{"inproc://server1"}).value();
  client.connect(fbzmq::SocketUrl{"inproc://server2"}).value();

  // send message to unknown identity
  auto const ret = client.sendMultiple(
      fbzmq::Message::from(std::string("server3")).value(),
      fbzmq::Message::from(std::string("test1")).value());
  EXPECT_TRUE(ret.hasError());
}

//
// Crypto testing
//
TEST(CryptoSocket, NoCryptoKey) {
  fbzmq::Context ctx;

  auto kpClient = fbzmq::util::genKeyPair();
  auto kpServer = fbzmq::util::genKeyPair();

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client{
      ctx, fbzmq::IdentityString{"client"}, kpClient};
  LOG(INFO) << "Creating server socket";
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server{
      ctx, fbzmq::IdentityString{"server"}, kpServer};

  server.bind(fbzmq::SocketUrl{"inproc://server"}).value();

  client
      .addServerKey(
          fbzmq::SocketUrl{"inproc://server"},
          fbzmq::PublicKey{kpServer.publicKey})
      .value();

  // use wrong url to connect
  auto ret = client.connect(fbzmq::SocketUrl{"inproc://server1"});

  EXPECT_TRUE(ret.hasError());
}

//
// Pass encrypted messages b/w two sockets
//
TEST(CryptoSocket, Dealer2Dealer) {
  fbzmq::Context ctx;

  auto kpClient = fbzmq::util::genKeyPair();
  auto kpServer = fbzmq::util::genKeyPair();

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> client{
      ctx, fbzmq::IdentityString{"client"}, kpClient};
  LOG(INFO) << "Creating server socket";
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_SERVER> server{
      ctx, fbzmq::IdentityString{"server"}, kpServer};

  server.bind(fbzmq::SocketUrl{"inproc://server"}).value();

  client
      .addServerKey(
          fbzmq::SocketUrl{"inproc://server"},
          fbzmq::PublicKey{kpServer.publicKey})
      .value();
  client.connect(fbzmq::SocketUrl{"inproc://server"}).value();

  LOG(INFO) << "Client connected";

  {
    auto msg1 = fbzmq::Message::from(std::string("test1")).value();
    auto msg2 = fbzmq::Message::from(std::string("test2")).value();
    client.sendMultiple(msg1, msg2).value();
  }

  LOG(INFO) << "Sent all data";

  {
    std::vector<fbzmq::PollItem> pollItems = {
        {reinterpret_cast<void*>(*server), 0, ZMQ_POLLIN, 0}};
    fbzmq::poll(pollItems);
  }

  LOG(INFO) << "Data ready on server socket";

  {
    fbzmq::Message msg1, msg2;
    server.recvMultiple(msg1, msg2).value();
    EXPECT_EQ("test1", msg1.read<std::string>().value());
    EXPECT_EQ("test2", msg2.read<std::string>().value());
  }

  client.disconnect(fbzmq::SocketUrl{"inproc://server"}).value();
  client.delServerKey(fbzmq::SocketUrl{"inproc://server"});
}

//
// Publisher sends encrypted messages
//
TEST(CryptoSocket, Sub2Pub) {
  fbzmq::Context ctx;

  auto kpClient = fbzmq::util::genKeyPair();
  auto kpServer = fbzmq::util::genKeyPair();

  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> client{
      ctx, fbzmq::IdentityString{"client"}, kpClient};

  LOG(INFO) << "Creating server socket";

  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> server{
      ctx, fbzmq::IdentityString{"server"}, kpServer};

  server.bind(fbzmq::SocketUrl{"inproc://server"}).value();

  client
      .addServerKey(
          fbzmq::SocketUrl{"inproc://server"},
          fbzmq::PublicKey{kpServer.publicKey})
      .value();
  client.connect(fbzmq::SocketUrl{"inproc://server"}).value();
  client.setSockOpt(ZMQ_SUBSCRIBE, "", 0);

  std::vector<fbzmq::PollItem> pollItems = {
      {reinterpret_cast<void*>(*client), 0, ZMQ_POLLIN, 0}};

  //
  // Loop until we start receiving messages
  //
  uint32_t i = 0;
  while (true) {
    auto const msg = fbzmq::Message::from(i).value();
    server.sendOne(msg);
    auto ret = fbzmq::poll(pollItems, 50ms);
    if (ret.value() == 0) {
      i++;
      continue;
    }
    break;
  }

  // send 16 more messages on top of what we sent
  for (uint32_t j = i; j < i + 16; j++) {
    auto const msg = fbzmq::Message::from(j).value();
    server.sendOne(msg);
  }

  // read the first message available on SUB
  auto k = client.recvOne().value().read<uint32_t>().value();

  // read the remaining messages, relative to fisrt received
  while (true) {
    // it's okay to put zero timeout, since we know we have messages
    fbzmq::poll(pollItems);
    const auto m = client.recvOne().value().read<uint32_t>().value();
    if (m == i + 15) {
      break;
    }
    EXPECT_EQ(k, m);
    k++;
  }

  client.disconnect(fbzmq::SocketUrl{"inproc://server"}).value();
  client.delServerKey(fbzmq::SocketUrl{"inproc://server"});
}

} // namespace fbzmq

int
main(int argc, char* argv[]) {
  // Parse command line flags
  testing::InitGoogleTest(&argc, argv);
  folly::init(&argc, &argv);

  // init sodium security library
  if (::sodium_init() == -1) {
    LOG(ERROR) << "Failed initializing sodium";
    return -1;
  }

  // Run the tests
  return RUN_ALL_TESTS();
}
