#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

#
# Generate thrift defs for C++ only. For python install via setuptools
#

add_fbthrift_cpp_library(
  monitor_cpp2
  service/if/Monitor.thrift
  OPTIONS
    json
  THRIFT_INCLUDE_DIR
    include
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
  service/monitor/SystemMetrics.cpp
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

install(
  TARGETS fbzmq monitor_cpp2 monitor_cpp2.thrift_includes
  EXPORT fbzmq-exports
  DESTINATION lib
)

set(CMAKE_INSTALL_DIR lib/cmake/fbzmq CACHE STRING
    "The subdirectory where CMake package config files should be installed")

# Install CMake package configuration files for fbzmq
include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/fbzmq-config.cmake.in
  fbzmq-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DIR}
  PATH_VARS
    CMAKE_INSTALL_DIR
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/fbzmq-config.cmake
  DESTINATION ${CMAKE_INSTALL_DIR}
)
install(EXPORT fbzmq-exports
        FILE fbzmq-targets.cmake
        NAMESPACE fbzmq::
        DESTINATION ${CMAKE_INSTALL_DIR})


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
  service/monitor/SystemMetrics.h
  DESTINATION include/fbzmq/service/monitor
)

install(FILES
  service/stats/ExportedStat.h
  service/stats/ExportType.h
  service/stats/ThreadData.h
  DESTINATION include/fbzmq/service/stats
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/service/if/gen-cpp2/Monitor_constants.h
  ${CMAKE_CURRENT_BINARY_DIR}/service/if/gen-cpp2/Monitor_data.h
  ${CMAKE_CURRENT_BINARY_DIR}/service/if/gen-cpp2/Monitor_types.h
  ${CMAKE_CURRENT_BINARY_DIR}/service/if/gen-cpp2/Monitor_metadata.h
  DESTINATION include/fbzmq/service/if/gen-cpp2
)

install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/service/if/Monitor.thrift
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

  add_fbthrift_cpp_library(
    test_cpp2
    zmq/tests/Test.thrift
    OPTIONS
      json
  )

  add_fbthrift_cpp_library(
    example_cpp2
    examples/if/Example.thrift
    OPTIONS
      json
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
  add_executable(system_metrics_test
    zmq/tests/SystemMetricsTest.cpp
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
  target_link_libraries(system_metrics_test
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
  add_test(SystemMetricsTest system_metrics_test)

endif()
