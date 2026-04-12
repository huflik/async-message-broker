// message_handler.cpp
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
    
    // Проверяем, не зарегистрирован ли уже клиент (как в оригинале)
    auto existing_session = ctx.session_manager.FindSession(client_name);
    if (existing_session) {
        spdlog::warn("Client {} already registered, replacing old session", client_name);
        
        const auto& old_identity = existing_session->GetIdentity();
        std::string old_identity_str(
            reinterpret_cast<const char*>(old_identity.data()),
            old_identity.size()
        );
        
        existing_session->PersistQueueToDatabase();
        existing_session->MarkOffline();
        ctx.session_manager.UnregisterClient(client_name);
        
        spdlog::debug("Old session for {} completely removed", client_name);
    }
    
    // Копируем identity для сессии
    zmq::message_t identity_copy(ctx.identity.size());
    std::memcpy(identity_copy.data(), ctx.identity.data(), ctx.identity.size());
    
    // Создаем новую сессию
    auto session = std::make_shared<Session>(
        std::move(identity_copy),
        ctx.message_sender,
        ctx.config_provider.GetConfig()
    );
    
    session->SetName(client_name);
    session->UpdateLastActivity();
    session->UpdateLastReceive();
    
    // Установка колбэка для персистенции
    session->SetPersistCallback([&ctx](const std::string& name, const Message& m) {
        ctx.session_manager.PersistMessageForClient(name, m);
    });
    
    // Регистрируем клиента
    if (ctx.session_manager.RegisterClient(client_name, session)) {
        spdlog::info("Client {} registered successfully", client_name);
        
        // Доставляем оффлайн сообщения (как в оригинале)
        ctx.session_manager.DeliverOfflineMessages(client_name);
        ctx.session_manager.DeliverPendingReplies(client_name);
    } else {
        spdlog::error("Failed to register client {}", client_name);
    }
    
    ctx.session_manager.PrintActiveClients();
}

void MessageHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    const std::string& destination = msg.GetDestination();
    
    spdlog::debug("Routing message to: {}", destination);
    
    uint64_t message_id = ctx.storage.SaveMessage(msg);
    spdlog::debug("Message saved to database with id: {}", message_id);
    
    if (msg.NeedsReply()) {
        ctx.storage.SaveCorrelation(message_id, msg.GetCorrelationId(), msg.GetSender());
        spdlog::debug("Saved correlation: corr_id={} -> original_sender={}", 
                      msg.GetCorrelationId(), msg.GetSender());
    }
    
    auto session = ctx.session_manager.FindSession(destination);
    
    bool can_deliver = false;
    if (session) {
        session->UpdateLastReceive();
        if (session->IsOnline()) {
            can_deliver = true;
        }
    }
    
    if (can_deliver && session && session->IsOnline()) {
        spdlog::debug("Destination {} online, sending immediately", destination);
        
        if (msg.NeedsAck()) {
            ctx.storage.MarkSent(message_id);
            spdlog::debug("Message {} marked as SENT, waiting for ACK", message_id);
        }
        
        if (session->SendMessage(msg)) {
            if (!msg.NeedsAck()) {
                ctx.storage.MarkDelivered(message_id);
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

void ReplyHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    spdlog::debug("Handling reply with correlation_id: {}", msg.GetCorrelationId());
    
    std::string original_sender = ctx.storage.FindOriginalSenderByCorrelation(msg.GetCorrelationId());
    
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
    
    uint64_t message_id = ctx.storage.SaveMessage(reply_msg);
    spdlog::debug("Reply saved to database with id: {}", message_id);
    
    std::string correlation_owner = !original_sender.empty() ? original_sender : reply_msg.GetDestination();
    ctx.storage.SaveCorrelation(message_id, msg.GetCorrelationId(), correlation_owner);
    spdlog::debug("Saved correlation for reply: message_id={}, corr_id={}, owner={}", 
                  message_id, msg.GetCorrelationId(), correlation_owner);
    
    auto session = ctx.session_manager.FindSession(destination);
    
    if (session && session->IsOnline()) {
        spdlog::debug("Attempting immediate delivery of reply {} to {}", message_id, destination);
        
        if (reply_msg.NeedsAck()) {
            ctx.storage.MarkSent(message_id);
        }
        
        if (session->SendMessage(reply_msg)) {
            if (!reply_msg.NeedsAck()) {
                ctx.storage.MarkDelivered(message_id);
                spdlog::debug("Reply {} delivered immediately", message_id);
            } else {
                spdlog::debug("Reply {} sent, waiting for ACK", message_id);
            }
            return;
        }
        
        spdlog::info("Immediate delivery failed for reply {}, marking {} offline", message_id, destination);
        session->MarkOffline();
    }
    
    spdlog::info("Reply {} stored in database for later delivery to {}", message_id, destination);
}

void AckHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    spdlog::debug("Handling ACK for correlation_id: {}", msg.GetCorrelationId());
    
    uint64_t acked_correlation_id = msg.GetCorrelationId();
    
    if (acked_correlation_id == 0) {
        spdlog::warn("ACK message with zero correlation_id, ignoring");
        return;
    }
    
    const std::string& ack_sender = msg.GetSender();
    
    uint64_t message_id = ctx.storage.FindMessageIdByCorrelationAndDestination(
        acked_correlation_id, ack_sender);
    
    if (message_id == 0) {
        message_id = ctx.storage.FindMessageIdByCorrelation(acked_correlation_id);
        if (message_id == 0) {
            spdlog::warn("ACK for unknown correlation_id={}", acked_correlation_id);
            return;
        }
        spdlog::debug("Using fallback: found message_id={} for correlation_id={}", 
                      message_id, acked_correlation_id);
    }
    
    if (ctx.storage.NeedsAck(message_id)) {
        ctx.storage.MarkDelivered(message_id);
        ctx.storage.MarkAckReceived(message_id, ack_sender);
        spdlog::info("Message {} marked as DELIVERED after ACK from {}", message_id, ack_sender);
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

} // namespace broker