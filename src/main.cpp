#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "server.hpp"
#include "config.hpp"

std::atomic<bool> running{true};

void SignalHandler(int signal) {
    spdlog::info("Received signal {}, shutting down...", signal);
    running = false;
}

void SetupSignalHandlers() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
}

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
    
    spdlog::set_default_logger(logger);
}

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

int main(int argc, char* argv[]) {
    auto cmd_config = ParseArgs(argc, argv);
    SetupLogging(cmd_config.LogLevel);
    
    spdlog::info("Starting Async Message Broker v1.0.0");
    spdlog::info("Configuration: port={}, db_path={}, threads={}, log_level={}, session_timeout={}s, ack_timeout={}s, heartbeat_interval={}s",
                 cmd_config.Port, cmd_config.DbPath, cmd_config.Threads, cmd_config.LogLevel,
                 cmd_config.SessionTimeout, cmd_config.AckTimeout);
    
    SetupSignalHandlers();

    
    broker::Server server(cmd_config);
    
    std::thread server_thread([&server]() {
        server.Run();
    });
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    server.Stop();
    server_thread.join();
    
    spdlog::info("Broker shutdown complete");
    return 0;
}