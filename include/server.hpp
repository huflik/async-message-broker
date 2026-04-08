#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <zmq.hpp>
#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>

#include "router.hpp"
#include "storage.hpp"

namespace broker {

struct Config {
    int Port = 5555;
    std::string DbPath = "./broker.db";
    int Threads = 0;
    std::string LogLevel = "info";
    
    // Таймауты (в секундах)
    int SessionTimeout = 60;      // Таймаут неактивной сессии (для fallback)
    int AckTimeout = 30;           // Таймаут ожидания ACK
    int HeartbeatInterval = 0;     // Интервал проверки heartbeat (0 = отключено)
};

struct PendingSend {
    zmq::message_t identity;
    zmq::message_t data;
    std::function<void(bool)> callback;
    
    PendingSend() = default;
    PendingSend(zmq::message_t id, zmq::message_t d, std::function<void(bool)> cb)
        : identity(std::move(id)), data(std::move(d)), callback(std::move(cb)) {}
    
    PendingSend(const PendingSend&) = delete;
    PendingSend& operator=(const PendingSend&) = delete;
    PendingSend(PendingSend&&) = default;
    PendingSend& operator=(PendingSend&&) = default;
};

class Server {
public:
    explicit Server(const Config& config);
    ~Server();
    
    void Run();
    void Stop();
    
    void SendMessage(zmq::message_t identity, zmq::message_t data, 
                     std::function<void(bool)> callback = nullptr);
    
    const Config& GetConfig() const { return config_; }

private:
    void SetupZmqSocket();
    void SetupAsioIntegration();
    void OnZmqEvent(const boost::system::error_code& ec);
    void AsioThread();
    void SetupCleanupTimer();
    void SetupAckTimeoutTimer();
    void ProcessPendingSends();
    void ScheduleSendProcessing();

private:
    Config config_;
    std::atomic<bool> running_{true};
    
    zmq::context_t zmq_context_;
    zmq::socket_t router_socket_;
    
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_guard_;
    std::vector<std::thread> threads_;
    std::unique_ptr<boost::asio::posix::stream_descriptor> zmq_fd_;
    std::unique_ptr<boost::asio::steady_timer> cleanup_timer_;
    std::unique_ptr<boost::asio::steady_timer> ack_timeout_timer_;
    
    std::unique_ptr<Router> router_;
    std::unique_ptr<Storage> storage_;
    
    std::queue<PendingSend> pending_sends_;
    std::mutex pending_sends_mutex_;
    std::atomic<bool> sending_in_progress_{false};
};

} // namespace broker