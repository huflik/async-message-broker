cmake_minimum_required(VERSION 3.15)

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

find_package(prometheus-cpp CONFIG QUIET)

if(NOT prometheus-cpp_FOUND)
    message(STATUS "prometheus-cpp not found via CONFIG, trying manual search...")
    
    find_path(PROMETHEUS_INCLUDE_DIR
        NAMES prometheus/counter.h
        PATHS /usr/local/include /usr/include
    )
    
    find_library(PROMETHEUS_CORE_LIB
        NAMES prometheus-cpp-core
        PATHS /usr/local/lib /usr/local/lib64 /usr/lib /usr/lib64
    )
    
    find_library(PROMETHEUS_PULL_LIB
        NAMES prometheus-cpp-pull
        PATHS /usr/local/lib /usr/local/lib64 /usr/lib /usr/lib64
    )
    
    if(PROMETHEUS_INCLUDE_DIR AND PROMETHEUS_CORE_LIB AND PROMETHEUS_PULL_LIB)
        message(STATUS "Found prometheus-cpp manually:")
        message(STATUS "  Include: ${PROMETHEUS_INCLUDE_DIR}")
        message(STATUS "  Core lib: ${PROMETHEUS_CORE_LIB}")
        message(STATUS "  Pull lib: ${PROMETHEUS_PULL_LIB}")
        
        set(prometheus-cpp_FOUND TRUE)
        
        # Создаем imported targets
        add_library(prometheus-cpp::core UNKNOWN IMPORTED)
        set_target_properties(prometheus-cpp::core PROPERTIES
            IMPORTED_LOCATION "${PROMETHEUS_CORE_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROMETHEUS_INCLUDE_DIR}"
        )
        
        add_library(prometheus-cpp::pull UNKNOWN IMPORTED)
        set_target_properties(prometheus-cpp::pull PROPERTIES
            IMPORTED_LOCATION "${PROMETHEUS_PULL_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROMETHEUS_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "prometheus-cpp::core"
        )
    endif()
endif()

# Проверяем, найдена ли библиотека
if(prometheus-cpp_FOUND)
    message(STATUS "Building with Prometheus metrics support")
    add_definitions(-DBROKER_ENABLE_METRICS)
    set(METRICS_LIBS prometheus-cpp::pull prometheus-cpp::core)
    set(METRICS_SOURCES metrics.cpp)
else()
    message(WARNING "prometheus-cpp not found, building without metrics support")
    add_definitions(-DBROKER_NO_METRICS)
    set(METRICS_LIBS "")
    set(METRICS_SOURCES metrics_stub.cpp)  # Используем заглушку
endif()

# Добавление исполняемого файла
add_executable(broker
    main.cpp
    message.cpp
    message_handler.cpp
    router.cpp
    server.cpp
    session.cpp
    storage.cpp
    ${METRICS_SOURCES}
)

# Линковка - ВАЖНО: порядок библиотек имеет значение!
target_link_libraries(broker
    ${METRICS_LIBS}           # Сначала prometheus-cpp
    ${ZMQ_LIBRARIES}
    spdlog::spdlog
    SQLite::SQLite3
    ${Boost_LIBRARIES}
    pthread
    # Дополнительные системные библиотеки, которые могут потребоваться
    dl
    rt
)

# Добавляем пути к библиотекам
target_link_directories(broker PRIVATE /usr/local/lib /usr/local/lib64)

# ============================================================================
# Examples Clients
# ============================================================================

option(BUILD_EXAMPLES "Build example clients" ON)

if(BUILD_EXAMPLES)

    add_executable(test_client_full ../examples/cpp_client/test_client_full.cpp)       
    target_link_libraries(test_client_full ${ZMQ_LIBRARIES} pthread)
    target_include_directories(test_client_full PRIVATE ${ZMQ_INCLUDE_DIRS})

    add_executable(universal_client ../examples/universal_client.cpp)
    target_link_libraries(universal_client ${ZMQ_LIBRARIES} pthread)
    target_include_directories(universal_client PRIVATE ${ZMQ_INCLUDE_DIRS})

endif()

# ============================================================================

install(TARGETS broker DESTINATION bin)

message(STATUS "========================================")
message(STATUS "Broker executable configured")
message(STATUS "========================================")