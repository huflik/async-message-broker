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

std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;
bool g_shutdown_requested = false;
std::atomic<broker::Server*> g_server_ptr{nullptr};

void SignalHandler(int signal) {
    const char* signal_name = (signal == SIGINT) ? "SIGINT" : 
                              (signal == SIGTERM) ? "SIGTERM" : "UNKNOWN";
    
    spdlog::info("Received signal {} ({}), initiating shutdown...", signal, signal_name);
    
    {
        std::lock_guard<std::mutex> lock(g_shutdown_mutex);
        if (g_shutdown_requested) {
            spdlog::warn("Shutdown already in progress");
            return;
        }
        g_shutdown_requested = true;
    }
    
    auto* server = g_server_ptr.load(std::memory_order_acquire);
    if (server) {
        server->Stop();
    }
    
    g_shutdown_cv.notify_one();
}

void SetupSignalHandlers() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);  
    std::signal(SIGHUP, SIG_IGN);   
    std::signal(SIGPIPE, SIG_IGN);
    
    spdlog::debug("Signal handlers configured");
}

void SetupLogging(const std::string& log_level = "info") {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("broker.log", true);
    
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>("broker", sinks.begin(), sinks.end());
    
    if (log_level == "trace") {
        logger->set_level(spdlog::level::trace);
    } else if (log_level == "debug") {
        logger->set_level(spdlog::level::debug);
    } else if (log_level == "info") {
        logger->set_level(spdlog::level::info);
    } else if (log_level == "warn") {
        logger->set_level(spdlog::level::warn);
    } else if (log_level == "error") {
        logger->set_level(spdlog::level::err);
    } else {
        logger->set_level(spdlog::level::info);
    }
    
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    logger->flush_on(spdlog::level::err); 
    
    spdlog::set_default_logger(logger);
}

void PrintBanner(const broker::Config& config) {
    spdlog::info("=== Async Message Broker v1.0.0 ===");
    spdlog::info("Configuration:");
    spdlog::info("  Port: {}", config.Port);
    spdlog::info("  Database: {}", config.DbPath);
    spdlog::info("  Threads: {}", config.Threads);
    spdlog::info("  Log level: {}", config.LogLevel);
    spdlog::info("  Session timeout: {}s", config.SessionTimeout);
    spdlog::info("  ACK timeout: {}s", config.AckTimeout);
    spdlog::info("=====================================");
}

int main(int argc, char* argv[]) {
    try {
        auto config = broker::Config::ParseArgs(argc, argv);
        
        SetupLogging(config.LogLevel);
        
        PrintBanner(config);
        
        SetupSignalHandlers();
        
        broker::Server server(config);
        g_server_ptr.store(&server, std::memory_order_release);
        
        std::thread server_thread([&server]() {
            spdlog::debug("Server thread started");
            server.Run();
            spdlog::debug("Server thread finished");
        });
        
        {
            std::unique_lock<std::mutex> lock(g_shutdown_mutex);
            g_shutdown_cv.wait(lock, [] { return g_shutdown_requested; });
        }
        
        spdlog::info("Shutdown signal received, waiting for server to stop...");
        
        if (server.IsRunning()) {
            server.Stop();
        }
        
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        g_server_ptr.store(nullptr, std::memory_order_release);
        
        spdlog::info("=== Broker shutdown complete ===");
        
    } catch (const broker::HelpRequested&) {
        return 0;
    } catch (const broker::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        broker::Config::PrintHelp(argv[0]);
        return 1;
    } catch (const std::exception& e) {
        if (spdlog::default_logger()) {
            spdlog::critical("Fatal error: {}", e.what());
        } else {
            std::cerr << "Fatal error: " << e.what() << std::endl;
        }
        return 1;
    } catch (...) {
        if (spdlog::default_logger()) {
            spdlog::critical("Unknown fatal error");
        } else {
            std::cerr << "Unknown fatal error" << std::endl;
        }
        return 1;
    }
    
    return 0;
}