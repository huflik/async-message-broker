cmake_minimum_required(VERSION 3.15)
project(AsyncMessageBroker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Boost
find_package(Boost 1.82 REQUIRED COMPONENTS system)

# ZeroMQ
find_library(ZMQ_LIB NAMES zmq)
find_path(ZMQ_INCLUDE NAMES zmq.hpp)

if(NOT ZMQ_LIB OR NOT ZMQ_INCLUDE)
    message(FATAL_ERROR "ZeroMQ not found. Install libzmq3-dev and libcppzmq-dev")
endif()

message(STATUS "ZeroMQ library: ${ZMQ_LIB}")
message(STATUS "ZeroMQ include: ${ZMQ_INCLUDE}")

# SQLite3
find_package(SQLite3 REQUIRED)

# spdlog
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# Исходники
set(SOURCES
    src/main.cpp
    src/server.cpp
    src/message.cpp
    src/router.cpp
    src/zmq_session.cpp
    src/storage.cpp
    src/dispatcher.cpp
    src/offline_queue.cpp
    src/correlation_engine.cpp
    src/metrics.cpp
)

add_executable(broker ${SOURCES})

target_include_directories(broker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${ZMQ_INCLUDE}
)

target_link_libraries(broker
    ${Boost_LIBRARIES}
    ${ZMQ_LIB}
    SQLite::SQLite3
    spdlog::spdlog
)

install(TARGETS broker DESTINATION bin)