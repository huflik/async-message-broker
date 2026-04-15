#pragma once

#include <string>

namespace broker {

struct Config {
    int Port = 5555;
    std::string DbPath = "./broker.db";
    int Threads = 0;
    std::string LogLevel = "info";
    
    // Таймауты (в секундах)
    int SessionTimeout = 60;      // Таймаут неактивной сессии (для fallback)
    int AckTimeout = 30;           // Таймаут ожидания ACK
    
    Config() = default;
    ~Config() = default;
    Config(const Config&) = default;
    Config(Config&&) = default;
    Config& operator=(const Config&) = default;
    Config& operator=(Config&&) = default;
};

} // namespace broker