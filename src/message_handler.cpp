// src/message_handler.cpp
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
    
    // Проверяем, не зарегистрирован ли уже клиент
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
        
        // Доставляем оффлайн сообщения
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
    
    // Сохраняем correlation если нужен ACK ИЛИ Reply
    if (msg.NeedsReply() || msg.NeedsAck()) {
        ctx.storage.SaveCorrelation(message_id, msg.GetCorrelationId(), msg.GetSender());
        spdlog::debug("Saved correlation: message_id={}, corr_id={}, sender={}", 
                      message_id, msg.GetCorrelationId(), msg.GetSender());
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
    }
    
    const std::string& destination = reply_msg.GetDestination();
    
    uint64_t message_id = ctx.storage.SaveMessage(reply_msg);
    spdlog::debug("Reply saved to database with id: {}", message_id);
    
    // Сохраняем correlation для reply
    std::string correlation_owner = !original_sender.empty() ? original_sender : reply_msg.GetDestination();
    ctx.storage.SaveCorrelation(message_id, msg.GetCorrelationId(), correlation_owner);
    spdlog::debug("Saved correlation for reply: message_id={}, corr_id={}, owner={}", 
                  message_id, msg.GetCorrelationId(), correlation_owner);
    
    auto session = ctx.session_manager.FindSession(destination);
    
    if (session && session->IsOnline()) {
        if (reply_msg.NeedsAck()) {
            ctx.storage.MarkSent(message_id);
        }
        
        if (session->SendMessage(reply_msg)) {
            if (!reply_msg.NeedsAck()) {
                ctx.storage.MarkDelivered(message_id);
            }
            return;
        }
        
        session->MarkOffline();
    }
    
    spdlog::info("Reply {} stored in database for later delivery to {}", message_id, destination);
}

// ================================================================
// ИСПРАВЛЕННЫЙ AckHandler - теперь отправляет ACK оригинальному отправителю
// ================================================================
void AckHandler::Handle(const Message& msg, const HandlerContext& ctx) {
    spdlog::debug("Handling ACK for correlation_id: {}", msg.GetCorrelationId());
    
    uint64_t acked_correlation_id = msg.GetCorrelationId();
    
    if (acked_correlation_id == 0) {
        spdlog::warn("ACK message with zero correlation_id, ignoring");
        return;
    }
    
    const std::string& ack_sender = msg.GetSender();
    
    // Ищем оригинального отправителя (кому нужно отправить ACK-уведомление)
    std::string original_sender = ctx.storage.FindOriginalSenderByCorrelation(acked_correlation_id);
    
    // Ищем сообщение по correlation_id
    uint64_t message_id = ctx.storage.FindMessageIdByCorrelation(acked_correlation_id);
    
    if (message_id == 0) {
        // Пробуем найти с учетом destination
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
        
        // ========== ДОБАВЛЕНО: Отправка ACK оригинальному отправителю ==========
        if (!original_sender.empty()) {
            auto session = ctx.session_manager.FindSession(original_sender);
            if (session && session->IsOnline()) {
                // Создаем ACK-уведомление для отправки оригинальному отправителю
                Message ack_notification;
                ack_notification.SetType(MessageType::Ack);
                ack_notification.SetSender(ack_sender);  // Кто отправил ACK
                ack_notification.SetDestination(original_sender);
                ack_notification.SetCorrelationId(acked_correlation_id);
                
                if (session->SendMessage(ack_notification)) {
                    spdlog::debug("ACK notification sent to original sender: {}", original_sender);
                } else {
                    spdlog::warn("Failed to send ACK notification to {}", original_sender);
                }
            } else {
                // Если отправитель офлайн, сохраняем ACK как pending сообщение
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
        // =====================================================================
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