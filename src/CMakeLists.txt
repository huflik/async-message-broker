# src/CMakeLists.txt (альтернативный вариант)
cmake_minimum_required(VERSION 3.10)

# Устанавливаем C++ стандарт
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# Подключаем родительскую директорию с заголовками
include_directories(${CMAKE_SOURCE_DIR}/include)

# ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
include_directories(${ZMQ_INCLUDE_DIRS})
link_directories(${ZMQ_LIBRARY_DIRS})

# spdlog 
find_package(spdlog CONFIG REQUIRED)

# SQLite3
find_package(SQLite3 REQUIRED)

# Boost.Asio
find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})

# Создаем исполняемый файл
add_executable(broker
    main.cpp
    message.cpp
    message_handler.cpp
    router.cpp
    server.cpp
    session.cpp
    storage.cpp
)

# Линковка библиотек
target_link_libraries(broker
    ${ZMQ_LIBRARIES}
    spdlog::spdlog
    SQLite::SQLite3
    ${Boost_LIBRARIES}
    pthread
)

install(TARGETS broker DESTINATION bin)

message(STATUS "========================================")
message(STATUS "Broker executable configured")
message(STATUS "========================================")