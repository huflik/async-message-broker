#include "session.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

Session::Session(zmq::message_t identity, 
                 IMessageSender& message_sender,
                 const Config& config)
    : identity_(std::move(identity))
    , message_sender_(message_sender)
    , config_(config)
    , last_receive_(std::chrono::steady_clock::now())
    , last_activity_(std::chrono::steady_clock::now())
{
    spdlog::debug("Session created for identity with size: {}", identity_.size());
}

Session::~Session() {
    PersistQueueToDatabase();
    spdlog::debug("Session destroyed for client: {}", name_);
}

void Session::PersistQueueToDatabase() {
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
        persist_callback_(name_, outgoing_queue_.front());
        persisted_count++;
        outgoing_queue_.pop();
    }
    
    spdlog::debug("Persisted {} messages for {}", persisted_count, name_);
}

bool Session::SendMessage(Message msg) {
    UpdateLastActivity();
    
    if (!IsOnline()) {
        EnqueueMessage(std::move(msg));
        spdlog::debug("Client {} offline, message queued", name_);
        return true;
    }
    
    SendZmqMessage(std::move(msg));
    return true;
}

void Session::SendZmqMessage(Message msg) {
    try {
        auto serialized = msg.Serialize();
        
        zmq::message_t identity_copy(identity_.size());
        std::memcpy(identity_copy.data(), identity_.data(), identity_.size());
        
        zmq::message_t data(serialized.data(), serialized.size());
        
        std::string client_name = name_;
        
        message_sender_.SendToClient(std::move(identity_copy), std::move(data), 
            [this, client_name](bool success) {
                if (!success) {
                    spdlog::warn("Failed to send message to {}, marking offline", client_name);
                    MarkOffline();
                } else {
                    spdlog::trace("Message sent to {}", client_name);
                }
            });
        
    } catch (const zmq::error_t& e) {
        spdlog::error("ZMQ error sending to {}: {}, marking offline", name_, e.what());
        MarkOffline();
    } catch (const std::exception& e) {
        spdlog::error("Failed to send message to {}: {}, marking offline", name_, e.what());
        MarkOffline();
    }
}

void Session::EnqueueMessage(Message msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    outgoing_queue_.push(std::move(msg));
    spdlog::debug("Message queued for client {}, queue size: {}", name_, outgoing_queue_.size());
    
    constexpr size_t QUEUE_PERSIST_THRESHOLD = 100;
    if (outgoing_queue_.size() > QUEUE_PERSIST_THRESHOLD && persist_callback_) {
        spdlog::debug("Queue size {} exceeded threshold {}, persisting to database", 
                      outgoing_queue_.size(), QUEUE_PERSIST_THRESHOLD);
        
        size_t to_persist = outgoing_queue_.size() / 2;
        for (size_t i = 0; i < to_persist && !outgoing_queue_.empty(); ++i) {
            persist_callback_(name_, outgoing_queue_.front());
            outgoing_queue_.pop();
        }
        spdlog::debug("Persisted {} messages, {} remaining in queue", to_persist, outgoing_queue_.size());
    }
}

void Session::FlushQueue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    spdlog::debug("Flushing queue for client {}, size: {}", name_, outgoing_queue_.size());
    
    size_t sent_count = 0;
    
    while (!outgoing_queue_.empty()) {
        if (!IsOnline()) {
            spdlog::debug("Client {} went offline during flush, stopping", name_);
            break;
        }
        
        auto msg = std::move(outgoing_queue_.front());
        outgoing_queue_.pop();
        
        SendZmqMessage(std::move(msg));
        sent_count++;
    }
    
    if (sent_count > 0) {
        spdlog::debug("Flushed {} messages to {}", sent_count, name_);
    }
}

}