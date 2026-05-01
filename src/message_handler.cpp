#include "message_handler.hpp"
#include <cstring>
#include <spdlog/spdlog.h>

namespace broker {

void RegisterHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    const std::string& client_name = msg.GetSender();
    
    if (client_name.empty()) {
        spdlog::warn("Register message with empty name");
        return;
    }
    
    spdlog::info("Registering client: {}", client_name);
    
    zmq::message_t identity_copy(ctx.identity.size());
    std::memcpy(identity_copy.data(), ctx.identity.data(), ctx.identity.size());
    
    auto session = std::make_shared<Session>(
        std::move(identity_copy),
        ctx.message_sender,
        ctx.config_provider.GetConfig()
    );
    
    session->SetName(client_name);
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    
    auto ctx_ptr = std::make_shared<HandlerContext>(ctx);
    session->SetPersistCallback([ctx_ptr](const std::string& name, const Message& m) {
        ctx_ptr->session_manager.PersistMessageForClient(name, m);
    });
    
    auto old_session = ctx.session_manager.UpsertClient(client_name, session);
    
    if (old_session) {
        spdlog::info("Client {} re-registered, old session replaced", client_name);
    } else {
        spdlog::info("Client {} registered successfully", client_name);
    }
    
    ctx.session_manager.DeliverOfflineMessages(client_name);
    ctx.session_manager.DeliverPendingReplies(client_name);
    
    ctx.session_manager.PrintActiveClients();
}

void MessageHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    const std::string& destination = msg.GetDestination();
    
    spdlog::debug("Routing message to: {}", destination);
    
    auto session = ctx.session_manager.FindSession(destination);
    bool can_deliver_immediately = session && session->IsOnline();
    
    uint64_t message_id = 0;
    
    try {
        message_id = ctx.storage.SaveMessage(msg);
        spdlog::debug("Message saved to database with id: {}, status PENDING", message_id);
        
        if (msg.NeedsReply() || msg.NeedsAck()) {
            ctx.storage.SaveCorrelation(message_id, msg.GetCorrelationId(), msg.GetSender());
            spdlog::debug("Saved correlation: message_id={}, corr_id={}, sender={}", 
                          message_id, msg.GetCorrelationId(), msg.GetSender());
        }
        
        if (!can_deliver_immediately) {
            spdlog::info("Destination {} offline, message {} stored in database", destination, message_id);
            return;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Database operation failed: {}. Message will not be sent.", e.what());
        if (ctx.metrics) {
            ctx.metrics->IncrementMessagesFailed();
        }
        return;
    }
    
    try {
        session->UpdateLastReceive();
        
        Message msg_copy = msg;
        
        if (session->SendMessage(std::move(msg_copy))) {
            try {
                if (msg.NeedsAck()) {
                    ctx.storage.MarkSent(message_id);
                    spdlog::debug("Message {} marked as SENT, waiting for ACK", message_id);
                } else {
                    ctx.storage.MarkDelivered(message_id);
                    spdlog::debug("Message {} delivered and marked as DELIVERED", message_id);
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to update message status after successful send: {}", e.what());
                if (ctx.metrics) {
                    ctx.metrics->IncrementMessagesFailed();
                }
            }
            spdlog::debug("Message sent successfully to {}", destination);
        } else {
            spdlog::warn("Failed to send message to {}, rolling back status", destination);
            
            try {
                ctx.storage.MarkPending(message_id);
                spdlog::debug("Message {} status remains PENDING (rolled back)", message_id);
            } catch (const std::exception& e) {
                spdlog::error("Failed to rollback message status: {}", e.what());
            }
            
            session->MarkOffline();
            
            if (ctx.metrics) {
                ctx.metrics->IncrementMessagesFailed();
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Exception during send to {}: {}. Rolling back status.", destination, e.what());
        
        try {
            ctx.storage.MarkPending(message_id);
            spdlog::debug("Message {} status rolled back to PENDING after exception", message_id);
        } catch (const std::exception& db_e) {
            spdlog::error("Failed to rollback message status after exception: {}", db_e.what());
        }
        
        session->MarkOffline();
        
        if (ctx.metrics) {
            ctx.metrics->IncrementMessagesFailed();
        }
    }
}

void ReplyHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    spdlog::debug("Handling reply with correlation_id: {}", msg.GetCorrelationId());
    
    Message reply_msg = msg;
    std::string original_sender;
    uint64_t message_id = 0;
    
    try {
        original_sender = ctx.storage.FindOriginalSenderByCorrelation(msg.GetCorrelationId());
        
        if (!original_sender.empty()) {
            reply_msg.SetDestination(original_sender);
            spdlog::debug("Reply redirected to original sender: {}", original_sender);
        }
        
        message_id = ctx.storage.SaveMessage(reply_msg);
        spdlog::debug("Reply saved to database with id: {}, status PENDING", message_id);
        
        std::string correlation_owner = !original_sender.empty() ? original_sender : reply_msg.GetDestination();
        ctx.storage.SaveCorrelation(message_id, msg.GetCorrelationId(), correlation_owner);
        spdlog::debug("Saved correlation for reply: message_id={}, corr_id={}, owner={}", 
                      message_id, msg.GetCorrelationId(), correlation_owner);
        
    } catch (const std::exception& e) {
        spdlog::error("Database operation failed for reply: {}. Reply will not be sent.", e.what());
        if (ctx.metrics) {
            ctx.metrics->IncrementMessagesFailed();
        }
        return;
    }
    
    const std::string& destination = reply_msg.GetDestination();
    auto session = ctx.session_manager.FindSession(destination);
    
    if (session && session->IsOnline()) {
        try {
            if (reply_msg.NeedsAck()) {
                ctx.storage.MarkSent(message_id);
                spdlog::debug("Reply {} marked as SENT, waiting for ACK", message_id);
            }
            
            Message reply_to_send = reply_msg;
            
            if (session->SendMessage(std::move(reply_to_send))) {
                if (!reply_msg.NeedsAck()) {
                    ctx.storage.MarkDelivered(message_id);
                    spdlog::debug("Reply {} delivered and marked as DELIVERED", message_id);
                }
                spdlog::debug("Reply sent successfully to {}", destination);
                return;
            }
            
            spdlog::warn("Failed to send reply to {}, marking offline", destination);
            
            if (reply_msg.NeedsAck()) {
                try {
                    ctx.storage.MarkPending(message_id);
                    spdlog::debug("Reply {} status rolled back to PENDING", message_id);
                } catch (const std::exception& e) {
                    spdlog::error("Failed to rollback reply status: {}", e.what());
                }
            }
            
            session->MarkOffline();
            
            if (ctx.metrics) {
                ctx.metrics->IncrementMessagesFailed();
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Exception during reply send to {}: {}. Rolling back status.", destination, e.what());
            
            try {
                ctx.storage.MarkPending(message_id);
                spdlog::debug("Reply {} status rolled back to PENDING after exception", message_id);
            } catch (const std::exception& db_e) {
                spdlog::error("Failed to rollback reply status after exception: {}", db_e.what());
            }
            
            session->MarkOffline();
            
            if (ctx.metrics) {
                ctx.metrics->IncrementMessagesFailed();
            }
        }
    } else {
        spdlog::info("Reply {} stored in database for later delivery to {}", message_id, destination);
    }
}

void AckHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    spdlog::debug("Handling ACK for correlation_id: {}", msg.GetCorrelationId());
    
    uint64_t acked_correlation_id = msg.GetCorrelationId();
    
    if (acked_correlation_id == 0) {
        spdlog::warn("ACK message with zero correlation_id, ignoring");
        return;
    }
    
    const std::string& ack_sender = msg.GetSender();
    
    std::string original_sender = ctx.storage.FindOriginalSenderByCorrelation(acked_correlation_id);
    
    uint64_t message_id = ctx.storage.FindMessageIdByCorrelation(acked_correlation_id);
    
    if (message_id == 0) {
        message_id = ctx.storage.FindMessageIdByCorrelationAndDestination(
            acked_correlation_id, ack_sender);
        
        if (message_id == 0) {
            spdlog::warn("ACK for unknown correlation_id={} from {}", 
                         acked_correlation_id, ack_sender);
            return;
        }
    }
    
    if (ctx.storage.NeedsAck(message_id)) {
        ctx.storage.MarkDelivered(message_id);
        ctx.storage.MarkAckReceived(message_id, ack_sender);
        spdlog::info("Message {} marked as DELIVERED after ACK from {}", message_id, ack_sender);
        
        if (ctx.metrics) {
            ctx.metrics->IncrementAcksReceived();
        }
        
        if (!original_sender.empty()) {
            auto session = ctx.session_manager.FindSession(original_sender);
            if (session && session->IsOnline()) {
                Message ack_notification;
                ack_notification.SetType(MessageType::Ack);
                ack_notification.SetSender(ack_sender); 
                ack_notification.SetDestination(original_sender);
                ack_notification.SetCorrelationId(acked_correlation_id);
                
                if (session->SendMessage(std::move(ack_notification))) {
                    spdlog::debug("ACK notification sent to original sender: {}", original_sender);
                } else {
                    spdlog::warn("Failed to send ACK notification to {}", original_sender);
                }
            } else {
                spdlog::debug("Original sender {} offline, storing ACK notification", original_sender);
                
                Message ack_notification;
                ack_notification.SetType(MessageType::Ack);
                ack_notification.SetSender(ack_sender);
                ack_notification.SetDestination(original_sender);
                ack_notification.SetCorrelationId(acked_correlation_id);
                
                uint64_t ack_msg_id = ctx.storage.SaveMessage(ack_notification);
                spdlog::debug("ACK notification stored in database with id: {}", ack_msg_id);
            }
        } else {
            spdlog::debug("No original sender found for correlation_id={}, ACK notification not sent", 
                          acked_correlation_id);
        }
    } else {
        spdlog::debug("Message {} does not require ACK, ignoring", message_id);
    }
}

void UnregisterHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    const std::string& client_name = msg.GetSender();
    spdlog::info("Client {} explicitly unregistering", client_name);
    
    auto session = ctx.session_manager.FindSession(client_name);
    if (session) {
        session->PersistQueueToDatabase();
        session->MarkOffline();
        ctx.session_manager.UnregisterClient(client_name);
        spdlog::info("Client {} unregistered successfully", client_name);
    }
}

std::unique_ptr<IMessageHandler> MessageHandlerFactory::Create(MessageType type) {
    switch (type) {
        case MessageType::Register:
            return std::make_unique<RegisterHandler>();
        case MessageType::Message:
            return std::make_unique<MessageHandler>();
        case MessageType::Reply:
            return std::make_unique<ReplyHandler>();
        case MessageType::Ack:
            return std::make_unique<AckHandler>();
        case MessageType::Unregister:
            return std::make_unique<UnregisterHandler>();
        default:
            spdlog::warn("Unknown message type: {}", static_cast<int>(type));
            return nullptr;
    }
}

}