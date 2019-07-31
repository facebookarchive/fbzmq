#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.2)
project(fbzmq)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-noexcept-type")

include_directories(${CMAKE_SOURCE_DIR}/..)
include_directories(${CMAKE_BINARY_DIR}/..)

include(${CMAKE_SOURCE_DIR}/build/fbcode_builder/CMake/ThriftCppLibrary.cmake)

# External third-party libraries
find_library(DOUBLECONV double-conversion)
find_package(Gflags REQUIRED)
find_package(Glog REQUIRED)
find_library(PTHREAD pthread)
find_library(ZMQ zmq)
find_library(SIGAR sigar)
find_package(Threads REQUIRED)
find_package(Boost 1.67.0 MODULE
  COMPONENTS
    context
    fiber
  REQUIRED
)

find_path(ZMQ_INCLUDE_DIR zmq.h)
find_path(SIGAR_INCLUDE_DIR sigar.h)

include_directories(${Boost_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIR} ${SIGAR_INCLUDE_DIR})


# Facebook specific libraries
find_package(folly CONFIG REQUIRED)
find_library(ZSTD zstd)
find_package(fizz CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(wangle CONFIG REQUIRED)
find_package(FBThrift CONFIG REQUIRED)
find_package(yarpl CONFIG REQUIRED)
find_package(rsocket CONFIG REQUIRED)

# Include Thrift
include_directories(${FBTHRIFT_INCLUDE_DIR})

#
# Generate thrift defs for C++ only. For python install via setuptools
#

add_thrift_cpp2_library(
  monitor_cpp2
  service/if/Monitor.thrift
  OPTIONS
    json
    optionals
)

#
# `fbzmq` library
#

add_library(fbzmq
  async/AsyncSignalHandler.cpp
  async/ZmqEventLoop.cpp
  async/ZmqThrottle.cpp
  async/ZmqTimeout.cpp
  service/logging/LogSample.cpp
  service/monitor/ZmqMonitorClient.cpp
  service/resource-monitor/ResourceMonitor.cpp
  service/stats/ExportedStat.cpp
  service/stats/ThreadData.cpp
  zmq/Common.cpp
  zmq/Context.cpp
  zmq/Message.cpp
  zmq/Socket.cpp
  zmq/SocketMonitor.cpp
)

if (BUILD_SHARED_LIBS)
  set_target_properties(fbzmq PROPERTIES VERSION 1.0.0 SOVERSION 1)
endif()

target_link_libraries(fbzmq
  monitor_cpp2
  ${ZSTD}
  Folly::folly
  ${DOUBLECONV}
  glog::glog
  gflags
  ${ZMQ}
  ${PTHREAD}
  FBThrift::thriftcpp2
  ${SIGAR}
  ${Boost_LIBRARIES}
  -ldl
)

target_include_directories(fbzmq PRIVATE
  ${FOLLY_INCLUDE_DIR}
  ${GLOG_INCLUDE_DIR}
  ${GFLAGS_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${CMAKE_PREFIX_PATH}
)

install(TARGETS
  fbzmq
  DESTINATION lib
)

install(FILES
  async/AsyncSignalHandler.h
  async/Runnable.h
  async/StopEventLoopSignalHandler.h
  async/ZmqEventLoop.h
  async/ZmqThrottle.h
  async/ZmqTimeout.h
  DESTINATION include/fbzmq/async
)

install(FILES
  zmq/Common.h
  zmq/Context.h
  zmq/Message.h
  zmq/Socket.h
  zmq/SocketMonitor.h
  zmq/Zmq.h
  DESTINATION include/fbzmq/zmq
)

install(FILES
  service/logging/LogSample.h
  DESTINATION include/fbzmq/service/logging
)

install(FILES
  service/monitor/ZmqMonitor.h
  service/monitor/ZmqMonitorClient.h
  DESTINATION include/fbzmq/service/monitor
)

install(FILES
  service/resource-monitor/ResourceMonitor.h
  DESTINATION include/fbzmq/service/resource-monitor
)

install(FILES
  service/stats/ExportedStat.h
  service/stats/ExportType.h
  service/stats/ThreadData.h
  DESTINATION include/fbzmq/service/stats
)

message("CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
install(FILES
  ${CMAKE_BINARY_DIR}/service/if/gen-cpp2/Monitor_constants.h
  ${CMAKE_BINARY_DIR}/service/if/gen-cpp2/Monitor_data.h
  ${CMAKE_BINARY_DIR}/service/if/gen-cpp2/Monitor_types.h
  DESTINATION include/fbzmq/service/if/gen-cpp2
)

install(FILES
  ${CMAKE_SOURCE_DIR}/service/if/Monitor.thrift
  DESTINATION include/fbzmq/service/if
)

#
# Unit-tests
#

option(BUILD_TESTS "BUILD_TESTS" ON)

if (BUILD_TESTS)

  enable_testing()

  find_package(GTest REQUIRED)
  find_library(ZSTD zstd)

  include_directories(${GTEST_INCLUDE_DIRS})

  add_thrift_cpp2_library(
    test_cpp2
    zmq/tests/Test.thrift
    OPTIONS
      json
      optionals
  )

  add_thrift_cpp2_library(
    example_cpp2
    examples/if/Example.thrift
    OPTIONS
      json
      optionals
  )

  add_executable(signal_handler_test
    async/tests/AsyncSignalHandlerTest.cpp
  )
  add_executable(zmq_eventloop_test
    async/tests/ZmqEventLoopTest.cpp
  )
  add_executable(zmq_throttle_test
    async/tests/ZmqThrottleTest.cpp
  )
  add_executable(zmq_timeout_test
    async/tests/ZmqTimeoutTest.cpp
  )
  add_executable(common_test
    zmq/tests/CommonTest.cpp
  )
  add_executable(context_test
    zmq/tests/ContextTest.cpp
  )
  add_executable(message_test
    zmq/tests/MessageTest.cpp
  )
  add_executable(socket_test
    zmq/tests/SocketTest.cpp
  )
  add_executable(socket_monitor_test
    zmq/tests/SocketMonitorTest.cpp
  )
  add_executable(log_sample_test
    service/logging/tests/LogSampleTest.cpp
  )
  add_executable(thread_data_test
    service/stats/tests/ThreadDataTest.cpp
  )
  add_executable(zmq_monitor_test
    service/monitor/tests/ZmqMonitorTest.cpp
  )
  add_executable(zmq_monitor_client_test
    service/monitor/tests/ZmqMonitorClientTest.cpp
  )
  add_executable(resource_monitor_test
    zmq/tests/ResourceMonitorTest.cpp
  )
  add_executable(zmq_monitor_sample
    service/monitor/ZmqMonitorSample.cpp
  )
  add_executable(zmq_server_example
    examples/common/Constants.cpp
    examples/server/ZmqServer.cpp
    examples/server/ZmqServerMain.cpp
  )
  add_executable(zmq_client_example
    examples/common/Constants.cpp
    examples/client/ZmqClient.cpp
    examples/client/ZmqClientMain.cpp
  )

  target_link_libraries(signal_handler_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(zmq_eventloop_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(zmq_throttle_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(zmq_timeout_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(common_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(context_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(message_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(socket_test
    fbzmq
    test_cpp2
    Folly::folly
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(socket_monitor_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(log_sample_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(thread_data_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(zmq_monitor_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(zmq_monitor_client_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(resource_monitor_test
    fbzmq
    GTest::GTest
    GTest::Main
  )
  target_link_libraries(zmq_monitor_sample
    fbzmq
  )
  target_link_libraries(zmq_server_example
    fbzmq
    example_cpp2
  )
  target_link_libraries(zmq_client_example
    fbzmq
    example_cpp2
  )

  add_test(SignalHandlerTest signal_handler_test)
  add_test(ZmqEventLoopTest zmq_eventloop_test)
  add_test(ZmqThrottleTest zmq_throttle_test)
  add_test(ZmqTimeoutTest zmq_timeout_test)
  add_test(CommonTest common_test)
  add_test(ContextTest context_test)
  add_test(MessageTest message_test)
  add_test(SocketTest socket_test)
  add_test(SocketMonitorTest socket_monitor_test)
  add_test(LogSampleTest log_sample_test)
  add_test(ThreadDataTest thread_data_test)
  add_test(ZmqMonitorTest zmq_monitor_test)
  add_test(ZmqMonitorClientTest zmq_monitor_client_test)
  add_test(ResourceMonitorTest resource_monitor_test)

endif()
