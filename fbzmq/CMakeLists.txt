cmake_minimum_required(VERSION 3.2)
project(fbzmq)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

include_directories(${CMAKE_SOURCE_DIR}/..)
include_directories(${CMAKE_BINARY_DIR})

# External third-party libraries
find_library(DOUBLECONV double-conversion)
find_library(GFLAGS gflags)
find_library(GLOG glog)
find_library(PTHREAD pthread)
find_library(SODIUM sodium)
find_library(ZMQ zmq)

# Facebook specific libraries
find_library(FOLLY folly PATHS)
find_library(ZSTD zstd)

# Include Thrift
find_program(THRIFT1 thrift1)
find_library(THRIFTCPP2 thriftcpp2)
find_library(THRIFTPROTOCOL thriftprotocol)
find_library(PROTOCOL protocol)
find_library(TRANSPORT transport)
find_path(THRIFT_INCLUDE thrift)
set(THRIFT_TEMPLATES ${THRIFT_INCLUDE}/thrift/templates)
include(${THRIFT_INCLUDE}/thrift/ThriftLibrary.cmake)

#
# Generate thrift defs for C++ only. For python install via setuptools
#
set(THRIFT_DIR ${CMAKE_BINARY_DIR}/fbzmq/service/if)
file(MAKE_DIRECTORY ${THRIFT_DIR})

thrift_object(
  "Monitor" #file_name
  "" #services
  "cpp2" #language
  "json,optionals" #options
  "${CMAKE_SOURCE_DIR}/service/if" #file_path
  "${THRIFT_DIR}" #output_path
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
  service/stats/ExportedStat.cpp
  service/stats/ThreadData.cpp
  zmq/Common.cpp
  zmq/Context.cpp
  zmq/Message.cpp
  zmq/Socket.cpp
  zmq/SocketMonitor.cpp
  $<TARGET_OBJECTS:Monitor-cpp2-obj>
)

if (BUILD_SHARED_LIBS)
  set_target_properties(fbzmq PROPERTIES VERSION 1.0.0 SOVERSION 1)
endif()

target_link_libraries(fbzmq
  ${DOUBLECONV}
  ${ZSTD}
  ${FOLLY}
  ${GFLAGS}
  ${GLOG}
  ${ZMQ}
  ${SODIUM}
  ${PTHREAD}
  ${THRIFTPROTOCOL}
  ${PROTOCOL}
  ${TRANSPORT}
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
  service/stats/ExportedStat.h
  service/stats/ExportType.h
  service/stats/ThreadData.h
  DESTINATION include/fbzmq/service/stats
)

install(FILES
  ${THRIFT_DIR}/gen-cpp2/Monitor_constants.h
  ${THRIFT_DIR}/gen-cpp2/Monitor_data.h
  ${THRIFT_DIR}/gen-cpp2/Monitor_types.h
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

  include_directories(${CMAKE_BINARY_DIR})

  find_library(GTEST gtest)
  find_library(GTEST_MAIN gtest_main)
  find_library(ZSTD zstd)

  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fbzmq)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fbzmq/examples/if)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fbzmq/zmq/tests)

  thrift_library(
    "Test" #file_name
    "" #services
    "cpp2" #language
    "json,optionals" #options
    "${CMAKE_SOURCE_DIR}/zmq/tests" #file_path
    "${CMAKE_BINARY_DIR}/fbzmq/zmq/tests" #output_path
  )

  thrift_library(
    "Example" #file_name
    "" #services
    "cpp2" #language
    "json,optionals" #options
    "${CMAKE_SOURCE_DIR}/examples/if" #file_path
    "${CMAKE_BINARY_DIR}/fbzmq/examples/if" #output_path
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
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(zmq_eventloop_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(zmq_throttle_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(zmq_timeout_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(common_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(context_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(message_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(socket_test
    fbzmq
    Test-cpp2
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(socket_monitor_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(log_sample_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(thread_data_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(zmq_monitor_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(zmq_monitor_client_test
    fbzmq
    ${GTEST}
    ${GTEST_MAIN}
  )
  target_link_libraries(zmq_monitor_sample
    fbzmq
  )
  target_link_libraries(zmq_server_example
    fbzmq
    Example-cpp2
  )
  target_link_libraries(zmq_client_example
    fbzmq
    Example-cpp2
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

endif()
