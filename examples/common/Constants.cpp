/**
 * Copyright 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the license found in the
 * LICENSE-examples file in the root directory of this source tree.
 */

#include <fbzmq/examples/common/Constants.h>

namespace fbzmq {
namespace example {

constexpr folly::StringPiece Constants::kPrimitiveCmdUrl;

constexpr folly::StringPiece Constants::kStringCmdUrl;

constexpr folly::StringPiece Constants::kThriftCmdUrl;

constexpr folly::StringPiece Constants::kMultipleCmdUrl;

constexpr folly::StringPiece Constants::kPubUrl;

constexpr std::chrono::milliseconds Constants::kReadTimeout;

} // namespace example
} // namespace fbzmq
