#include "server.hpp"
#include <spdlog/spdlog.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

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
    SetupAsioIntegration();
}

Server::~Server() {
    Stop();
}

void Server::SetupZmqSocket() {
    std::string endpoint = "tcp://*:" + std::to_string(config_.Port);
    router_socket_.bind(endpoint);
    spdlog::info("ZeroMQ ROUTER socket bound to {}", endpoint);
    
    // Неблокирующий режим
    router_socket_.set(zmq::sockopt::rcvtimeo, 0);
    router_socket_.set(zmq::sockopt::sndtimeo, 0);
}

void Server::SetupAsioIntegration() {
    // ========== 1. Получение файлового дескриптора ==========
    int fd = router_socket_.get(zmq::sockopt::fd);
    
    if (fd <= 0) {
        throw std::runtime_error("Failed to get ZeroMQ file descriptor");
    }
    
    spdlog::debug("ZeroMQ file descriptor: {}", fd);
    
    // ========== 2. Установка неблокирующего режима ==========
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("Failed to get file descriptor flags");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("Failed to set non-blocking mode");
    }
    
    // ========== 3. Создание work_guard ==========
    work_guard_ = std::make_unique<boost::asio::io_context::work>(io_context_);
    
    // ========== 4. Обёртка дескриптора в Asio ==========
    zmq_fd_ = std::make_unique<boost::asio::posix::stream_descriptor>(io_context_, fd);
    
    // ========== 5. Запуск асинхронного ожидания ==========
    zmq_fd_->async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [this](const boost::system::error_code& ec) {
            OnZmqEvent(ec);
        }
    );
    
    spdlog::info("ZeroMQ integrated with Boost.Asio via FD");
}

void Server::OnZmqEvent(const boost::system::error_code& ec) {
    if (!running_) return;
    
    if (ec) {
        spdlog::error("ZeroMQ FD error: {}", ec.message());
        // Перерегистрируем ожидание
        zmq_fd_->async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            [this](const boost::system::error_code& ec) {
                OnZmqEvent(ec);
            }
        );
        return;
    }
    
    // ========== 1. Проверка реальных событий ==========
    int events = router_socket_.get(zmq::sockopt::events);
    
    // ========== 2. Обработка входящих сообщений ==========
    if (events & ZMQ_POLLIN) {
        spdlog::debug("ZMQ_POLLIN event received");
        
        // Читаем все доступные сообщения
        while (running_) {
            zmq::message_t identity;
            auto result = router_socket_.recv(identity, zmq::recv_flags::dontwait);
            if (!result) {
                break;  // нет больше сообщений
            }
            
            // Выводим identity для отладки
            std::string identity_str(
                reinterpret_cast<const char*>(identity.data()),
                identity.size()
            );
            spdlog::debug("Received identity: {}", identity_str);
            
            // Второй фрейм — данные (нет разделителя от DEALER!)
            zmq::message_t data;
            result = router_socket_.recv(data, zmq::recv_flags::dontwait);
            if (!result) {
                spdlog::error("Failed to receive data");
                break;
            }
            
            spdlog::debug("Received data, size: {}", data.size());
            
            // Обработка сообщения
            try {
                std::vector<uint8_t> msg_data(
                    static_cast<uint8_t*>(data.data()),
                    static_cast<uint8_t*>(data.data()) + data.size()
                );
                
                auto msg = Message::Deserialize(msg_data);
                spdlog::debug("Deserialized: {}", msg.ToString());
                
                router_->RouteMessage(msg, identity);
                
            } catch (const std::exception& e) {
                spdlog::error("Error processing message: {}", e.what());
            }
        }
    }
    
    // ========== 3. Обработка исходящих сообщений (опционально) ==========
    if (events & ZMQ_POLLOUT) {
        spdlog::trace("ZMQ_POLLOUT event (socket ready for write)");
    }
    
    // ========== 4. Перерегистрация ожидания ==========
    zmq_fd_->async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [this](const boost::system::error_code& ec) {
            OnZmqEvent(ec);
        }
    );
}

void Server::HandleZmqMessage() {
    // Этот метод больше не нужен, всё обрабатывается в OnZmqEvent
    // Оставляем для совместимости, но не используем
}

void Server::AsioThread() {
    spdlog::debug("Asio thread started");
    
    while (running_) {
        try {
            io_context_.run();
            break;
        } catch (const std::exception& e) {
            spdlog::error("Asio error: {}", e.what());
        }
    }
    
    spdlog::debug("Asio thread stopped");
}

void Server::Run() {
    spdlog::info("Starting server...");
    
    // Запуск пула Asio потоков
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
    
    io_context_.stop();
    work_guard_.reset();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Server stopped");
}

} // namespace broker