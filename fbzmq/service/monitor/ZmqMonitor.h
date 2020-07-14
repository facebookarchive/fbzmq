/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include <boost/serialization/strong_typedef.hpp>
#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/service/logging/LogSample.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Optional.h>
#include <folly/gen/Base.h>
#include <thrift/lib/cpp2/Thrift.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "SystemMetrics.h"

namespace fbzmq {

using CounterMap =
    std::unordered_map<std::string /* counter name */, thrift::Counter>;

using CounterTimestampMap = std::unordered_map<
    std::string /* counter name */,
    std::pair<
        thrift::Counter /* counter value */,
        std::chrono::steady_clock::time_point /* last update ts */>>;
const std::chrono::seconds kAlivenessCheckInterval{180};
const std::chrono::seconds kProfilingStatInterval{5};
const size_t kMaxLogEvents{100};

class ZmqMonitor final : public ZmqEventLoop {
 public:
  ZmqMonitor(
      const std::string& monitorSubmitUrl,
      const std::string& monitorPubUrl,
      Context& zmqContext,
      const folly::Optional<LogSample>& logSampleToMerge = folly::none,
      const std::chrono::seconds alivenessCheckInterval =
          kAlivenessCheckInterval,
      const size_t maxLogEvents = kMaxLogEvents,
      const std::chrono::seconds profilingStatInterval = kProfilingStatInterval)
      : monitorSubmitUrl_(monitorSubmitUrl),
        monitorPubUrl_(monitorPubUrl),
        monitorReceiveSock_{zmqContext},
        monitorPubSock_{zmqContext},
        startTime_{std::chrono::steady_clock::now()},
        alivenessCheckInterval_{alivenessCheckInterval},
        maxLogEvents_{maxLogEvents},
        logSampleToMerge_{logSampleToMerge} {
    // Schedule periodic timer for counters aliveness check
    const bool isPeriodic = true;
    monitorTimer_ = fbzmq::ZmqTimeout::make(
        this, [this]() noexcept { purgeStaleCounters(); });
    monitorTimer_->scheduleTimeout(alivenessCheckInterval_, isPeriodic);
    updateMemStat();
    updateCpuStat();
    profilingTimer_ = fbzmq::ZmqTimeout::make(
        this, [this]() noexcept { updateResourceStats(); });
    profilingTimer_->scheduleTimeout(profilingStatInterval, isPeriodic);

    // Prepare router socket to talk to Broker/other processes
    const int handover = 1;
    const auto handoverRet = monitorReceiveSock_.setSockOpt(
        ZMQ_ROUTER_HANDOVER, &handover, sizeof(int));
    if (handoverRet.hasError()) {
      LOG(FATAL) << "ZmqMonitor: Could not set ZMQ_ROUTER_HANDOVER "
                 << handoverRet.error();
    }

    // bind monitor router socket
    VLOG(2) << "ZmqMonitor: Binding to monitorSubmitUrl '" << monitorSubmitUrl_
            << "'";
    const auto receiveBindRet =
        monitorReceiveSock_.bind(SocketUrl{monitorSubmitUrl_});
    if (receiveBindRet.hasError()) {
      LOG(FATAL) << "ZmqMonitor: Error binding to '" << monitorSubmitUrl_
                 << "' " << receiveBindRet.error();
    }

    // Prepare PUB socket for updating monitor
    const int hwm = 1024;
    const auto hwmRet =
        monitorPubSock_.setSockOpt(ZMQ_SNDHWM, &hwm, sizeof(int));
    if (hwmRet.hasError()) {
      LOG(FATAL) << "ZmqMonitor: Could not set ZMQ_SNDHWM " << hwmRet.error();
    }

    // bind monitor pub socket
    // bind monitor router socket
    VLOG(2) << "ZmqMonitor: Binding to monitorPubUrl '" << monitorPubUrl_
            << "'";
    const auto pubBindRet = monitorPubSock_.bind(SocketUrl{monitorPubUrl_});
    if (pubBindRet.hasError()) {
      LOG(FATAL) << "ZmqMonitor: Error binding to '" << monitorPubUrl_ << "' "
                 << pubBindRet.error();
    }

    // Attach callback on monitor socket for read events
    addSocket(
        RawZmqSocketPtr{*monitorReceiveSock_},
        ZMQ_POLLIN,
        [this](int /* revents */) noexcept {
          VLOG(4) << "ZmqMonitor: monitor request received...";
          try {
            processRequest();
          } catch (std::exception const& e) {
            LOG(ERROR) << "Error processing MonitorRequest: "
                       << folly::exceptionStr(e);
          }
        });
  }

 private:
  ZmqMonitor(ZmqMonitor const&) = delete;
  ZmqMonitor& operator=(ZmqMonitor const&) = delete;
  std::list<thrift::EventLog> eventLogs_;

