cmake_minimum_required(VERSION 3.10)

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(SPDLOG REQUIRED spdlog)
find_package(SQLite3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

find_package(GTest REQUIRED)

add_executable(broker_tests
    test_message.cpp
    test_storage.cpp
    test_session.cpp
    test_router.cpp
    test_message_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/message.cpp
    ${CMAKE_SOURCE_DIR}/src/message_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/router.cpp
    ${CMAKE_SOURCE_DIR}/src/session.cpp
    ${CMAKE_SOURCE_DIR}/src/storage.cpp
)

target_link_libraries(broker_tests
    gtest gtest_main gmock gmock_main
    ${ZMQ_LIBRARIES}
    ${SPDLOG_LIBRARIES}
    SQLite::SQLite3
    ${Boost_LIBRARIES}
    pthread
)

include(GoogleTest)
gtest_discover_tests(broker_tests)