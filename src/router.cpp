#include "router.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

Router::Router(Storage& storage, zmq::socket_t& router_socket)
    : storage_(storage)
    , router_socket_(router_socket)
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
    
    auto session = std::make_shared<ZmqSession>(std::move(identity_copy), router_socket_, &storage_);
    session->SetName(client_name);
    session->UpdateLastActivity();
    
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
    
    // Сохраняем в БД со статусом PENDING
    uint64_t message_id = storage_.SaveMessage(msg);
    spdlog::debug("Message saved to database with id: {}", message_id);
    
    // Если сообщение требует ответа - сохраняем correlation
    if (msg.NeedsReply()) {
        storage_.SaveCorrelation(message_id, msg.GetCorrelationId(), msg.GetSender());
        spdlog::debug("Saved correlation: corr_id={} -> original_sender={}", 
                      msg.GetCorrelationId(), msg.GetSender());
    }
    
    auto session = FindSession(destination);
    
    bool can_deliver = false;
    if (session) {
        if (session->IsTimedOut(5)) {
            spdlog::info("Client {} inactive for 5 seconds, marking offline", destination);
            session->MarkOffline();
        }
        else if (session->CheckConnection()) {
            can_deliver = true;
        } else {
            spdlog::info("Client {} connection check failed, marking offline", destination);
            session->MarkOffline();
        }
    }
    
    if (can_deliver && session && session->IsOnline()) {
        spdlog::debug("Destination {} online, sending immediately", destination);
        
        // Если сообщение требует ACK, помечаем как SENT перед отправкой
        if (msg.NeedsAck()) {
            storage_.MarkSent(message_id);
            spdlog::debug("Message {} marked as SENT, waiting for ACK", message_id);
        }
        
        if (session->SendMessage(msg)) {
            if (!msg.NeedsAck()) {
                // Если ACK не требуется, сразу помечаем как доставленное
                storage_.MarkDelivered(message_id);
                spdlog::debug("Message {} delivered and marked as delivered (no ACK required)", message_id);
            } else {
                spdlog::debug("Message {} sent, waiting for ACK from client", message_id);
            }
        } else {
            spdlog::info("Failed to send message to {}, will be delivered later", destination);
            // Возвращаем статус на PENDING при ошибке отправки
            // TODO: реализовать возврат статуса
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
    
    uint64_t message_id = storage_.SaveMessage(reply_msg);
    spdlog::debug("Reply saved to database with id: {}", message_id);
    
    const std::string& destination = reply_msg.GetDestination();
    spdlog::debug("Reply destination: {}", destination);
    
    auto session = FindSession(destination);
    
    bool can_deliver = false;
    if (session) {
        if (session->IsTimedOut(5)) {
            spdlog::info("Client {} inactive for 5 seconds, marking offline", destination);
            session->MarkOffline();
        }
        else if (session->CheckConnection()) {
            can_deliver = true;
        } else {
            spdlog::info("Client {} connection check failed, marking offline", destination);
            session->MarkOffline();
        }
    }
    
    if (can_deliver && session && session->IsOnline()) {
        spdlog::debug("Destination {} online, sending reply immediately", destination);
        
        if (reply_msg.NeedsAck()) {
            storage_.MarkSent(message_id);
        }
        
        if (session->SendMessage(reply_msg)) {
            if (!reply_msg.NeedsAck()) {
                storage_.MarkDelivered(message_id);
                spdlog::debug("Reply {} delivered and marked as delivered", message_id);
            } else {
                spdlog::debug("Reply {} sent, waiting for ACK", message_id);
            }
        } else {
            spdlog::info("Failed to send reply to {}, will be delivered later", destination);
        }
    } else {
        spdlog::info("Destination {} offline, reply {} stored in database for later delivery", 
                     destination, message_id);
        
        if (!original_sender.empty() && original_sender == destination) {
            spdlog::info("Reply for offline sender {} saved, will be delivered on reconnect", 
                         original_sender);
        }
    }
}

void Router::HandleAck(const Message& msg) {
    spdlog::debug("Handling ACK for correlation_id: {}", msg.GetCorrelationId());
    
    // ACK сообщение должно содержать correlation_id подтверждаемого сообщения
    uint64_t acked_correlation_id = msg.GetCorrelationId();
    
    if (acked_correlation_id == 0) {
        spdlog::warn("ACK message with zero correlation_id, ignoring");
        return;
    }
    
    // Ищем сообщение по correlation_id
    Message original_msg = storage_.FindByCorrelation(acked_correlation_id);
    
    if (original_msg.GetType() == MessageType::Message || original_msg.GetType() == MessageType::Reply) {
        // TODO: нужно хранить mapping от correlation_id к message_id
        // Пока используем fallback - ищем по correlation_id в таблице correlations
        spdlog::info("ACK received for correlation_id={} from {}", acked_correlation_id, msg.GetSender());
        
        // В реальной реализации нужно найти message_id по correlation_id
        // и вызвать storage_.MarkDelivered(message_id);
        
        // Отправляем подтверждение брокера клиенту (опционально)
        spdlog::debug("ACK processed successfully");
    } else {
        spdlog::warn("ACK for unknown correlation_id={}", acked_correlation_id);
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
    auto session = FindSession(name);
    
    if (!session) {
        spdlog::warn("Cannot deliver offline messages: client {} not found", name);
        return;
    }
    
    auto pending_messages = storage_.LoadPending(name);
    
    if (pending_messages.empty()) {
        spdlog::debug("No pending messages for client: {}", name);
    } else {
        spdlog::info("Delivering {} offline messages to: {}", pending_messages.size(), name);
        
        for (const auto& pending : pending_messages) {
            // Если сообщение требует ACK, помечаем как SENT перед отправкой
            if (pending.msg.NeedsAck()) {
                storage_.MarkSent(pending.id);
            }
            
            if (session->SendMessage(pending.msg)) {
                if (!pending.msg.NeedsAck()) {
                    storage_.MarkDelivered(pending.id);
                }
                spdlog::debug("Delivered offline message id={} to {}", pending.id, name);
            } else {
                spdlog::warn("Failed to deliver offline message id={} to {}", pending.id, name);
                break;
            }
        }
    }
    
    session->FlushQueue();
}

void Router::DeliverPendingReplies(const std::string& name) {
    auto session = FindSession(name);
    
    if (!session) {
        spdlog::warn("Cannot deliver pending replies: client {} not found", name);
        return;
    }
    
    auto pending_replies = storage_.LoadPendingRepliesForSender(name);
    
    if (pending_replies.empty()) {
        spdlog::debug("No pending replies for client: {}", name);
        return;
    }
    
    spdlog::info("Delivering {} pending replies to: {}", pending_replies.size(), name);
    
    for (const auto& reply : pending_replies) {
        if (reply.msg.NeedsAck()) {
            storage_.MarkSent(reply.id);
        }
        
        if (session->SendMessage(reply.msg)) {
            if (!reply.msg.NeedsAck()) {
                storage_.MarkDelivered(reply.id);
            }
            spdlog::debug("Delivered pending reply id={} to original sender {}", reply.id, name);
        } else {
            spdlog::warn("Failed to deliver pending reply id={} to {}", reply.id, name);
            break;
        }
    }
}

void Router::PrintActiveClients() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    spdlog::info("=== Active clients ({}) ===", active_clients_.size());
    for (const auto& [name, session] : active_clients_) {
        const auto& identity = session->GetIdentity();
        if (identity.size() > 0) {
            spdlog::info("  - {} (identity size: {})", name, identity.size());
        } else {
            spdlog::info("  - {} (identity: disconnected)", name);
        }
    }
}

void Router::CleanupInactiveSessions() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    const int TIMEOUT_SECONDS = 5;
    
    std::vector<std::string> to_remove;
    
    for (const auto& [name, session] : active_clients_) {
        if (session->IsTimedOut(TIMEOUT_SECONDS)) {
            spdlog::debug("Session {} timed out (no activity for {} seconds)", name, TIMEOUT_SECONDS);
            to_remove.push_back(name);
        } 
        else if (!session->IsOnline()) {
            spdlog::debug("Marked {} for cleanup (offline)", name);
            to_remove.push_back(name);
        }
    }
    
    for (const auto& name : to_remove) {
        auto it = active_clients_.find(name);
        if (it != active_clients_.end()) {
            it->second->PersistQueueToDatabase();
            
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

} // namespace broker