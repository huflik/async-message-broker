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

# ============================================================================
# Примеры клиентов
# ============================================================================

option(BUILD_EXAMPLES "Build example clients" ON)

if(BUILD_EXAMPLES)
    # Проверяем наличие клиента
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../examples/test_client_full.cpp")
        add_executable(test_client_full ../examples/test_client_full.cpp)
        
        # Исправлено: используем ${ZMQ_LIBRARIES} вместо ${ZMQ_LIB}
        target_link_libraries(test_client_full 
            ${ZMQ_LIBRARIES} 
            pthread
        )
        target_include_directories(test_client_full PRIVATE 
            ${ZMQ_INCLUDE_DIRS}
        )
        
        message(STATUS "test_client_full configured")
    else()
        message(WARNING "test_client_full.cpp not found")
    endif()
endif()

# ============================================================================

install(TARGETS broker DESTINATION bin)

message(STATUS "========================================")
message(STATUS "Broker executable configured")
message(STATUS "========================================")