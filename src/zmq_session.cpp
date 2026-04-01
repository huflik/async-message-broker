#include "zmq_session.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

ZmqSession::ZmqSession(zmq::message_t identity, zmq::socket_t& router_socket)
    : identity_(std::move(identity))
    , router_socket_(router_socket)
{
    spdlog::debug("ZmqSession created for identity with size: {}", identity_.size());
}

bool ZmqSession::SendMessage(const Message& msg) {
    if (!is_online_) {
        EnqueueMessage(msg);
        spdlog::debug("Client {} offline, message queued", name_);
        return true;
    }
    return SendZmqMessage(msg);
}

bool ZmqSession::SendZmqMessage(const Message& msg) {
    try {
        auto serialized = msg.Serialize();
        
        // 1. Отправляем identity получателя
        router_socket_.send(identity_, zmq::send_flags::sndmore);
        
        // 2. Отправляем пустой разделитель (НУЖЕН для ROUTER → DEALER)
        zmq::message_t delimiter;
        router_socket_.send(delimiter, zmq::send_flags::sndmore);
        
        // 3. Отправляем данные
        zmq::message_t data(serialized.data(), serialized.size());
        router_socket_.send(data, zmq::send_flags::none);
        
        spdlog::debug("Message sent to client {}: {}", name_, msg.ToString());
        return true;
        
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