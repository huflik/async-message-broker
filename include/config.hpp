#pragma once

#include <string>
#include <iostream>
#include <thread>

namespace broker {

class HelpRequested : public std::exception {
public:
    const char* what() const noexcept override { return "Help requested"; }
};

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& msg) : std::runtime_error(msg) {}
};    

struct Config {
    int Port = 5555;
    std::string DbPath = "./broker.db";
    int Threads = 0;
    std::string LogLevel = "info";
    
    // Таймауты (в секундах)
    int SessionTimeout = 60;      // Таймаут неактивной сессии (для fallback)
    int AckTimeout = 30;           // Таймаут ожидания ACK
    
    Config() noexcept = default;
    ~Config() noexcept = default;
    Config(const Config&) noexcept = default;
    Config(Config&&) noexcept = default;
    Config& operator=(const Config&) noexcept = default;
    Config& operator=(Config&&) noexcept = default;


    static void PrintHelp(const char* program_name) {
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

    static Config ParseArgs(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            PrintHelp(argv[0]);
            throw HelpRequested();
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
            throw ConfigError("Unknown option: " + arg);
        }
    }
    
    if (config.Threads == 0) {
        config.Threads = std::thread::hardware_concurrency();
        if (config.Threads == 0) config.Threads = 1;
    }
    
    return config;
}
};

} // namespace broker