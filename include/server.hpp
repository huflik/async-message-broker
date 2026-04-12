// server.hpp
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

#include "config.hpp"        // ДОБАВИТЬ
#include "interfaces.hpp"    // ДОБАВИТЬ
#include "router.hpp"
#include "storage.hpp"

namespace broker {

// Удаляем struct Config из этого файла (теперь он в config.hpp)

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

// ДОБАВИТЬ: Server теперь реализует интерфейсы
class Server : public IMessageSender, public IConfigProvider {
public:
    explicit Server(const Config& config);
    ~Server();
    
    void Run();
    void Stop();
    
    // Реализация IMessageSender
    void SendToClient(zmq::message_t identity, 
                      zmq::message_t data,
                      std::function<void(bool)> callback = nullptr) override;
    
    // Реализация IConfigProvider
    const Config& GetConfig() const override { return config_; }
    
    // Для обратной совместимости (можно удалить после рефакторинга)
    void SendMessage(zmq::message_t identity, zmq::message_t data, 
                     std::function<void(bool)> callback = nullptr) {
        SendToClient(std::move(identity), std::move(data), std::move(callback));
    }

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