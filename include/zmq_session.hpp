#pragma once

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <functional>
#include <future>
#include <zmq.hpp>

#include "session.hpp"
#include "message.hpp"
#include <spdlog/spdlog.h>

namespace broker {

class Server;

class ZmqSession : public Session {
public:
    explicit ZmqSession(zmq::message_t identity, Server& server);
    ~ZmqSession() override;
    
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
    
    void UpdateLastReceive() override {
        last_receive_ = std::chrono::steady_clock::now();
    }
    
    void UpdateHeartbeat() {
        last_heartbeat_ = std::chrono::steady_clock::now();
    }
    
    std::chrono::steady_clock::time_point GetLastReceiveTime() const { return last_receive_; }
    
    bool IsTimedOut(int timeout_seconds) const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_activity_);
        return elapsed.count() > timeout_seconds;
    }
    
    bool CheckConnection();
    void PersistQueueToDatabase();
    
    void SetPersistCallback(std::function<void(const std::string&, const Message&)> callback) {
        persist_callback_ = std::move(callback);
    }
    
    void SetSendCallback(std::function<void(zmq::message_t, zmq::message_t, 
                                            std::function<void(bool)>)> callback) {
        send_callback_ = std::move(callback);
    }
    
    size_t GetQueueSize() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return outgoing_queue_.size();
    }

private:
    bool SendZmqMessage(const Message& msg);
    
    zmq::message_t identity_;
    Server& server_;
    std::string name_;
    bool is_online_ = true;
    std::queue<Message> outgoing_queue_;
    mutable std::mutex queue_mutex_;
    std::chrono::steady_clock::time_point last_activity_;
    std::chrono::steady_clock::time_point last_receive_;
    std::chrono::steady_clock::time_point last_heartbeat_;
    
    std::function<void(zmq::message_t, zmq::message_t, std::function<void(bool)>)> send_callback_;
    std::function<void(const std::string&, const Message&)> persist_callback_;
};

} // namespace broker