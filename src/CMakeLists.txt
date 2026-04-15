cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
include_directories(${ZMQ_INCLUDE_DIRS})
link_directories(${ZMQ_LIBRARY_DIRS})

find_package(spdlog CONFIG REQUIRED)

find_package(SQLite3 REQUIRED)

find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(broker
    main.cpp
    message.cpp
    message_handler.cpp
    router.cpp
    server.cpp
    session.cpp
    storage.cpp
)

target_link_libraries(broker
    ${ZMQ_LIBRARIES}
    spdlog::spdlog
    SQLite::SQLite3
    ${Boost_LIBRARIES}
    pthread
)

# ============================================================================
# Examples Clients
# ============================================================================

option(BUILD_EXAMPLES "Build example clients" ON)

if(BUILD_EXAMPLES)

    add_executable(test_client_full ../examples/cpp_client/test_client_full.cpp)       
    target_link_libraries(test_client_full ${ZMQ_LIBRARIES} pthread)
    target_include_directories(test_client_full PRIVATE ${ZMQ_INCLUDE_DIRS})

endif()

# ============================================================================

install(TARGETS broker DESTINATION bin)

message(STATUS "========================================")
message(STATUS "Broker executable configured")
message(STATUS "========================================")