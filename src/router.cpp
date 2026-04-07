#include "router.hpp"
#include "server.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

Router::Router(Storage& storage, zmq::socket_t& router_socket, Server& server)
    : storage_(storage)
    , router_socket_(router_socket)
    , server_(server)
    , offline_manager_(storage)
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
            it->second->PersistQueueToDatabase();
            it->second->MarkOffline();
            active_clients_.erase(it);
        }
    }
    
    zmq::message_t identity_copy(identity.size());
    std::memcpy(identity_copy.data(), identity.data(), identity.size());
    
    auto session = std::make_shared<ZmqSession>(std::move(identity_copy), server_);
    session->SetName(client_name);
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    session->UpdateHeartbeat();
    
    session->SetSendCallback([this](zmq::message_t identity, zmq::message_t data, 
                                     std::function<void(bool)> callback) {
        server_.SendMessage(std::move(identity), std::move(data), std::move(callback));
    });
    
    session->SetPersistCallback([this](const std::string& client_name, const Message& msg) {
        PersistMessageForClient(client_name, msg);
    });
    
    if (RegisterClient(client_name, session)) {
        spdlog::info("Client {} registered successfully", client_name);
        
        DeliverOfflineMessages(client_name);
        DeliverPendingReplies(client_name);
    }
    
    PrintActiveClients();
}

void Router::HandleMessage(const Message& msg) {
    const std::string& destination = msg.GetDestination();
    
    spdlog::debug("Routing message to: {}", destination);
    
    uint64_t message_id = storage_.SaveMessage(msg);
    spdlog::debug("Message saved to database with id: {}", message_id);
    
    if (msg.NeedsReply()) {
        storage_.SaveCorrelation(message_id, msg.GetCorrelationId(), msg.GetSender());
        spdlog::debug("Saved correlation: corr_id={} -> original_sender={}", 
                      msg.GetCorrelationId(), msg.GetSender());
    }
    
    std::shared_ptr<ZmqSession> session = FindSession(destination);
    
    bool can_deliver = false;
    if (session) {
        session->UpdateLastReceive();
        session->UpdateHeartbeat();
        
        if (session->IsTimedOut(60)) {
            spdlog::info("Client {} inactive for 60 seconds, marking offline", destination);
            session->MarkOffline();
        }
        else if (session->IsOnline()) {
            can_deliver = true;
        }
    }
    
    if (can_deliver && session && session->IsOnline()) {
        spdlog::debug("Destination {} online, sending immediately", destination);
        
        if (msg.NeedsAck()) {
            storage_.MarkSent(message_id);
            spdlog::debug("Message {} marked as SENT, waiting for ACK", message_id);
        }
        
        if (session->SendMessage(msg)) {
            if (!msg.NeedsAck()) {
                storage_.MarkDelivered(message_id);
                spdlog::debug("Message {} delivered and marked as delivered", message_id);
            } else {
                spdlog::debug("Message {} sent, waiting for ACK from client", message_id);
            }
        } else {
            spdlog::info("Failed to send message to {}, marking offline", destination);
            session->MarkOffline();
        }
    } else {
        spdlog::info("Destination {} offline, message {} stored in database", destination, message_id);
    }
}

