// main.cpp
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "server.hpp"
#include "config.hpp"

// ==================== Глобальные переменные для graceful shutdown ====================
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;
bool g_shutdown_requested = false;
broker::Server* g_server_ptr = nullptr;  // Указатель для доступа из signal handler

// ==================== Обработчик сигналов ====================
void SignalHandler(int signal) {
    const char* signal_name = (signal == SIGINT) ? "SIGINT" : 
                              (signal == SIGTERM) ? "SIGTERM" : "UNKNOWN";
    
    // Логируем напрямую (spdlog потокобезопасен)
    spdlog::info("Received signal {} ({}), initiating shutdown...", signal, signal_name);
    
    {
        std::lock_guard<std::mutex> lock(g_shutdown_mutex);
        if (g_shutdown_requested) {
            spdlog::warn("Shutdown already in progress");
            return;
        }
        g_shutdown_requested = true;
    }
    
    // Останавливаем сервер если он существует
    if (g_server_ptr) {
        g_server_ptr->Stop();
    }
    
    // Уведомляем главный поток
    g_shutdown_cv.notify_one();
}

void SetupSignalHandlers() {
    // Основные сигналы завершения
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    // Игнорируем SIGHUP (может использоваться для reload)
    std::signal(SIGHUP, SIG_IGN);
    
    // Игнорируем SIGPIPE (может возникать при разрыве сокета)
    std::signal(SIGPIPE, SIG_IGN);
    
    spdlog::debug("Signal handlers configured");
}

// ==================== Настройка логирования ====================
void SetupLogging(const std::string& log_level = "info") {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("broker.log", true);
    
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>("broker", sinks.begin(), sinks.end());
    
    if (log_level == "trace") logger->set_level(spdlog::level::trace);
    else if (log_level == "debug") logger->set_level(spdlog::level::debug);
    else if (log_level == "info") logger->set_level(spdlog::level::info);
    else if (log_level == "warn") logger->set_level(spdlog::level::warn);
    else if (log_level == "error") logger->set_level(spdlog::level::err);
    else logger->set_level(spdlog::level::info);
    
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    logger->flush_on(spdlog::level::err);  // Автоматический flush при ошибках
    
    spdlog::set_default_logger(logger);
}

// ==================== Парсинг аргументов ====================
void PrintHelp(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  --port PORT                ZeroMQ listen port (default: 5555)\n"
              << "  --db-path PATH             SQLite database path (default: ./broker.db)\n"
              << "  --threads N                Number of worker threads (default: CPU cores)\n"
              << "  --log-level LEVEL          Log level: trace, debug, info, warn, error (default: info)\n"
              << "  --session-timeout N        Session timeout in seconds (default: 60)\n"
              << "  --ack-timeout N            ACK timeout in seconds (default: 30)\n"
              << "  --help                     Show this help message\n";
}

broker::Config ParseArgs(int argc, char* argv[]) {
    broker::Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            PrintHelp(argv[0]);
            exit(0);
        } else if (arg == "--port" && i + 1 < argc) {
            config.Port = std::stoi(argv[++i]);
        } else if (arg == "--db-path" && i + 1 < argc) {
            config.DbPath = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            config.Threads = std::stoi(argv[++i]);
        } else if (arg == "--log-level" && i + 1 < argc) {
            config.LogLevel = argv[++i];
        } else if (arg == "--session-timeout" && i + 1 < argc) {
            config.SessionTimeout = std::stoi(argv[++i]);
        } else if (arg == "--ack-timeout" && i + 1 < argc) {
            config.AckTimeout = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintHelp(argv[0]);
            exit(1);
        }
    }
    
    if (config.Threads == 0) {
        config.Threads = std::thread::hardware_concurrency();
        if (config.Threads == 0) config.Threads = 1;
    }
    
    return config;
}

// ==================== Главная функция ====================
int main(int argc, char* argv[]) {
    try {
        // Парсим аргументы
        auto config = ParseArgs(argc, argv);
        
        // Настраиваем логирование
        SetupLogging(config.LogLevel);
        
        spdlog::info("=== Async Message Broker v1.0.0 ===");
        spdlog::info("Configuration:");
        spdlog::info("  Port: {}", config.Port);
        spdlog::info("  Database: {}", config.DbPath);
        spdlog::info("  Threads: {}", config.Threads);
        spdlog::info("  Log level: {}", config.LogLevel);
        spdlog::info("  Session timeout: {}s", config.SessionTimeout);
        spdlog::info("  ACK timeout: {}s", config.AckTimeout);
        spdlog::info("=====================================");
        
        // Настраиваем обработчики сигналов
        SetupSignalHandlers();
        
        // Создаем сервер
        broker::Server server(config);
        g_server_ptr = &server;
        
        // Запускаем сервер в отдельном потоке
        std::thread server_thread([&server]() {
            spdlog::debug("Server thread started");
            server.Run();
            spdlog::debug("Server thread finished");
        });
        
        // Главный поток ждет сигнала завершения
        {
            std::unique_lock<std::mutex> lock(g_shutdown_mutex);
            g_shutdown_cv.wait(lock, [] { return g_shutdown_requested; });
        }
        
        spdlog::info("Shutdown signal received, waiting for server to stop...");
        
        // Если сервер еще не остановлен (на случай, если сигнал пришел до создания сервера)
        if (server.IsRunning()) {
            server.Stop();
        }
        
        // Ждем завершения потока сервера
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        g_server_ptr = nullptr;
        
        spdlog::info("=== Broker shutdown complete ===");
        
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }
    
    return 0;
}