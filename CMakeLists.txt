#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.2)
project(fbzmq)

if (NOT DEFINED CPACK_GENERATOR)
  set(CPACK_GENERATOR "RPM")
endif()
include(CPack)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-noexcept-type")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

set(
  CMAKE_MODULE_PATH
  "${CMAKE_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH}
)
include(FBThriftCppLibrary)

# External third-party libraries
find_library(DOUBLECONV double-conversion)
find_package(Gflags REQUIRED)
find_package(Glog REQUIRED)
find_library(PTHREAD pthread)
find_library(ZMQ zmq)
find_package(Threads REQUIRED)
find_package(Boost 1.67.0 MODULE
  COMPONENTS
    context
  REQUIRED
)

find_path(ZMQ_INCLUDE_DIR zmq.h)

include_directories(${Boost_INCLUDE_DIRS} ${ZMQ_INCLUDE_DIR})


# Facebook specific libraries
find_package(folly CONFIG REQUIRED)
find_library(ZSTD zstd)
find_package(fizz CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(wangle CONFIG REQUIRED)
find_package(FBThrift CONFIG REQUIRED)

# Include Thrift
include_directories(${FBTHRIFT_INCLUDE_DIR})

add_subdirectory(fbzmq)
