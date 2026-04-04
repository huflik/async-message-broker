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
    
    // ========== ZMQ_ROUTER_HANDOVER ==========
    // Позволяет новому клиенту с тем же identity перехватить соединение
    // Это решает проблему с переподключением после корректного закрытия
    router_socket_.set(zmq::sockopt::router_handover, 1);
    spdlog::debug("ZMQ_ROUTER_HANDOVER enabled - new connections with same identity will replace old ones");
    
    // ========== TCP keepalive для быстрого обнаружения разрыва соединения ==========
    // idle=5s   - начинаем проверку через 5 секунд бездействия
    // intvl=2s  - проверяем каждые 2 секунды
    // cnt=2     - 2 неудачные попытки считаем разрывом
    router_socket_.set(zmq::sockopt::tcp_keepalive, 1);
    router_socket_.set(zmq::sockopt::tcp_keepalive_idle, 5);   // 5 секунд бездействия
    router_socket_.set(zmq::sockopt::tcp_keepalive_intvl, 2);   // проверка каждые 2 секунды
    router_socket_.set(zmq::sockopt::tcp_keepalive_cnt, 2);     // 2 неудачные попытки
    
    spdlog::debug("TCP keepalive configured: idle=5s, interval=2s, count=2");
    
    // Неблокирующий режим
    router_socket_.set(zmq::sockopt::rcvtimeo, 0);
    router_socket_.set(zmq::sockopt::sndtimeo, 0);
}

void Server::SetupAsioIntegration() {
    int fd = router_socket_.get(zmq::sockopt::fd);
    
    if (fd <= 0) {
        throw std::runtime_error("Failed to get ZeroMQ file descriptor");
    }
    
    spdlog::debug("ZeroMQ file descriptor: {}", fd);
    
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("Failed to get file descriptor flags");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("Failed to set non-blocking mode");
    }
    
    work_guard_ = std::make_unique<boost::asio::io_context::work>(io_context_);
    
    zmq_fd_ = std::make_unique<boost::asio::posix::stream_descriptor>(io_context_, fd);
    
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
        zmq_fd_->async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            [this](const boost::system::error_code& ec) {
                OnZmqEvent(ec);
            }
        );
        return;
    }
    
    int events = router_socket_.get(zmq::sockopt::events);
    
    if (events & ZMQ_POLLIN) {
        spdlog::debug("ZMQ_POLLIN event received");
        
        while (running_) {
            zmq::message_t identity;
            auto result = router_socket_.recv(identity, zmq::recv_flags::dontwait);
            if (!result) {
                break;
            }
            
            std::string identity_str(
                reinterpret_cast<const char*>(identity.data()),
                identity.size()
            );
            spdlog::debug("Received identity: {}", identity_str);
            
            zmq::message_t data;
            result = router_socket_.recv(data, zmq::recv_flags::dontwait);
            if (!result) {
                spdlog::error("Failed to receive data");
                break;
            }
            
            spdlog::debug("Received data, size: {}", data.size());
            
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
    
    if (events & ZMQ_POLLOUT) {
        spdlog::trace("ZMQ_POLLOUT event (socket ready for write)");
    }
    
    zmq_fd_->async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [this](const boost::system::error_code& ec) {
            OnZmqEvent(ec);
        }
    );
}

void Server::SetupCleanupTimer() {
    cleanup_timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
    // Уменьшаем интервал очистки до 5 секунд для быстрого обнаружения
    cleanup_timer_->expires_after(std::chrono::seconds(5));
    
    cleanup_timer_->async_wait([this](const boost::system::error_code& ec) {
        if (!ec && running_) {
            router_->CleanupInactiveSessions();
            SetupCleanupTimer();
        }
    });
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
    
    SetupCleanupTimer();
    
    for (int i = 0; i < config_.Threads; ++i) {
        threads_.emplace_back(&Server::AsioThread, this);
    }
    
    spdlog::info("Server started with {} threads", config_.Threads);
    
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