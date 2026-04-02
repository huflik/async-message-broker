#include "zmq_session.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

ZmqSession::ZmqSession(zmq::message_t identity, zmq::socket_t& router_socket)
    : identity_(std::move(identity))
    , router_socket_(router_socket)
    , last_activity_(std::chrono::steady_clock::now())
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
        
        auto result = router_socket_.send(identity_, zmq::send_flags::sndmore);
        if (!result) {
            spdlog::warn("Failed to send identity to {}, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
        zmq::message_t delimiter;
        result = router_socket_.send(delimiter, zmq::send_flags::sndmore);
        if (!result) {
            spdlog::warn("Failed to send delimiter to {}, marking offline", name_);
            is_online_ = false;
            return false;
        }
        
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