  // update stats from within ZmqMonitor
  void
  updateResourceStats() {
    runImmediatelyOrInEventLoop([&]() {
      updateMemStat();
      updateCpuStat();
    });
  }

  // update memory stat using getrusage
  void
  updateMemStat() {
    std::string key{"process.memory.rss"};
    auto rssMem = systemMetrics_.getRSSMemBytes();
    if (rssMem.has_value()) {
      *counters_[key].first.value_ref() = static_cast<uint64_t>(rssMem.value());
      *counters_[key].first.valueType_ref() =
          fbzmq::thrift::CounterValueType::GAUGE;
      *counters_[key].first.timestamp_ref() = getCurrentMilliTime();
      counters_[key].second = std::chrono::steady_clock::now();
    }
  }

  // update CPU stat using getrusage
  void
  updateCpuStat() {
    std::string key{"process.cpu.pct"};
    auto cpuPct = systemMetrics_.getCPUpercentage();
    if (cpuPct.has_value()) {
      *counters_[key].first.value_ref() = static_cast<double>(cpuPct.value());
      *counters_[key].first.valueType_ref() =
          fbzmq::thrift::CounterValueType::GAUGE;
      *counters_[key].first.timestamp_ref() = getCurrentMilliTime();
      counters_[key].second = std::chrono::steady_clock::now();
    }
  }

  // process a monitor request pending oni monitorReceiveSock_
  void
  processRequest() {
    thrift::CounterValuesResponse thriftValueRep;
    thrift::CounterNamesResponse thriftNameRep;
    thrift::MonitorPub thriftPub;

    Message requestIdMsg, thriftReqMsg;
    const auto ret =
        monitorReceiveSock_.recvMultiple(requestIdMsg, thriftReqMsg);
    if (ret.hasError()) {
      LOG(ERROR) << "processRequest: Error receiving command: " << ret.error();
      return;
    }

    // read the request id supplied by router socket
    const auto requestId = requestIdMsg.read<std::string>().value();

    // read actual request
    const auto maybeThriftReq =
        thriftReqMsg.readThriftObj<thrift::MonitorRequest>(serializer_);

    if (maybeThriftReq.hasError()) {
      LOG(ERROR) << "processRequest: failed reading thrift::MonitorRequest "
                 << maybeThriftReq.error();
      return;
    }

    const auto thriftReq = maybeThriftReq.value();
    const auto now = std::chrono::steady_clock::now();

    // Always update uptime counter counter
    const std::string kUptimeCounter{"process.uptime.seconds"};
    counters_[kUptimeCounter] = std::make_pair(
        [&] {
          thrift::Counter counter;
          *counter.value_ref() =
              std::chrono::duration_cast<std::chrono::seconds>(now - startTime_)
                  .count();
          *counter.valueType_ref() = thrift::CounterValueType::COUNTER;
          *counter.timestamp_ref() =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  now.time_since_epoch())
                  .count();
          return counter;
        }(),
        now);

