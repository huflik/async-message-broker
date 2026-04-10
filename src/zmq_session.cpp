#include "zmq_session.hpp"
#include "server.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

ZmqSession::ZmqSession(zmq::message_t identity, Server& server)
    : identity_(std::move(identity))
    , server_(server)
    , last_activity_(std::chrono::steady_clock::now())
    , last_receive_(std::chrono::steady_clock::now())
    , last_heartbeat_(std::chrono::steady_clock::now())
{
    spdlog::debug("ZmqSession created for identity with size: {}", identity_.size());
}

ZmqSession::~ZmqSession() {
    PersistQueueToDatabase();
    spdlog::debug("ZmqSession destroyed for client: {}", name_);
}

bool ZmqSession::CheckConnection() {
    if (!is_online_) {
        return false;
    }
    return true;
}

void ZmqSession::PersistQueueToDatabase() {
    if (!persist_callback_) {
        spdlog::debug("PersistQueueToDatabase: callback not set for {}", name_);
        return;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (outgoing_queue_.empty()) {
        spdlog::debug("No queued messages to persist for {}", name_);
        return;
    }
    
    spdlog::info("Persisting {} queued messages for {} to database", outgoing_queue_.size(), name_);
    
    size_t persisted_count = 0;
    while (!outgoing_queue_.empty()) {
        const auto& msg = outgoing_queue_.front();
        persist_callback_(name_, msg);
        persisted_count++;
        outgoing_queue_.pop();
    }
    
    spdlog::debug("Persisted {} messages for {}", persisted_count, name_);
}

bool ZmqSession::SendMessage(const Message& msg) {
    UpdateLastActivity();
    UpdateHeartbeat();
    
    if (!is_online_) {
        EnqueueMessage(msg);
        spdlog::debug("Client {} offline, message queued", name_);
        return true;
    }
    
    return SendZmqMessage(msg);
}

bool ZmqSession::SendZmqMessage(const Message& msg) {
    if (!send_callback_) {
        spdlog::error("Send callback not set for session {}", name_);
        return false;
    }
    
    try {
        auto serialized = msg.Serialize();
        
        zmq::message_t identity_copy(identity_.size());
        std::memcpy(identity_copy.data(), identity_.data(), identity_.size());
        
        zmq::message_t data(serialized.data(), serialized.size());
        
        bool sent = false;
        bool callback_called = false;
        std::promise<bool> promise;
        auto future = promise.get_future();
        
        send_callback_(std::move(identity_copy), std::move(data), 
                      [&promise, &sent, &callback_called, this](bool success) {
                          sent = success;
                          callback_called = true;
                          if (!success) {
                              spdlog::warn("Send callback reported failure for {}", name_);
                              is_online_ = false;
                          }
                          promise.set_value(success);
                      });
        
        auto status = future.wait_for(std::chrono::milliseconds(1000));
        if (status == std::future_status::timeout) {
            spdlog::warn("Send timeout for client {}, marking offline", name_);
            is_online_ = false;
            // Исправление: не оставляем висящий promise
            // promise уже не будет установлен, но это нормально для timeout
            // Важно: не удаляем session, просто помечаем offline
            return false;
        }
        
        // Исправление: проверяем, что callback был вызван
        if (!callback_called) {
            spdlog::error("Callback was not called for client {}", name_);
            is_online_ = false;
            return false;
        }
        
        if (sent) {
            spdlog::debug("Message sent to client {}: {}", name_, msg.ToString());
        } else {
            spdlog::warn("Failed to send message to {}, marking offline", name_);
            is_online_ = false;
        }
        
        return sent;
        
    } catch (const zmq::error_t& e) {
        spdlog::error("ZMQ error sending to {}: {}, marking offline", name_, e.what());
        is_online_ = false;
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Failed to send message to {}: {}, marking offline", name_, e.what());
        is_online_ = false;
        return false;
    }
}

void ZmqSession::EnqueueMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    outgoing_queue_.push(msg);
    spdlog::debug("Message queued for client {}, queue size: {}", name_, outgoing_queue_.size());
    
    constexpr size_t QUEUE_PERSIST_THRESHOLD = 100;
    if (outgoing_queue_.size() > QUEUE_PERSIST_THRESHOLD && persist_callback_) {
        spdlog::debug("Queue size {} exceeded threshold {}, persisting to database", 
                      outgoing_queue_.size(), QUEUE_PERSIST_THRESHOLD);
        
        size_t to_persist = outgoing_queue_.size() / 2;
        for (size_t i = 0; i < to_persist && !outgoing_queue_.empty(); ++i) {
            const auto& oldest = outgoing_queue_.front();
            persist_callback_(name_, oldest);
            outgoing_queue_.pop();
        }
        spdlog::debug("Persisted {} messages, {} remaining in queue", to_persist, outgoing_queue_.size());
    }
}

void ZmqSession::FlushQueue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    spdlog::debug("Flushing queue for client {}, size: {}", name_, outgoing_queue_.size());
    
    size_t sent_count = 0;
    size_t failed_count = 0;
    
    while (!outgoing_queue_.empty()) {
        auto msg = outgoing_queue_.front();
        outgoing_queue_.pop();
        
        if (SendZmqMessage(msg)) {
            sent_count++;
        } else {
            spdlog::warn("Failed to send queued message to {}, re-queuing", name_);
            outgoing_queue_.push(msg);
            failed_count++;
            break;
        }
    }
    
    if (sent_count > 0 || failed_count > 0) {
        spdlog::debug("Flushed {} messages to {}, {} failed", sent_count, name_, failed_count);
    }
}

} // namespace broker