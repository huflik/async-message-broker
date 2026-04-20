cmake_minimum_required(VERSION 3.15)
project(async-message-broker VERSION 1.0.0)

include_directories(include)

add_subdirectory(src)

option(WITH_GTEST "Build tests" ON)

if(WITH_GTEST)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "========================================")
message(STATUS "Async Message Broker v${PROJECT_VERSION}")
message(STATUS "Build tests: ${WITH_GTEST}")
message(STATUS "========================================")