void Router::HandleReply(const Message& msg) {
    spdlog::debug("Handling reply with correlation_id: {}", msg.GetCorrelationId());
    
    std::string original_sender = storage_.FindOriginalSenderByCorrelation(msg.GetCorrelationId());
    
    Message reply_msg = msg;
    
    if (!original_sender.empty()) {
        reply_msg.SetDestination(original_sender);
        spdlog::debug("Reply redirected to original sender: {}", original_sender);
    } else {
        spdlog::warn("No original sender found for correlation_id={}, using destination={}", 
                     msg.GetCorrelationId(), msg.GetDestination());
    }
    
    const std::string& destination = reply_msg.GetDestination();
    spdlog::debug("Reply destination: {}", destination);
    
    // ВСЕГДА сохраняем reply в БД - гарантия от потери
    uint64_t message_id = storage_.SaveMessage(reply_msg);
    spdlog::debug("Reply saved to database with id: {}", message_id);
    
    if (!original_sender.empty()) {
        storage_.SaveCorrelation(message_id, msg.GetCorrelationId(), original_sender);
        spdlog::debug("Saved correlation for reply: message_id={}, corr_id={}, original_sender={}", 
                      message_id, msg.GetCorrelationId(), original_sender);
    }
    
    // Пытаемся доставить сразу (оптимизация)
    std::shared_ptr<ZmqSession> session = FindSession(destination);
    
    if (session && session->IsOnline()) {
        spdlog::debug("Attempting immediate delivery of reply {} to {}", message_id, destination);
        
        if (reply_msg.NeedsAck()) {
            storage_.MarkSent(message_id);
        }
        
        if (session->SendMessage(reply_msg)) {
            if (!reply_msg.NeedsAck()) {
                storage_.MarkDelivered(message_id);
                spdlog::debug("Reply {} delivered immediately", message_id);
            } else {
                spdlog::debug("Reply {} sent, waiting for ACK", message_id);
            }
            return;
        }
        
        // Отправка не удалась - помечаем сессию как offline
        spdlog::info("Immediate delivery failed for reply {}, marking {} offline", message_id, destination);
        session->MarkOffline();
    }
    
    // Если дошли сюда - сообщение уже в БД со статусом PENDING
    // При переподключении клиента будет доставлено через DeliverPendingReplies
    spdlog::info("Reply {} stored in database for later delivery to {}", message_id, destination);
}

void Router::HandleAck(const Message& msg) {
    spdlog::debug("Handling ACK for correlation_id: {}", msg.GetCorrelationId());
    
    uint64_t acked_correlation_id = msg.GetCorrelationId();
    
    if (acked_correlation_id == 0) {
        spdlog::warn("ACK message with zero correlation_id, ignoring");
        return;
    }
    
    uint64_t message_id = storage_.FindMessageIdByCorrelation(acked_correlation_id);
    
    if (message_id == 0) {
        spdlog::warn("ACK for unknown correlation_id={}", acked_correlation_id);
        return;
    }
    
    if (storage_.NeedsAck(message_id)) {
        storage_.MarkDelivered(message_id);
        spdlog::info("Message {} marked as DELIVERED after ACK from {}", message_id, msg.GetSender());
    } else {
        spdlog::debug("Message {} does not require ACK, ignoring", message_id);
    }
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
    std::shared_ptr<ZmqSession> session = FindSession(name);
    
    if (!session) {
        spdlog::warn("Cannot deliver offline messages: client {} not found", name);
        return;
    }
    
    auto pending_messages = storage_.LoadPendingOnly(name);
    
    if (pending_messages.empty()) {
        spdlog::debug("No pending messages for client: {}", name);
        return;
    }
    
    spdlog::info("Delivering {} offline messages to: {}", pending_messages.size(), name);
    
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    session->UpdateHeartbeat();
    
    for (const auto& pending : pending_messages) {
        if (!session->IsOnline()) {
            spdlog::warn("Client {} went offline during delivery, stopping", name);
            break;
        }
        
        if (pending.msg.NeedsAck()) {
            storage_.MarkSent(pending.id);
        }
        
        if (session->SendMessage(pending.msg)) {
            if (!pending.msg.NeedsAck()) {
                storage_.MarkDelivered(pending.id);
                spdlog::debug("Delivered offline message id={} to {} (no ACK needed)", pending.id, name);
            } else {
                spdlog::debug("Delivered offline message id={} to {}, waiting for ACK", pending.id, name);
            }
        } else {
            spdlog::warn("Failed to deliver offline message id={} to {}", pending.id, name);
            break;
        }
    }
    
    session->FlushQueue();
}

