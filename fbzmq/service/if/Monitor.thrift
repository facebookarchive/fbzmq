/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp fbzmq.thrift
namespace cpp2 fbzmq.thrift
namespace go fbzmq.Monitor
namespace php Fbzmq
namespace py fbzmq.Monitor
namespace py3 fbzmq.thrift

enum MonitorCommand {
  // operations on counters in the monitor
  SET_COUNTER_VALUES = 1,
  GET_COUNTER_VALUES = 2,
  DUMP_ALL_COUNTER_NAMES = 3,
  DUMP_ALL_COUNTER_DATA = 4,
  BUMP_COUNTER = 5,
  GET_EVENT_LOGS = 6,

  // operations on logs, which are not saved in the monitor
  LOG_EVENT = 11,
}

//
// cmd parameters for client threads to update their counters
//

// counter value type for calculating rates
enum CounterValueType {
  GAUGE = 1,
  COUNTER = 2
}

// store a counter or gauge value with timestamp
struct Counter {
  10: double value
  11: CounterValueType valueType
  // last update timestamp in microseconds
  12: i64 timestamp
}
typedef map<string, Counter>
  (cpp.type = "std::unordered_map<std::string, fbzmq::thrift::Counter>")
  CounterMap

// parameters for SET_COUNTER_VALUES command
struct CounterSetParams {
  // counter name -> Counter struct
  1: CounterMap counters
}

// parameters for GET_COUNTER_VALUES command
struct CounterGetParams {
  1: list<string> counterNames
}

// parameters for BUMP_COUNTER command
struct CounterBumpParams {
  1: list<string> counterNames
}

// parameters for LOG_EVENT
struct EventLog {
  // name/id of the event log
  1: string category
  2: list<string> samples
}

//
// Request specification
//

// a request to the server (tagged union)
struct MonitorRequest {
  1: MonitorCommand cmd
  2: CounterSetParams counterSetParams
  3: CounterGetParams counterGetParams
  4: CounterBumpParams counterBumpParams
  5: EventLog eventLog
}

//
// Responses
//

struct CounterValuesResponse {
  1: CounterMap counters
}

struct EventLogsResponse {
  1: list<EventLog> eventLogs
}

struct CounterNamesResponse {
  1: list<string> counterNames
}

//
// Publication
//

enum PubType {
  COUNTER_PUB = 1,
  EVENT_LOG_PUB = 2,
}

struct MonitorPub {
  1: PubType pubType
  2: CounterValuesResponse counterPub
  3: EventLog eventLogPub
}
