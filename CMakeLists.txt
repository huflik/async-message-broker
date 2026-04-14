# Корневой CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(async-message-broker VERSION 1.0.0)

# Подключаем директорию с заголовками
include_directories(include)

# Добавляем поддиректорию src
add_subdirectory(src)

# Тесты
option(WITH_GTEST "Build tests" ON)

if(WITH_GTEST)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "========================================")
message(STATUS "Async Message Broker v${PROJECT_VERSION}")
message(STATUS "Build tests: ${WITH_GTEST}")
message(STATUS "========================================")