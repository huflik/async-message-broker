#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include "message.hpp"

namespace broker {

/**
 * Структура для отложенного сообщения с ID
 */
struct PendingMessage {
    uint64_t id;
    Message msg;
};

/**
 * Статусы сообщений в БД
 */
enum MessageStatus : int {
    STATUS_PENDING = 0,      // Ожидает доставки
    STATUS_DELIVERED = 1,    // Доставлено (получено подтверждение от клиента)
    STATUS_SENT = 2          // Отправлено, но ACK ещё не получен
};

class Storage {
public:
    explicit Storage(const std::string& db_path);
    ~Storage();
    
    /**
     * Сохраняет сообщение в БД со статусом PENDING
     * @return ID сообщения в БД
     */
    uint64_t SaveMessage(const Message& msg);
    
    /**
     * Сохраняет сообщение с указанным статусом
     */
    uint64_t SaveMessageWithStatus(const Message& msg, int status);
    
    /**
     * Загружает все отложенные сообщения для клиента (где клиент - получатель)
     * Загружает сообщения со статусом PENDING или SENT
     */
    std::vector<PendingMessage> LoadPending(const std::string& client_name);
    
    /**
     * Загружает только сообщения со статусом PENDING (не SENT)
     */
    std::vector<PendingMessage> LoadPendingOnly(const std::string& client_name);
    
    /**
     * Загружает все ответы (Reply), предназначенные для указанного отправителя
     * Используется для доставки ответов, когда original_sender подключается
     * Загружает сообщения со статусом PENDING или SENT
     */
    std::vector<PendingMessage> LoadPendingRepliesForSender(const std::string& sender_name);
    
    /**
     * Помечает сообщение как доставленное (получен ACK)
     */
    void MarkDelivered(uint64_t message_id);
    
    /**
     * Помечает сообщение как отправленное (ждёт ACK)
     */
    void MarkSent(uint64_t message_id);
    
    /**
     * Проверяет, требуется ли для сообщения ACK
     */
    bool NeedsAck(uint64_t message_id);
    
    /**
     * Сохраняет связь между correlation_id и оригинальным отправителем
     * Используется для request-reply паттерна
     */
    void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender);
    
    /**
     * Находит оригинального отправителя по correlation_id
     * @return имя отправителя или пустую строку, если не найден
     */
    std::string FindOriginalSenderByCorrelation(uint64_t correlation_id);
    
    /**
     * Находит message_id по correlation_id
     * @return message_id или 0 если не найден
     */
    uint64_t FindMessageIdByCorrelation(uint64_t correlation_id);
    
    /**
     * @deprecated Оставлен для совместимости, используйте версию с original_sender
     */
    void SaveCorrelation(uint64_t message_id, uint64_t correlation_id) {
        SaveCorrelation(message_id, correlation_id, "");
    }
    
    /**
     * Находит сообщение по correlation_id
     */
    Message FindByCorrelation(uint64_t correlation_id);
    
    /**
     * Получает количество отложенных сообщений для клиента
     */
    uint64_t GetPendingCount(const std::string& client_name);
    
    /**
     * Очищает старые доставленные сообщения
     */
    void CleanupOldMessages(int days = 7);

    /**
     * Загружает сообщения со статусом SENT, отправленные давнее timeout секунд
     */
    std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds);

    /**
     * Возвращает сообщение в статус PENDING
     */
    void MarkPending(uint64_t message_id);

    /**
     * Загружает только ответы со статусом PENDING или SENT для отправителя
     */
    std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name);
    
    /**
     * Удаляет correlation запись после получения ACK от оригинального отправителя
     * @param message_id ID сообщения
     * @param ack_sender Отправитель ACK (проверяется, что это original_sender)
     */
    void MarkAckReceived(uint64_t message_id, const std::string& ack_sender);

    /**
    * Загружает только обычные сообщения (НЕ Reply) для клиента-получателя
    * Загружает сообщения со статусом PENDING
    */
    std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name);

    /**
    * Находит message_id по correlation_id и получателю
    * @return message_id или 0 если не найден
    */
    uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, const std::string& destination);

private:
    void CreateTables();
    void ThrowOnDbError(int rc, const std::string& msg);
    
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex db_mutex_;
};

} // namespace broker