#include "router.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

Router::Router(Storage& storage, zmq::socket_t& router_socket)
    : storage_(storage)
    , router_socket_(router_socket)
{
    spdlog::info("Router initialized");
}

void Router::RouteMessage(const Message& msg, const zmq::message_t& identity) {
    spdlog::debug("Routing message: {}", msg.ToString());
    
    switch (msg.GetType()) {
        case MessageType::Register:
            HandleRegister(msg, identity);
            break;
        case MessageType::Message:
            HandleMessage(msg);
            break;
        case MessageType::Reply:
            HandleReply(msg);
            break;
        case MessageType::Ack:
            HandleAck(msg);
            break;
        default:
            spdlog::warn("Unknown message type: {}", static_cast<int>(msg.GetType()));
            break;
    }
}

void Router::HandleRegister(const Message& msg, const zmq::message_t& identity) {
    const std::string& client_name = msg.GetSender();
    
    if (client_name.empty()) {
        spdlog::warn("Register message with empty name");
        return;
    }
    
    spdlog::info("Registering client: {}", client_name);
    
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = active_clients_.find(client_name);
        if (it != active_clients_.end()) {
            spdlog::warn("Client {} already registered, replacing old session", client_name);
            it->second->MarkOffline();
            active_clients_.erase(it);
        }
    }
    
    zmq::message_t identity_copy(identity.size());
    std::memcpy(identity_copy.data(), identity.data(), identity.size());
    
    auto session = std::make_shared<ZmqSession>(std::move(identity_copy), router_socket_);
    session->SetName(client_name);
    
    if (RegisterClient(client_name, session)) {
        spdlog::info("Client {} registered successfully", client_name);
        DeliverOfflineMessages(client_name);
    }
    
    PrintActiveClients();
}

void Router::HandleMessage(const Message& msg) {
    const std::string& destination = msg.GetDestination();
    
    spdlog::debug("Routing message to: {}", destination);
    
    auto session = FindSession(destination);
    
    if (session && session->IsOnline()) {
        spdlog::debug("Destination {} online, sending immediately", destination);
        session->SendMessage(msg);
    } else {
        spdlog::info("Destination {} offline, storing message", destination);
        // TODO: в Фазе 3 сохраняем в Storage
        // storage_.SaveMessage(msg);
    }
}

void Router::HandleReply(const Message& msg) {
    spdlog::debug("Handling reply with correlation_id: {}", msg.GetCorrelationId());
    
    // Находим сессию получателя (отправителя исходного запроса)
    const std::string& destination = msg.GetDestination();
    spdlog::debug("Reply destination: {}", destination);
    
    auto session = FindSession(destination);
    
    if (session && session->IsOnline()) {
        spdlog::debug("Destination {} online, sending reply immediately", destination);
        session->SendMessage(msg);
    } else {
        spdlog::info("Destination {} offline, storing reply", destination);
        // TODO: в Фазе 3 сохраняем в Storage
        // storage_.SaveMessage(msg);
    }
}

void Router::HandleAck(const Message& msg) {
    spdlog::debug("Handling ACK for correlation_id: {}", msg.GetCorrelationId());
    // TODO: в Фазе 3 обновляем статус сообщения в БД
}

bool Router::RegisterClient(const std::string& name, std::shared_ptr<ZmqSession> session) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    if (active_clients_.find(name) != active_clients_.end()) {
        spdlog::warn("Client name {} already taken", name);
        return false;
    }
    
    active_clients_[name] = session;
    
    const auto& identity = session->GetIdentity();
    std::string identity_str(
        reinterpret_cast<const char*>(identity.data()),
        identity.size()
    );
    identity_to_name_[identity_str] = name;
    
    spdlog::debug("Client registered: {} (identity size: {})", name, identity.size());
    return true;
}

void Router::UnregisterClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    auto it = active_clients_.find(name);
    if (it != active_clients_.end()) {
        const auto& identity = it->second->GetIdentity();
        std::string identity_str(
            reinterpret_cast<const char*>(identity.data()),
            identity.size()
        );
        identity_to_name_.erase(identity_str);
        active_clients_.erase(it);
        spdlog::info("Client unregistered: {}", name);
    }
}

std::shared_ptr<ZmqSession> Router::FindSession(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    auto it = active_clients_.find(name);
    if (it != active_clients_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<ZmqSession> Router::FindSessionByIdentity(const zmq::message_t& identity) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    std::string identity_str(
        reinterpret_cast<const char*>(identity.data()),
        identity.size()
    );
    
    auto it = identity_to_name_.find(identity_str);
    if (it != identity_to_name_.end()) {
        auto session_it = active_clients_.find(it->second);
        if (session_it != active_clients_.end()) {
            return session_it->second;
        }
    }
    return nullptr;
}

void Router::HandleDisconnect(const zmq::message_t& identity) {
    auto session = FindSessionByIdentity(identity);
    
    if (session) {
        std::string name = session->GetName();
        spdlog::info("Client disconnected: {}", name);
        session->MarkOffline();
        UnregisterClient(name);
    }
}

void Router::DeliverOfflineMessages(const std::string& name) {
    auto session = FindSession(name);
    
    if (!session) {
        spdlog::warn("Cannot deliver offline messages: client {} not found", name);
        return;
    }
    
    spdlog::info("Delivering offline messages to: {}", name);
    session->FlushQueue();
}

void Router::PrintActiveClients() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    spdlog::info("=== Active clients ({}) ===", active_clients_.size());
    for (const auto& [name, session] : active_clients_) {
        const auto& identity = session->GetIdentity();
        spdlog::info("  - {} (identity size: {})", name, identity.size());
    }
}

} // namespace broker