    switch (*thriftReq.cmd_ref()) {
    case thrift::MonitorCommand::SET_COUNTER_VALUES: {
      for (auto const& kv : *thriftReq.counterSetParams_ref()->counters_ref()) {
        counters_[kv.first].first = kv.second;
        counters_[kv.first].second = now;
      }
      // Dump new monitor values to the publish socket.
      *thriftPub.pubType_ref() = thrift::PubType::COUNTER_PUB;
      *thriftPub.counterPub_ref()->counters_ref() =
          *thriftReq.counterSetParams_ref()->counters_ref();
      monitorPubSock_.sendOne(
          Message::fromThriftObj(thriftPub, serializer_).value());
    } break;

    case thrift::MonitorCommand::GET_COUNTER_VALUES:
      for (auto const& counterName :
           *thriftReq.counterGetParams_ref()->counterNames_ref()) {
        auto it = counters_.find(counterName);
        if (it != counters_.end()) {
          thriftValueRep.counters_ref()[counterName] = it->second.first;
        }
      }
      monitorReceiveSock_.sendMultiple(
          requestIdMsg,
          Message::fromThriftObj(thriftValueRep, serializer_).value());
      break;

    case thrift::MonitorCommand::DUMP_ALL_COUNTER_NAMES:
      *thriftNameRep.counterNames_ref() = folly::gen::from(counters_) |
          folly::gen::get<0>() | folly::gen::as<std::vector<std::string>>();
      monitorReceiveSock_.sendMultiple(
          requestIdMsg,
          Message::fromThriftObj(thriftNameRep, serializer_).value());
      break;

    case thrift::MonitorCommand::DUMP_ALL_COUNTER_DATA:
      for (auto const& kv : counters_) {
        thriftValueRep.counters_ref()->emplace(kv.first, kv.second.first);
      }
      monitorReceiveSock_.sendMultiple(
          requestIdMsg,
          Message::fromThriftObj(thriftValueRep, serializer_).value());
      break;

    case thrift::MonitorCommand::BUMP_COUNTER: {
      for (auto const& name :
           *thriftReq.counterBumpParams_ref()->counterNames_ref()) {
        if (counters_.find(name) == counters_.end()) {
          thrift::Counter counter;
          *counter.value_ref() = 0;
          *counter.valueType_ref() = thrift::CounterValueType::COUNTER;
          *counter.timestamp_ref() = std::time(nullptr);
          counters_.emplace(name, std::make_pair(counter, now));
        }
        auto& counter = counters_[name].first;
        ++(*counter.value_ref());
        counters_[name].second = now;
        thriftPub.counterPub_ref()->counters_ref()->emplace(name, counter);
      }
      // Dump new counter values to the publish socket.
      *thriftPub.pubType_ref() = thrift::PubType::COUNTER_PUB;
      monitorPubSock_.sendOne(
          Message::fromThriftObj(thriftPub, serializer_).value());
    } break;

    case thrift::MonitorCommand::LOG_EVENT:
      // simply forward, do not store logs
      *thriftPub.pubType_ref() = thrift::PubType::EVENT_LOG_PUB;
      *thriftPub.eventLogPub_ref() = std::move(*thriftReq.eventLog_ref());
      if (logSampleToMerge_) {
        for (auto& sample : *thriftPub.eventLogPub_ref()->samples_ref()) {
          try {
            // throws if this sample doesn't have a timestamp
            // in that case, lets just pass this sample along without appending
            auto ls = LogSample::fromJson(sample);
            ls.mergeSample(*logSampleToMerge_);
            sample = ls.toJson();
          } catch (...) {
          }
        }
      }
      // save the event log in local queue
      if (eventLogs_.size() >= maxLogEvents_) {
        eventLogs_.pop_front();
      }
      eventLogs_.push_back(*thriftPub.eventLogPub_ref());
      monitorPubSock_.sendOne(
          Message::fromThriftObj(thriftPub, serializer_).value());
      break;

    case thrift::MonitorCommand::GET_EVENT_LOGS: {
      thrift::EventLogsResponse thriftEventLogsRep;
      for (auto it = eventLogs_.begin(); it != eventLogs_.end(); ++it) {
        thriftEventLogsRep.eventLogs_ref()->emplace_back(*it);
      }
      monitorReceiveSock_.sendMultiple(
          requestIdMsg,
          Message::fromThriftObj(thriftEventLogsRep, serializer_).value());
    } break;

    default:
      LOG(ERROR) << "Unknown monitor command received";
    }

    VLOG(4) << "processMonitorRequest has finished";
  }

  // Check last update timestamp of each counter
  // If the counter is not active for long time, remove this counter
  void
  purgeStaleCounters() {
    // Scan through all counters to find out those have not been updated for
    // longer than alivenessCheckInterval
    auto const& current = std::chrono::steady_clock::now();

    for (auto it = counters_.begin(); it != counters_.end();) {
      auto const& key = it->first;
      auto const& lastTs = it->second.second;
      // remove expired counter
      if (current - lastTs > alivenessCheckInterval_) {
        LOG(INFO) << "Expired Counter: " << key;
        it = counters_.erase(it);
        continue;
      }
      ++it;
    }
  }

  // get current timestamp (in milliseconds)
  uint64_t
  getCurrentMilliTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // Timer for checking counter aliveness periodically
  std::unique_ptr<ZmqTimeout> monitorTimer_;

  std::unique_ptr<ZmqTimeout> profilingTimer_;
  const std::string monitorSubmitUrl_;
  const std::string monitorPubUrl_;

  Socket<ZMQ_ROUTER, ZMQ_SERVER> monitorReceiveSock_;
  Socket<ZMQ_PUB, ZMQ_SERVER> monitorPubSock_;

  // the serializer/deserializer helper we'll be using
  apache::thrift::CompactSerializer serializer_;

  // track critical statistics, e.g., number of times functions are called
  CounterTimestampMap counters_;

  // Start timestamp
  const std::chrono::steady_clock::time_point startTime_;

  // time interval of counter aliveness check
  const std::chrono::seconds alivenessCheckInterval_;

  // Number of last log events to queue
  const size_t maxLogEvents_{0};

  // LogSample to merge to each LogSample we recv
  const folly::Optional<LogSample> logSampleToMerge_;

  // Get the system metrics for resource usage counters
  fbzmq::SystemMetrics systemMetrics_{};
};

} // namespace fbzmq
