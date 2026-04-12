cmake_minimum_required(VERSION 3.15)
project(AsyncMessageBroker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Поиск зависимостей
# ============================================================================

find_package(Boost 1.82 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

find_library(ZMQ_LIB NAMES zmq)
find_path(ZMQ_INCLUDE NAMES zmq.h)

if(NOT ZMQ_LIB OR NOT ZMQ_INCLUDE)
    message(FATAL_ERROR "ZeroMQ not found. Install libzmq3-dev")
endif()

find_path(CPPZMQ_INCLUDE NAMES zmq.hpp PATHS /usr/include /usr/local/include)

if(NOT CPPZMQ_INCLUDE)
    message(FATAL_ERROR "cppzmq not found. Install libcppzmq-dev")
endif()

find_package(SQLite3 REQUIRED)

# spdlog
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# ============================================================================
# Основная программа
# ============================================================================

set(SOURCES
    src/main.cpp
    src/server.cpp
    src/message.cpp
    src/router.cpp
    src/session.cpp
    src/storage.cpp
)

add_executable(broker ${SOURCES})

target_include_directories(broker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${ZMQ_INCLUDE}
    ${CPPZMQ_INCLUDE}
)

target_link_libraries(broker
    ${Boost_LIBRARIES}
    ${ZMQ_LIB}
    SQLite::SQLite3
    spdlog::spdlog
    Threads::Threads
)


# ============================================================================
# Примеры клиентов
# ============================================================================

option(BUILD_EXAMPLES "Build example clients" ON)

if(BUILD_EXAMPLES)
    add_executable(test_client_full examples/test_client_full.cpp)
    target_link_libraries(test_client_full ${ZMQ_LIB})
    target_include_directories(test_client_full PRIVATE ${ZMQ_INCLUDE} ${CPPZMQ_INCLUDE})

    add_executable(simple_client examples/simple_client.cpp)
    target_link_libraries(simple_client ${ZMQ_LIB})
    target_include_directories(simple_client PRIVATE ${ZMQ_INCLUDE} ${CPPZMQ_INCLUDE})

    add_executable(reconnect_client examples/reconnect_client.cpp)
    target_link_libraries(reconnect_client ${ZMQ_LIB})
    target_include_directories(reconnect_client PRIVATE ${ZMQ_INCLUDE} ${CPPZMQ_INCLUDE})
    
    add_executable(alice_client examples/cpp_client/client.cpp)
    target_link_libraries(alice_client ${ZMQ_LIB})
    target_include_directories(alice_client PRIVATE ${ZMQ_INCLUDE} ${CPPZMQ_INCLUDE})
    
    add_executable(bob_client examples/cpp_client/bob_client.cpp)
    target_link_libraries(bob_client ${ZMQ_LIB})
    target_include_directories(bob_client PRIVATE ${ZMQ_INCLUDE} ${CPPZMQ_INCLUDE})
endif()

# ============================================================================
# Установка
# ============================================================================

install(TARGETS broker DESTINATION bin)