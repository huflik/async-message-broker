// router.cpp
#include "router.hpp"
#include "message_handler.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

Router::Router(IStorage& storage, 
               IMessageSender& message_sender,
               IConfigProvider& config_provider,
               std::shared_ptr<IMetrics> metrics)
    : storage_(storage)
    , message_sender_(message_sender)
    , config_provider_(config_provider)
    , metrics_(metrics)
{
    int ack_timeout = config_provider_.GetConfig().AckTimeout;
    spdlog::info("Router initialized with ACK timeout: {}s", ack_timeout);
}

void Router::RouteMessage(const Message& msg, const zmq::message_t& identity) {
    spdlog::debug("Routing message: {}", msg.ToString());
    
    // Находим сессию
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        std::string identity_str(
            reinterpret_cast<const char*>(identity.data()),
            identity.size()
        );
        
        auto it = identity_to_name_.find(identity_str);
        if (it != identity_to_name_.end()) {
            auto session_it = active_clients_.find(it->second);
            if (session_it != active_clients_.end()) {
                session = session_it->second;
                session->UpdateLastReceive();
            }
        }
    }
    
    // Создаем контекст с метриками
    HandlerContext ctx{
        .storage = storage_,
        .session_manager = *this,
        .message_sender = message_sender_,
        .config_provider = config_provider_,
        .identity = identity,
        .metrics = metrics_  
    };
    
    auto handler = MessageHandlerFactory::Create(msg.GetType());
    if (handler) {
        handler->Handle(msg, ctx);
    } else {
        spdlog::warn("Unknown message type: {}", static_cast<int>(msg.GetType()));
    }
}

bool Router::RegisterClient(const std::string& name, std::shared_ptr<Session> session) {
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
    
    // Метрики
    if (metrics_) {
        metrics_->IncrementClientsRegistered();
        metrics_->SetActiveSessions(active_clients_.size());
    }
    
    spdlog::debug("Client registered: {}", name);
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
        
        // Метрики
        if (metrics_) {
            metrics_->IncrementClientsUnregistered();
            metrics_->SetActiveSessions(active_clients_.size());
        }
        
        spdlog::debug("Client unregistered: {}", name);
    }
}

std::shared_ptr<Session> Router::FindSession(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    auto it = active_clients_.find(name);
    if (it != active_clients_.end()) {
        return it->second;
    }
    return nullptr;
}

void Router::DeliverOfflineMessages(const std::string& name) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = active_clients_.find(name);
        if (it != active_clients_.end()) {
            session = it->second;
        }
    }
    
    if (!session) {
        spdlog::warn("Cannot deliver offline messages: client {} not found", name);
        return;
    }
    
    auto pending_messages = storage_.LoadPendingMessagesOnly(name);

    if (!pending_messages.empty() && metrics_) {
        metrics_->IncrementOfflineDelivered();
    }
    
    if (pending_messages.empty()) {
        spdlog::debug("No pending messages for client: {}", name);
        return;
    }
    
    spdlog::info("Delivering {} offline messages to: {}", pending_messages.size(), name);
    
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    
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
    
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = active_clients_.find(name);
        if (it != active_clients_.end()) {
            session = it->second;
        }
    }
    
    if (!session) {
        spdlog::warn("Cannot deliver pending replies: client {} not found", name);
        return;
    }
    
    auto pending_replies = storage_.LoadPendingRepliesForSenderOnly(name);
    
    if (pending_replies.empty()) {
        spdlog::debug("No pending replies for client: {}", name);
        return;
    }
    
    spdlog::info("Delivering {} pending replies to: {}", pending_replies.size(), name);
    
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    
    for (const auto& reply : pending_replies) {
        if (!session->IsOnline()) {
            spdlog::warn("Client {} went offline during reply delivery, stopping", name);
            break;
        }
        
        if (reply.msg.NeedsAck()) {
            storage_.MarkSent(reply.id);
        }
        
        if (session->SendMessage(reply.msg)) {
            storage_.MarkDelivered(reply.id);
            spdlog::debug("Delivered pending reply id={} to {}", reply.id, name);
        } else {
            spdlog::warn("Failed to deliver pending reply id={} to {}, will retry later", reply.id, name);
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
    
    std::vector<std::string> to_remove;
    int timeout = config_provider_.GetConfig().SessionTimeout;
    
    for (const auto& [name, session] : active_clients_) {
        bool should_remove = false;
        
        if (!session->IsOnline()) {
            should_remove = true;
        } else if (timeout > 0 && session->IsExpired(timeout)) {
            spdlog::info("Client {} inactive for {}s, marking as offline", name, timeout);
            session->MarkOffline();
            session->PersistQueueToDatabase();
            should_remove = true;
        }
        
        if (should_remove) {
            to_remove.push_back(name);
        }
    }
    
    for (const auto& name : to_remove) {
        auto it = active_clients_.find(name);
        if (it != active_clients_.end()) {
            const auto& identity = it->second->GetIdentity();
            std::string identity_str(
                reinterpret_cast<const char*>(identity.data()),
                identity.size()
            );
            identity_to_name_.erase(identity_str);
            active_clients_.erase(it);
            
            // Метрики
            if (metrics_) {
                metrics_->IncrementClientsTimeout();
            }
        }
    }
    
    if (!to_remove.empty()) {
        spdlog::info("Cleaned up {} inactive sessions", to_remove.size());
        UpdateSessionMetrics();
    }
}

void Router::CheckExpiredAcks() {
    int ack_timeout_seconds = config_provider_.GetConfig().AckTimeout;
    
    if (ack_timeout_seconds <= 0) return;
    
    auto expired_messages = storage_.LoadExpiredSent(ack_timeout_seconds);

    if (!expired_messages.empty() && metrics_) {
        metrics_->IncrementMessagesExpired();
    }
    
    for (const auto& pending : expired_messages) {
        spdlog::warn("Message {} expired (no ACK received after {} seconds), resetting to PENDING", 
                     pending.id, ack_timeout_seconds);
        
        storage_.MarkPending(pending.id);
        
        std::shared_ptr<Session> session;
        std::string destination = pending.msg.GetDestination();
        
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            auto it = active_clients_.find(destination);
            if (it != active_clients_.end() && it->second->IsOnline()) {
                session = it->second;
            }
        }
        
        if (session) {
            spdlog::debug("Retrying delivery of expired message {} to {}", pending.id, destination);
            storage_.MarkSent(pending.id);
            if (session->SendMessage(pending.msg)) {
                spdlog::debug("Retry successful for message {}", pending.id);
            } else {
                spdlog::warn("Retry failed for message {}", pending.id);
                storage_.MarkPending(pending.id);
            }
        }
    }
}

void Router::UpdateSessionMetrics() {
    if (metrics_) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        metrics_->SetActiveSessions(active_clients_.size());
    }
}

} // namespace broker