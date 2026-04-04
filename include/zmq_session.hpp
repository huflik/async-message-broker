#pragma once

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <zmq.hpp>

#include "session.hpp"
#include "message.hpp"
#include "storage.hpp"
#include <spdlog/spdlog.h>

namespace broker {

class ZmqSession : public Session {
public:
    explicit ZmqSession(zmq::message_t identity, zmq::socket_t& router_socket, Storage* storage = nullptr);
    ~ZmqSession() override = default;
    
    bool SendMessage(const Message& msg) override;
    std::string GetName() const override { return name_; }
    bool IsOnline() const override { return is_online_; }
    
    const zmq::message_t& GetIdentity() const override { return identity_; }
    
    void SetName(const std::string& name) override { name_ = name; }
    void EnqueueMessage(const Message& msg) override;
    void FlushQueue() override;
    
    void MarkOffline() { 
        is_online_ = false; 
        spdlog::debug("Client {} marked as offline", name_);
    }
    void MarkOnline() { 
        is_online_ = true; 
        spdlog::debug("Client {} marked as online", name_);
    }
    
    void UpdateLastActivity() {
        last_activity_ = std::chrono::steady_clock::now();
    }
    
    /**
     * Проверяет, не истёк ли таймаут активности
     * @param timeout_seconds количество секунд без активности
     * @return true если таймаут истёк
     */
    bool IsTimedOut(int timeout_seconds) const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_activity_);
        return elapsed.count() > timeout_seconds;
    }
    
    /**
     * Проверяет, действительно ли соединение ещё живо
     * @return true если соединение активно
     */
    bool CheckConnection();
    
    /**
     * Сохраняет все сообщения из очереди в БД
     */
    void PersistQueueToDatabase();

private:
    bool SendZmqMessage(const Message& msg);
    
    zmq::message_t identity_;
    zmq::socket_t& router_socket_;
    Storage* storage_ = nullptr;
    std::string name_;
    bool is_online_ = true;
    std::queue<Message> outgoing_queue_;
    std::mutex queue_mutex_;
    std::chrono::steady_clock::time_point last_activity_;
};

} // namespace broker