void Router::DeliverPendingReplies(const std::string& name) {
    spdlog::debug("DeliverPendingReplies called for client: {}", name);
    
    std::shared_ptr<ZmqSession> session = FindSession(name);
    
    if (!session) {
        spdlog::warn("Cannot deliver pending replies: client {} not found", name);
        return;
    }
    
    // Загружаем reply со статусом PENDING или SENT
    auto pending_replies = storage_.LoadPendingRepliesForSenderOnly(name);
    
    if (pending_replies.empty()) {
        spdlog::debug("No pending replies for client: {}", name);
        return;
    }
    
    spdlog::info("Delivering {} pending replies to: {}", pending_replies.size(), name);
    
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    session->UpdateHeartbeat();
    
    for (const auto& reply : pending_replies) {
        if (!session->IsOnline()) {
            spdlog::warn("Client {} went offline during reply delivery, stopping", name);
            break;
        }
        
        spdlog::debug("Attempting to deliver pending reply id={} to {}", reply.id, name);
        
        if (session->SendMessage(reply.msg)) {
            storage_.MarkDelivered(reply.id);
            spdlog::debug("Delivered pending reply id={} to {}", reply.id, name);
        } else {
            spdlog::warn("Failed to deliver pending reply id={} to {}, will retry later", reply.id, name);
            // Не помечаем как delivered - при следующем подключении будет повторная попытка
        }
    }
}

void Router::PersistMessageForClient(const std::string& client_name, const Message& msg) {
    Message persistent_msg = msg;
    persistent_msg.SetDestination(client_name);
    uint64_t id = storage_.SaveMessage(persistent_msg);
    spdlog::debug("Persisted message for client {} to database, id: {}", client_name, id);
}

void Router::PrintActiveClients() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    spdlog::info("=== Active clients ({}) ===", active_clients_.size());
    for (const auto& [name, session] : active_clients_) {
        const auto& identity = session->GetIdentity();
        if (identity.size() > 0) {
            spdlog::info("  - {} (identity size: {}, queue size: {})", 
                         name, identity.size(), session->GetQueueSize());
        } else {
            spdlog::info("  - {} (identity: disconnected, queue size: {})", 
                         name, session->GetQueueSize());
        }
    }
}

void Router::CleanupInactiveSessions() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    constexpr int TIMEOUT_SECONDS = 60;
    std::vector<std::string> to_remove;
    
    for (const auto& [name, session] : active_clients_) {
        if (!session->IsOnline()) {
            to_remove.push_back(name);
        }
        else if (session->IsTimedOut(TIMEOUT_SECONDS)) {
            spdlog::debug("Session {} timed out (no activity for {} seconds)", name, TIMEOUT_SECONDS);
            to_remove.push_back(name);
        }
    }
    
    for (const auto& name : to_remove) {
        auto it = active_clients_.find(name);
        if (it != active_clients_.end()) {
            it->second->PersistQueueToDatabase();
            
            if (it->second->GetQueueSize() > 0) {
                spdlog::warn("Client {} still has {} messages in queue after persist", 
                             name, it->second->GetQueueSize());
            }
            
            const auto& identity = it->second->GetIdentity();
            std::string identity_str(
                reinterpret_cast<const char*>(identity.data()),
                identity.size()
            );
            identity_to_name_.erase(identity_str);
            active_clients_.erase(it);
            spdlog::debug("Cleaned up inactive session for: {}", name);
        }
    }
    
    if (!to_remove.empty()) {
        spdlog::info("Cleaned up {} inactive sessions", to_remove.size());
    }
}

void Router::CheckHeartbeats() {
    // Для production: минимальная проверка для отладки
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    size_t online_count = 0;
    for (const auto& [name, session] : active_clients_) {
        if (session->IsOnline()) {
            online_count++;
        }
    }
    
    if (online_count > 0) {
        spdlog::trace("Heartbeat check: {} online clients", online_count);
    }
}

} // namespace broker