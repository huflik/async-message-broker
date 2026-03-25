#include <iostream>
#include <csignal>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

// Глобальный флаг для graceful shutdown
std::atomic<bool> running{true};

void signal_handler(int signal) {
    spdlog::info("Received signal {}, shutting down...", signal);
    running = false;
}

void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

void setup_logging(const std::string& log_level = "info") {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("broker.log", true);
    
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>("broker", sinks.begin(), sinks.end());
    
    // Установка уровня логирования
    if (log_level == "trace") logger->set_level(spdlog::level::trace);
    else if (log_level == "debug") logger->set_level(spdlog::level::debug);
    else if (log_level == "info") logger->set_level(spdlog::level::info);
    else if (log_level == "warn") logger->set_level(spdlog::level::warn);
    else if (log_level == "error") logger->set_level(spdlog::level::err);
    else logger->set_level(spdlog::level::info);
    
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    
    spdlog::set_default_logger(logger);
}

void print_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  --port PORT         ZeroMQ listen port (default: 5555)\n"
              << "  --db-path PATH      SQLite database path (default: ./broker.db)\n"
              << "  --threads N         Number of worker threads (default: CPU cores)\n"
              << "  --log-level LEVEL   Log level: trace, debug, info, warn, error (default: info)\n"
              << "  --help              Show this help message\n";
}

struct Config {
    int port = 5555;
    std::string db_path = "./broker.db";
    int threads = 0;  // 0 = auto
    std::string log_level = "info";
};

Config parse_args(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_help(argv[0]);
            exit(0);
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = std::stoi(argv[++i]);
        } else if (arg == "--db-path" && i + 1 < argc) {
            config.db_path = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoi(argv[++i]);
        } else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_help(argv[0]);
            exit(1);
        }
    }
    
    // Если threads = 0, используем количество ядер
    if (config.threads == 0) {
        config.threads = std::thread::hardware_concurrency();
        if (config.threads == 0) config.threads = 1;
    }
    
    return config;
}

int main(int argc, char* argv[]) {
    // Парсинг аргументов
    auto config = parse_args(argc, argv);
    
    // Настройка логирования
    setup_logging(config.log_level);
    
    spdlog::info("Starting Async Message Broker v1.0.0");
    spdlog::info("Configuration: port={}, db_path={}, threads={}, log_level={}",
                 config.port, config.db_path, config.threads, config.log_level);
    
    // Настройка обработчиков сигналов
    setup_signal_handlers();
    
    // TODO: Здесь будет инициализация и запуск брокера
    // Server server(config);
    // server.run();
    
    spdlog::info("Broker initialized, waiting for connections...");
    
    // Основной цикл (временно)
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    spdlog::info("Broker shutting down...");
    
    return 0;
}