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
    //int HeartbeatInterval = 0;     // Интервал проверки heartbeat (0 = отключено)
};

} // namespace broker