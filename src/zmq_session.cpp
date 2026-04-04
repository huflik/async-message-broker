#include "zmq_session.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

ZmqSession::ZmqSession(zmq::message_t identity, zmq::socket_t& router_socket, Storage* storage)
    : identity_(std::move(identity))
    , router_socket_(router_socket)
    , storage_(storage)
    , last_activity_(std::chrono::steady_clock::now())
{
    spdlog::debug("ZmqSession created for identity with size: {}", identity_.size());
}

bool ZmqSession::CheckConnection() {
    if (!is_online_) {
        return false;
    }
    
    try {
        // Используем новый API для получения событий
        int events = router_socket_.get(zmq::sockopt::events);
        
        if (!(events & ZMQ_POLLOUT)) {
            spdlog::debug("Socket for {} not ready for write, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
        // Пытаемся отправить пустое сообщение с флагом dontwait
        // Это реально проверит, живо ли соединение
        zmq::message_t dummy;
        auto result = router_socket_.send(identity_, zmq::send_flags::dontwait | zmq::send_flags::sndmore);
        if (!result) {
            spdlog::debug("Failed to send test message to {}, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
        // Отменяем отправку (не отправляем остальные фреймы)
        // Для этого просто не отправляем delimiter и data
        
        return true;
        
    } catch (const zmq::error_t& e) {
        spdlog::debug("Connection check failed for {}: error {}", name_, e.num());
        is_online_ = false;
        return false;
    } catch (const std::exception& e) {
        spdlog::debug("Connection check failed for {}: {}", name_, e.what());
        is_online_ = false;
        return false;
    }
}

void ZmqSession::PersistQueueToDatabase() {
    if (!storage_) {
        spdlog::debug("PersistQueueToDatabase: storage is null for {}", name_);
        return;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (outgoing_queue_.empty()) {
        spdlog::debug("No queued messages to persist for {}", name_);
        return;
    }
    
    spdlog::info("Persisting {} queued messages for {} to database", outgoing_queue_.size(), name_);
    
    while (!outgoing_queue_.empty()) {
        const auto& msg = outgoing_queue_.front();
        uint64_t id = storage_->SaveMessage(msg);
        spdlog::debug("Persisted queued message for {} to database, id: {}", name_, id);
        outgoing_queue_.pop();
    }
}

bool ZmqSession::SendMessage(const Message& msg) {
    // Обновляем время последней активности
    UpdateLastActivity();
    
    if (!is_online_) {
        EnqueueMessage(msg);
        spdlog::debug("Client {} offline, message queued", name_);
        return true;
    }
    
    // Перед отправкой проверяем, живо ли соединение
    if (!CheckConnection()) {
        spdlog::warn("Connection check failed for {}, marking offline and queuing message", name_);
        EnqueueMessage(msg);
        return false;
    }
    
    return SendZmqMessage(msg);
}

bool ZmqSession::SendZmqMessage(const Message& msg) {
    try {
        auto serialized = msg.Serialize();
        
        // Отправляем identity
        auto result = router_socket_.send(identity_, zmq::send_flags::sndmore);
        if (!result) {
            spdlog::warn("Failed to send identity to {}, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
        // Отправляем пустой разделитель
        zmq::message_t delimiter;
        result = router_socket_.send(delimiter, zmq::send_flags::sndmore);
        if (!result) {
            spdlog::warn("Failed to send delimiter to {}, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
        // Отправляем данные
        zmq::message_t data(serialized.data(), serialized.size());
        result = router_socket_.send(data, zmq::send_flags::dontwait);
        
        if (!result) {
            spdlog::warn("Failed to send data to {}, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
        spdlog::debug("Message sent to client {}: {}", name_, msg.ToString());
        return true;
        
    } catch (const zmq::error_t& e) {
        if (e.num() == EAGAIN || e.num() == EFSM || e.num() == ETERM || e.num() == ENOTSOCK) {
            spdlog::warn("Client {} not reachable (error {}), marking offline", name_, e.num());
            is_online_ = false;
        } else {
            spdlog::error("Failed to send message to {}: {}", name_, e.what());
        }
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Failed to send message to {}: {}", name_, e.what());
        return false;
    }
}

void ZmqSession::EnqueueMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    outgoing_queue_.push(msg);
    spdlog::debug("Message queued for client {}, queue size: {}", name_, outgoing_queue_.size());
}

void ZmqSession::FlushQueue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    spdlog::debug("Flushing queue for client {}, size: {}", name_, outgoing_queue_.size());
    
    while (!outgoing_queue_.empty()) {
        auto msg = outgoing_queue_.front();
        outgoing_queue_.pop();
        
        if (!SendZmqMessage(msg)) {
            spdlog::warn("Failed to send queued message to {}, re-queuing", name_);
            outgoing_queue_.push(msg);
            break;
        }
    }
}

} // namespace broker