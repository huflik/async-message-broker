#pragma once

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <functional>
#include <atomic>
#include <zmq.hpp>

#include "message.hpp"
#include "interfaces.hpp" 
#include <spdlog/spdlog.h>

namespace broker {

class Session {
public:
    explicit Session(zmq::message_t identity, 
                     IMessageSender& message_sender,
                     const Config& config);
    
    ~Session();
    
    bool SendMessage(Message msg);
    
    std::string GetName() const noexcept { return name_; }
    bool IsOnline() const noexcept { return is_online_.load(std::memory_order_acquire); }
    
    const zmq::message_t& GetIdentity() const noexcept { return identity_; }
    
    void SetName(const std::string& name) { name_ = name; }
    void FlushQueue();
    
    void MarkOffline() noexcept { 
        is_online_.store(false, std::memory_order_release);
        spdlog::debug("Client {} marked as offline", name_);
    }
    
    void MarkOnline() noexcept {
        is_online_.store(true, std::memory_order_release);
        spdlog::debug("Client {} marked as online", name_);
    }
    
    void UpdateLastReceive() noexcept {
        last_receive_ = std::chrono::steady_clock::now();
        spdlog::trace("Client {} last receive updated", name_);
    }
    
    void UpdateLastActivity() noexcept {
        last_activity_ = std::chrono::steady_clock::now();
        spdlog::trace("Client {} last activity updated", name_);
    }
    
    bool IsExpired(int timeout_seconds) const noexcept {
        if (!IsOnline()) return true;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_receive_);
        return elapsed.count() > timeout_seconds;
    }
    
    void PersistQueueToDatabase();
    
    void SetPersistCallback(std::function<void(const std::string&, const Message&)> callback) {
        persist_callback_ = std::move(callback);
    }
    
    size_t GetQueueSize() const noexcept {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return outgoing_queue_.size();
    }

private:
    void SendZmqMessage(Message msg);
    void EnqueueMessage(Message msg);
    
    zmq::message_t identity_;
    
    IMessageSender& message_sender_;  
    const Config& config_;             
    
    std::string name_;
    std::atomic<bool> is_online_{true};
    std::queue<Message> outgoing_queue_;
    mutable std::mutex queue_mutex_;
    std::chrono::steady_clock::time_point last_receive_;
    std::chrono::steady_clock::time_point last_activity_;
      
    std::function<void(const std::string&, const Message&)> persist_callback_;
};

}