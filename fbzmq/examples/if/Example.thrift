/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

namespace cpp2 fbzmq.example.thrift

enum Command {
  KEY_SET = 1,
  KEY_GET = 2,
}

// a request to the server
struct Request {
  1: required Command cmd,
  2: required string key,
  // value not used if it's a KEY_GET command
  3: optional i64 value,
}

// a response back to client
struct Response {
  1: required bool success,
  // value not used if it's a KEY_SET command
  2: optional i64 value = 99,
}

struct StrValue {
  1: string value;
}
