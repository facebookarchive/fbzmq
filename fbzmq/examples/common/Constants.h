/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#pragma once

#include <folly/Range.h>
#include <chrono>
#include <string>

namespace fbzmq {
namespace example {

class Constants {
 public:
  // the zmq url for request/reply primitive message
  static constexpr folly::StringPiece kPrimitiveCmdUrl =
      "tcp://127.0.0.1:55555";

  // the zmq url for request/reply string message
  static constexpr folly::StringPiece kStringCmdUrl = "tcp://127.0.0.1:55556";

  // the zmq url for request/reply thrift message
  static constexpr folly::StringPiece kThriftCmdUrl = "tcp://127.0.0.1:55557";

  // the zmq url for request/reply multiple message
  static constexpr folly::StringPiece kMultipleCmdUrl = "tcp://127.0.0.1:55558";

  // the zmq url for subscribe/publish primitive message
  static constexpr folly::StringPiece kPubUrl = "tcp://127.0.0.1:55559";

  // the default I/O read timeout in milliseconds
  static constexpr std::chrono::milliseconds kReadTimeout{500};
};

} // namespace example
} // namespace fbzmq
