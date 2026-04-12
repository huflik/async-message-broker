// session.hpp
#pragma once

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <functional>
#include <future>
#include <zmq.hpp>

#include "message.hpp"
#include "interfaces.hpp"  // ДОБАВИТЬ эту строку
#include <spdlog/spdlog.h>

namespace broker {

// Удаляем forward declaration of Server (больше не нужен)
// class Server;  // УДАЛИТЬ эту строку

class Session {
public:
    // ИЗМЕНИТЬ: вместо Server используем интерфейсы
    explicit Session(zmq::message_t identity, 
                     IMessageSender& message_sender,
                     const Config& config);
    
    ~Session();
    
    bool SendMessage(const Message& msg);
    std::string GetName() const { return name_; }
    bool IsOnline() const { return is_online_; }
    
    const zmq::message_t& GetIdentity() const { return identity_; }
    
    void SetName(const std::string& name) { name_ = name; }
    void FlushQueue();
    
    void MarkOffline() { 
        is_online_ = false; 
        spdlog::debug("Client {} marked as offline", name_);
    }
    
    void MarkOnline() {
        is_online_ = true;
        spdlog::debug("Client {} marked as online", name_);
    }
    
    void UpdateLastReceive() {
        last_receive_ = std::chrono::steady_clock::now();
        spdlog::trace("Client {} last receive updated", name_);
    }
    
    void UpdateLastActivity() {
        last_activity_ = std::chrono::steady_clock::now();
        spdlog::trace("Client {} last activity updated", name_);
    }
    
    bool IsExpired(int timeout_seconds) const {
        if (!is_online_) return true;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_receive_);
        return elapsed.count() > timeout_seconds;
    }
    
    void PersistQueueToDatabase();
    
    // ИЗМЕНИТЬ: сигнатура колбэка теперь принимает IStorage* или функцию
    void SetPersistCallback(std::function<void(const std::string&, const Message&)> callback) {
        persist_callback_ = std::move(callback);
    }
    
    // ИЗМЕНИТЬ: больше не нужен отдельный send_callback, используем IMessageSender
    // Удаляем метод SetSendCallback
    
    size_t GetQueueSize() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return outgoing_queue_.size();
    }

private:
    bool SendZmqMessage(const Message& msg);
    void EnqueueMessage(const Message& msg);
    
    zmq::message_t identity_;
    
    // ИЗМЕНИТЬ: вместо server_ используем интерфейсы
    IMessageSender& message_sender_;  // Для отправки сообщений
    const Config& config_;             // Для доступа к конфигурации
    
    std::string name_;
    bool is_online_ = true;
    std::queue<Message> outgoing_queue_;
    mutable std::mutex queue_mutex_;
    std::chrono::steady_clock::time_point last_receive_;
    std::chrono::steady_clock::time_point last_activity_;
    
    // УДАЛИТЬ: больше не нужен send_callback_, так как используем message_sender_
    // std::function<void(zmq::message_t, zmq::message_t, std::function<void(bool)>)> send_callback_;
    
    std::function<void(const std::string&, const Message&)> persist_callback_;
};

} // namespace broker