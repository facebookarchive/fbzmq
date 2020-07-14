/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ZmqMonitorClient.h"

namespace fbzmq {

ZmqMonitorClient::ZmqMonitorClient(
    Context& zmqContext,
    const std::string& monitorSubmitUrl,
    std::string const& socketId)
    : monitorCmdUrl_(std::move(monitorSubmitUrl)),
      monitorCmdSock_{
          zmqContext, folly::none, folly::none, NonblockingFlag{false}} {
  if (!socketId.empty()) {
    const auto idRet = monitorCmdSock_.setSockOpt(
        ZMQ_IDENTITY, socketId.c_str(), socketId.length());
    if (idRet.hasError()) {
      LOG(FATAL) << "Error setting ZMQ_IDENTITY to " << socketId << " "
                 << idRet.error();
    }
  }
  // allow for empty url mainly in unit tests
  if (!monitorCmdUrl_.empty()) {
    if (monitorCmdSock_.connect(SocketUrl{monitorCmdUrl_}).hasError()) {
      LOG(FATAL) << "Error connecting to monitor '" << monitorCmdUrl_ << "'";
    }
  }
}

void
ZmqMonitorClient::setCounter(
    std::string const& name, thrift::Counter const& counter) {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::SET_COUNTER_VALUES;
  thriftReq.counterSetParams_ref()->counters_ref()->emplace(name, counter);

  const auto ret = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (ret.hasError()) {
    LOG(ERROR) << "setCounter: error sending message " << ret.error();
  }
}

void
ZmqMonitorClient::setCounters(CounterMap const& counters) {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::SET_COUNTER_VALUES;
  *thriftReq.counterSetParams_ref()->counters_ref() = counters;

  const auto ret = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (ret.hasError()) {
    LOG(ERROR) << "setCounters: error sending message " << ret.error();
  }
}

folly::Optional<thrift::Counter>
ZmqMonitorClient::getCounter(std::string const& name) {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::GET_COUNTER_VALUES;
  thriftReq.counterGetParams_ref()->counterNames_ref()->emplace_back(name);

  const auto sendRet = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (sendRet.hasError()) {
    LOG(ERROR) << "getCounter: error sending message " << sendRet.error();
    return folly::none;
  }

  const auto respMsg = monitorCmdSock_.recvOne();
  if (respMsg.hasError()) {
    LOG(ERROR) << "getCounter: error receiving message " << respMsg.error();
    return folly::none;
  }

  const auto response =
      respMsg.value().readThriftObj<thrift::CounterValuesResponse>(serializer_);
  if (response.hasError()) {
    LOG(ERROR) << "getCounter: error reading message" << response.error();
    return folly::none;
  }

  const auto& counters = *response.value().counters_ref();
  const auto it = counters.find(name);
  if (it == counters.end()) {
    return folly::none;
  }
  return it->second;
}

std::vector<std::string /* name */>
ZmqMonitorClient::dumpCounterNames() {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_NAMES;

  const auto sendRet = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (sendRet.hasError()) {
    LOG(ERROR) << "dumpCounterNames: error sending message " << sendRet.error();
    return {};
  }

  const auto respMsg = monitorCmdSock_.recvOne();
  if (respMsg.hasError()) {
    LOG(ERROR) << "dumpCounterNames: error receiving message "
               << respMsg.error();
    return {};
  }

  const auto response =
      respMsg.value().readThriftObj<thrift::CounterNamesResponse>(serializer_);
  if (response.hasError()) {
    LOG(ERROR) << "dumpCounterNames: error reading message" << response.error();
    return {};
  }

  return *response.value().counterNames_ref();
}

CounterMap
ZmqMonitorClient::dumpCounters() {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::DUMP_ALL_COUNTER_DATA;

  CounterMap emptyCounterMap;

  const auto sendRet = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (sendRet.hasError()) {
    LOG(ERROR) << "dumpCounters: error sending message " << sendRet.error();
    return emptyCounterMap;
  }

  const auto respMsg = monitorCmdSock_.recvOne();
  if (respMsg.hasError()) {
    LOG(ERROR) << "dumpCounters: error receiving message " << respMsg.error();
    return emptyCounterMap;
  }

  const auto response =
      respMsg.value().readThriftObj<thrift::CounterValuesResponse>(serializer_);
  if (response.hasError()) {
    LOG(ERROR) << "dumpCounters: error reading message" << response.error();
    return emptyCounterMap;
  }

  return *response.value().counters_ref();
}

void
ZmqMonitorClient::bumpCounter(std::string const& name) {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::BUMP_COUNTER;
  thriftReq.counterBumpParams_ref()->counterNames_ref()->emplace_back(name);

  const auto ret = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (ret.hasError()) {
    LOG(ERROR) << "bumpCounter: error sending message " << ret.error();
    return;
  }
}

void
ZmqMonitorClient::addEventLog(thrift::EventLog const& eventLog) {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::LOG_EVENT;
  *thriftReq.eventLog_ref() = eventLog;

  const auto ret = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (ret.hasError()) {
    LOG(ERROR) << "addEventLog: error sending message " << ret.error();
    return;
  }
}

folly::Optional<std::vector<thrift::EventLog>>
ZmqMonitorClient::getLastEventLogs() {
  thrift::MonitorRequest thriftReq;
  *thriftReq.cmd_ref() = thrift::MonitorCommand::GET_EVENT_LOGS;

  const auto ret = monitorCmdSock_.sendOne(
      Message::fromThriftObj(thriftReq, serializer_).value());
  if (ret.hasError()) {
    LOG(ERROR) << "getLastEventLogs: error sending message " << ret.error();
    return folly::none;
  }

  const auto respMsg = monitorCmdSock_.recvOne();
  if (respMsg.hasError()) {
    LOG(ERROR) << "getLastEventLogs: error receiving message "
               << respMsg.error();
    return folly::none;
  }

  const auto response =
      respMsg.value().readThriftObj<thrift::EventLogsResponse>(serializer_);
  if (response.hasError()) {
    LOG(ERROR) << "getLastEventLogs: error reading message" << response.error();
    return folly::none;
  }

  return *response.value().eventLogs_ref();
}

} // namespace fbzmq
