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

    metrics_manager_ = std::make_shared<MetricsManager>();
    
#ifdef BROKER_ENABLE_METRICS
    if (config_.EnableMetrics) {
        try {
            metrics_manager_->InitExposer(config_.MetricsBindAddress);
            metrics_manager_->StartUpdater(std::chrono::seconds(config_.MetricsUpdateInterval));
            spdlog::info("Metrics enabled on {}", config_.MetricsBindAddress);
        } catch (const std::exception& e) {
            spdlog::error("Failed to initialize metrics: {}", e.what());
        }
    }
#endif
    
    storage_ = std::make_unique<Storage>(config_.DbPath);
    
    router_ = std::make_unique<Router>(
        *storage_,
        *this,
        *this,
        metrics_manager_
    );
    
    SetupZmqSocket();
    SetupAsioIntegration();
}

Server::~Server() {
    Stop();
    if (metrics_manager_) {
        metrics_manager_->StopUpdater();
    }
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
    
    UpdateQueueMetrics();
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

                ScopedMetricsTimer timer(metrics_manager_);
                
                if (metrics_manager_) {
                    metrics_manager_->IncrementMessagesReceived();
                    metrics_manager_->ObservePayloadSize(data.size());
                }

                std::vector<uint8_t> msg_data(
                    static_cast<uint8_t*>(data.data()),
                    static_cast<uint8_t*>(data.data()) + data.size()
                );
                
                auto msg = Message::Deserialize(msg_data);
                spdlog::debug("Deserialized: {}", msg.ToString());
                
                router_->RouteMessage(msg, identity);
                
            } catch (const std::exception& e) {
                spdlog::error("Error processing message: {}", e.what());
                if (metrics_manager_) {
                    metrics_manager_->IncrementMessagesFailed();
                }
            }
        }
    }
    
    if (events & ZMQ_POLLOUT) {
        spdlog::trace("ZMQ_POLLOUT event (socket ready for write)");
    }
    
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
        bool success = false;
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
                success = true;
                if (send.callback) send.callback(true);
            }
        } catch (const zmq::error_t& e) {
            spdlog::error("ZMQ error while sending: {}", e.what());
            if (send.callback) send.callback(false);
        } catch (const std::exception& e) {
            spdlog::error("Error sending message: {}", e.what());
            if (send.callback) send.callback(false);
        }


        if (metrics_manager_) {
            if (success) {
                metrics_manager_->IncrementMessagesSent();
            } else {
                metrics_manager_->IncrementMessagesFailed();
            }
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

    UpdateQueueMetrics();
}

void Server::UpdateQueueMetrics() {
    if (metrics_manager_) {
        std::lock_guard<std::mutex> lock(pending_sends_mutex_);
        metrics_manager_->SetPendingSendQueueSize(pending_sends_.size());
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
            SetupCleanupTimer(); 
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
            SetupAckTimeoutTimer(); 
        }
    });
}

void Server::Run() {
    spdlog::info("Starting server...");
    
    SetupCleanupTimer();
    SetupAckTimeoutTimer();
    
    for (int i = 0; i < config_.Threads; ++i) {
        threads_.emplace_back([this, i] {
            spdlog::debug("Worker thread #{} started", i);
            io_context_.run();
            spdlog::debug("Worker thread #{} stopped", i);
        });
    }
    
    spdlog::info("Server started with {} worker threads", config_.Threads);
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Server run loop exited");
}

void Server::Stop() {
    if (!running_.exchange(false)) {
        return;  
    }
    
    spdlog::info("Stopping server...");
    
    {
        std::lock_guard<std::mutex> lock(pending_sends_mutex_);
        while (!pending_sends_.empty()) {
            auto& send = pending_sends_.front();
            if (send.callback) {
                send.callback(false);
            }
            pending_sends_.pop();
        }
    }
    
    if (cleanup_timer_) {
        boost::system::error_code ec;
        cleanup_timer_->cancel(ec);
    }
    
    if (ack_timeout_timer_) {
        boost::system::error_code ec;
        ack_timeout_timer_->cancel(ec);
    }
    
    try {
        router_socket_.close();
    } catch (const std::exception& e) {
        spdlog::error("Error closing ZMQ socket: {}", e.what());
    }

    work_guard_.reset();
    
    io_context_.stop();
    
    spdlog::info("Server stop signal sent, waiting for threads...");
}


} 