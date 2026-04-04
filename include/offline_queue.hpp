#pragma once

#include "message.hpp"
#include "storage.hpp"
#include "session.hpp"
#include <spdlog/spdlog.h>

namespace broker {

/**
 * OfflineQueueManager
 * 
 * Управляет очередями сообщений для офлайн-клиентов
 */
class OfflineQueueManager {
public:
    explicit OfflineQueueManager(Storage& storage) : storage_(storage) {}
    
    /**
     * Сохраняет сообщение для офлайн-клиента
     */
    void Enqueue(const std::string& client, const Message& msg) {
        uint64_t id = storage_.SaveMessage(msg);
        spdlog::debug("Message for offline client {} saved with id: {}", client, id);
    }
    
    /**
     * Доставляет все отложенные сообщения клиенту
     */
    void Deliver(const std::string& client, Session& session) {
        auto pending_messages = storage_.LoadPending(client);
        
        spdlog::debug("Delivering {} offline messages to {}", pending_messages.size(), client);
        
        for (const auto& pending : pending_messages) {
            Message msg_to_send = pending.msg;
            
            // Если это ответ (Reply), проверяем, нужно ли переадресовать
            if (msg_to_send.GetType() == MessageType::Reply) {
                std::string original_sender = storage_.FindOriginalSenderByCorrelation(
                    msg_to_send.GetCorrelationId()
                );
                if (!original_sender.empty() && original_sender != client) {
                    spdlog::debug("Redirecting offline reply from {} to original sender {}", 
                                  client, original_sender);
                    msg_to_send.SetDestination(original_sender);
                }
            }
            
            if (session.SendMessage(msg_to_send)) {
                storage_.MarkDelivered(pending.id);
                spdlog::debug("Delivered offline message id={} to {}", pending.id, client);
            } else {
                spdlog::warn("Failed to deliver offline message id={} to {}", pending.id, client);
            }
        }
    }
    
    /**
     * Получает количество отложенных сообщений для клиента
     */
    uint64_t GetPendingCount(const std::string& client) {
        return storage_.GetPendingCount(client);
    }

private:
    Storage& storage_;
};

} // namespace broker