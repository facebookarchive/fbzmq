/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/Random.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <fbzmq/zmq/Message.h>

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

TEST(Message, Allocate) {
  auto msg = fbzmq::Message::allocate(128);
  EXPECT_TRUE(msg.hasValue());
  EXPECT_EQ(128, msg.value().size());
}

TEST(Message, isLastLocal) {
  auto msg = fbzmq::Message::allocate(128).value();
  EXPECT_TRUE(msg.isLast());
}

TEST(Message, CopyCtor) {
  auto msg1 = fbzmq::Message::from(genRandomStr(128)).value();
  fbzmq::Message msg2 = msg1;
  EXPECT_TRUE(
      std::equal(msg1.data().begin(), msg1.data().end(), msg2.data().begin()));
}

TEST(Message, MoveCtor) {
  auto msg1 = fbzmq::Message::allocate(128).value();
  fbzmq::Message msg2 = std::move(msg1);
  EXPECT_TRUE(msg1.empty());
  EXPECT_FALSE(msg2.empty());
}

TEST(Message, CopyAssign) {
  auto msg1 = fbzmq::Message::from(genRandomStr(128)).value();
  fbzmq::Message msg2;
  msg2 = msg1;
  EXPECT_TRUE(
      std::equal(msg1.data().begin(), msg1.data().end(), msg2.data().begin()));
}

TEST(Message, MoveAssign) {
  auto msg1 = fbzmq::Message::allocate(128).value();
  fbzmq::Message msg2;
  msg2 = std::move(msg1);
  EXPECT_EQ(128, msg2.size());
  EXPECT_TRUE(msg1.empty());
}

TEST(Message, WrapBuffer) {
  auto buf = folly::IOBuf::createCombined(128);
  // mark all space as used
  buf->append(128);
  {
    EXPECT_EQ(128, buf->length());
    auto result = fbzmq::Message::wrapBuffer(buf->clone());
    EXPECT_EQ(128, result->size());
    EXPECT_TRUE(buf->isShared());
    EXPECT_TRUE(result.hasValue());
  }
  // message has been destructed, IOBuf should be released
  EXPECT_FALSE(buf->isShared());
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
