/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace fbzmq {

enum ExportType {
  SUM = 0x01,
  AVG = 0x02,
  // MIN        = 0x04,   // Not Available yet
  // MAX        = 0x08,   // Not Available yet
  RATE = 0x10,
  COUNT = 0x20,
  COUNT_RATE = 0x40
};
} // namespace fbzmq
