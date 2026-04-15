// server.cpp
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
    spdlog::info("Configuration: session_timeout={}s, ack_timeout={}s",
                 config_.SessionTimeout, config_.AckTimeout);
    
    storage_ = std::make_unique<Storage>(config_.DbPath);
    
    router_ = std::make_unique<Router>(
        *storage_,      // IStorage&
        *this,          // IMessageSender&
        *this           // IConfigProvider&
    );
    
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
    
    router_socket_.set(zmq::sockopt::router_handover, 1);
    
    router_socket_.set(zmq::sockopt::tcp_keepalive, 1);
    router_socket_.set(zmq::sockopt::tcp_keepalive_idle, 5);
    router_socket_.set(zmq::sockopt::tcp_keepalive_intvl, 2);
    router_socket_.set(zmq::sockopt::tcp_keepalive_cnt, 2);   
    
    spdlog::debug("TCP keepalive configured: idle=5s, interval=2s, count=2");
    
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
    if (!running_) {
        spdlog::debug("Server stopping, ignoring ZMQ event");
        return;
    }
    
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            spdlog::debug("ZMQ FD operation aborted");
            return;
        }
        spdlog::error("ZeroMQ FD error: {}", ec.message());
        
        // Перерегистрируемся на следующее событие
        if (running_) {
            zmq_fd_->async_wait(
                boost::asio::posix::stream_descriptor::wait_read,
                [this](const boost::system::error_code& ec) {
                    OnZmqEvent(ec);
                }
            );
        }
        return;
    }
    
    ProcessPendingSends();
    
    int events = router_socket_.get(zmq::sockopt::events);
    
    if (events & ZMQ_POLLIN) {
        spdlog::debug("ZMQ_POLLIN event received");
        
        while (running_) {
            zmq::message_t identity;
            auto result = router_socket_.recv(identity, zmq::recv_flags::dontwait);
            if (!result) {
                break;
            }
            
            zmq::message_t data;
            result = router_socket_.recv(data, zmq::recv_flags::dontwait);
            if (!result) {
                spdlog::error("Failed to receive data");
                break;
            }
            
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
    
    // Перерегистрируемся на следующее событие
    if (running_) {
        zmq_fd_->async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            [this](const boost::system::error_code& ec) {
                OnZmqEvent(ec);
            }
        );
    }
}

void Server::ProcessPendingSends() {
    std::queue<PendingSend> sends;
    {
        std::lock_guard<std::mutex> lock(pending_sends_mutex_);
        if (pending_sends_.empty()) {
            sending_in_progress_ = false;
            return;
        }
        sends.swap(pending_sends_);
        sending_in_progress_ = true;
    }
    
    while (!sends.empty()) {
        auto& send = sends.front();
        try {
            auto result = router_socket_.send(send.identity, zmq::send_flags::dontwait | zmq::send_flags::sndmore);
            if (!result) {
                spdlog::warn("Failed to send identity frame");
                if (send.callback) send.callback(false);
                sends.pop();
                continue;
            }
            
            zmq::message_t delimiter;
            result = router_socket_.send(delimiter, zmq::send_flags::dontwait | zmq::send_flags::sndmore);
            if (!result) {
                spdlog::warn("Failed to send delimiter frame");
                if (send.callback) send.callback(false);
                sends.pop();
                continue;
            }
            
            result = router_socket_.send(send.data, zmq::send_flags::dontwait);
            if (!result) {
                spdlog::warn("Failed to send data frame");
                if (send.callback) send.callback(false);
            } else {
                spdlog::trace("Message sent successfully");
                if (send.callback) send.callback(true);
            }
        } catch (const zmq::error_t& e) {
            spdlog::error("ZMQ error while sending: {}", e.what());
            if (send.callback) send.callback(false);
        } catch (const std::exception& e) {
            spdlog::error("Error sending message: {}", e.what());
            if (send.callback) send.callback(false);
        }
        sends.pop();
    }
    
    {
        std::lock_guard<std::mutex> lock(pending_sends_mutex_);
        sending_in_progress_ = false;
        if (!pending_sends_.empty()) {
            ScheduleSendProcessing();
        }
    }
}

void Server::ScheduleSendProcessing() {
    boost::asio::post(io_context_, [this]() {
        if (running_ && !sending_in_progress_) {
            ProcessPendingSends();
        }
    });
}

void Server::SendToClient(zmq::message_t identity, 
                          zmq::message_t data,
                          std::function<void(bool)> callback) {
    {
        std::lock_guard<std::mutex> lock(pending_sends_mutex_);
        pending_sends_.emplace(std::move(identity), std::move(data), std::move(callback));
    }
    
    ScheduleSendProcessing();
}

void Server::SetupCleanupTimer() {
    if (!running_) return;
    
    cleanup_timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
    cleanup_timer_->expires_after(std::chrono::seconds(10));
    
    cleanup_timer_->async_wait([this](const boost::system::error_code& ec) {
        if (!ec && running_) {
            router_->CleanupInactiveSessions();
            SetupCleanupTimer();  // Рекурсивно перезапускаем таймер
        }
    });
}

void Server::SetupAckTimeoutTimer() {
    if (!running_ || config_.AckTimeout <= 0) return;
    
    ack_timeout_timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
    ack_timeout_timer_->expires_after(std::chrono::seconds(config_.AckTimeout));
    
    ack_timeout_timer_->async_wait([this](const boost::system::error_code& ec) {
        if (!ec && running_) {
            router_->CheckExpiredAcks();
            SetupAckTimeoutTimer();  // Рекурсивно перезапускаем таймер
        }
    });
}

void Server::Run() {
    spdlog::info("Starting server...");
    
    SetupCleanupTimer();
    SetupAckTimeoutTimer();
    
    // Запускаем пул потоков
    for (int i = 0; i < config_.Threads; ++i) {
        threads_.emplace_back([this, i] {
            spdlog::debug("Worker thread #{} started", i);
            io_context_.run();
            spdlog::debug("Worker thread #{} stopped", i);
        });
    }
    
    spdlog::info("Server started with {} worker threads", config_.Threads);
    
    // Ждем завершения всех потоков
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Server run loop exited");
}

void Server::Stop() {
    if (!running_.exchange(false)) {
        return;  // Уже остановлен
    }
    
    spdlog::info("Stopping server...");
    
    // Очищаем очередь отправки
    {
        std::lock_guard<std::mutex> lock(pending_sends_mutex_);
        while (!pending_sends_.empty()) {
            auto& send = pending_sends_.front();
            if (send.callback) {
                send.callback(false);  // Уведомляем об ошибке
            }
            pending_sends_.pop();
        }
    }
    
    // Отменяем все таймеры
    if (cleanup_timer_) {
        boost::system::error_code ec;
        cleanup_timer_->cancel(ec);
    }
    
    if (ack_timeout_timer_) {
        boost::system::error_code ec;
        ack_timeout_timer_->cancel(ec);
    }
    
    // Закрываем ZMQ сокет
    try {
        router_socket_.close();
    } catch (const std::exception& e) {
        spdlog::error("Error closing ZMQ socket: {}", e.what());
    }
    
    // Сбрасываем work_guard, чтобы io_context::run() мог завершиться
    work_guard_.reset();
    
    // Останавливаем io_context
    io_context_.stop();
    
    spdlog::info("Server stop signal sent, waiting for threads...");
}


} // namespace broker