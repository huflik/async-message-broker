#include "server.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace broker {

Server::Server(const Config& config)
    : config_(config)
    , zmq_context_(1)
    , router_socket_(zmq_context_, zmq::socket_type::router)
{
    spdlog::info("Initializing server...");
    
    storage_ = std::make_unique<Storage>(config_.DbPath);
    router_ = std::make_unique<Router>(*storage_, router_socket_);
    
    SetupZmqSocket();
}

Server::~Server() {
    Stop();
}

void Server::SetupZmqSocket() {
    std::string endpoint = "tcp://*:" + std::to_string(config_.Port);
    router_socket_.bind(endpoint);
    spdlog::info("ZeroMQ ROUTER socket bound to {}", endpoint);
    
    // Устанавливаем неблокирующий режим
    router_socket_.set(zmq::sockopt::rcvtimeo, 0);
    router_socket_.set(zmq::sockopt::sndtimeo, 0);
}

/**
 * Поток для опроса ZeroMQ сокета
 * 
 * Работает в отдельном потоке:
 * - Опрашивает router_socket_ на наличие входящих сообщений
 * - При получении сообщения читает два фрейма: identity и данные
 * - Десериализует сообщение и передаёт в Router
 */
void Server::ZmqPollThread() {
    spdlog::info("ZeroMQ poll thread started");
    
    int poll_count = 0;
    
    while (running_) {
        poll_count++;
        // Каждые 1000 итераций выводим сообщение, что поток жив (для отладки)
        if (poll_count % 1000 == 0) {
            spdlog::debug("ZmqPollThread alive ({} iterations), waiting for messages...", poll_count);
        }
        
        zmq::pollitem_t items[] = {
            {router_socket_, 0, ZMQ_POLLIN, 0}
        };
        
        try {
            // Опрос с таймаутом 10 мс
            zmq::poll(items, 1, std::chrono::milliseconds(10));
            
            if (items[0].revents & ZMQ_POLLIN) {
                spdlog::debug("ZMQ_POLLIN event received");
                
                // Читаем все доступные сообщения
                while (running_) {
                    // ========== 1. Получение identity отправителя ==========
                    zmq::message_t identity;
                    auto result = router_socket_.recv(identity, zmq::recv_flags::dontwait);
                    if (!result) {
                        spdlog::debug("No more messages in batch");
                        break;
                    }
                    
                    // Выводим identity для отладки
                    std::string identity_str(
                        reinterpret_cast<const char*>(identity.data()),
                        identity.size()
                    );
                    spdlog::debug("Received identity: {} ({} bytes)", identity_str, identity.size());
                    
                    // ========== 2. Получение данных сообщения ==========
                    // ВНИМАНИЕ: НЕТ разделителя! Второй фрейм — это сразу данные.
                    // DEALER-клиент отправляет два фрейма: [identity] [данные]
                    zmq::message_t data;
                    result = router_socket_.recv(data, zmq::recv_flags::dontwait);
                    if (!result) {
                        spdlog::error("Failed to receive data after identity");
                        break;
                    }
                    
                    spdlog::debug("Received data, size: {}", data.size());
                    
                    // ========== 3. Десериализация ==========
                    try {
                        std::vector<uint8_t> msg_data(
                            static_cast<uint8_t*>(data.data()),
                            static_cast<uint8_t*>(data.data()) + data.size()
                        );
                        
                        // Выводим hex-дамп для отладки (первые 32 байта)
                        std::string hex;
                        for (size_t i = 0; i < std::min(msg_data.size(), size_t(32)); i++) {
                            char buf[4];
                            snprintf(buf, sizeof(buf), "%02x ", msg_data[i]);
                            hex += buf;
                        }
                        spdlog::debug("Data hex: {}", hex);
                        
                        auto msg = Message::Deserialize(msg_data);
                        spdlog::debug("Deserialized: {}", msg.ToString());
                        
                        // ========== 4. Передача в Router ==========
                        router_->RouteMessage(msg, identity);
                        
                    } catch (const std::exception& e) {
                        spdlog::error("Error processing message: {}", e.what());
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("ZeroMQ poll error: {}", e.what());
        }
    }
    
    spdlog::info("ZeroMQ poll thread stopped");
}

/**
 * Поток для Boost.Asio (для будущих расширений)
 * 
 * Сейчас используется только для таймеров и возможного HTTP-сервера метрик
 */
void Server::AsioThread() {
    spdlog::debug("Asio thread started");
    
    while (running_) {
        try {
            // run_for обрабатывает события в течение 100 мс
            io_context_.run_for(std::chrono::milliseconds(100));
        } catch (const std::exception& e) {
            spdlog::error("Asio error: {}", e.what());
        }
    }
    
    spdlog::debug("Asio thread stopped");
}

void Server::Run() {
    spdlog::info("Starting server...");
    
    // Запуск ZeroMQ poll потока
    zmq_thread_ = std::thread(&Server::ZmqPollThread, this);
    spdlog::debug("ZmqPollThread created");
    
    // Запуск Asio потоков (для будущих расширений)
    for (int i = 0; i < config_.Threads; ++i) {
        threads_.emplace_back(&Server::AsioThread, this);
    }
    
    spdlog::info("Server started with {} threads", config_.Threads);
    
    // Главный поток ждёт сигнала остановки
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Server::Stop() {
    if (!running_) return;
    
    spdlog::info("Stopping server...");
    running_ = false;
    
    // Остановка io_context
    io_context_.stop();
    
    // Ожидание завершения ZeroMQ потока
    if (zmq_thread_.joinable()) {
        spdlog::debug("Waiting for ZmqPollThread to finish...");
        zmq_thread_.join();
    }
    
    // Ожидание завершения Asio потоков
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Server stopped");
}

} // namespace broker