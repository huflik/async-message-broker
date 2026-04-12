// storage.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include "message.hpp"
#include "interfaces.hpp"  // ДОБАВИТЬ эту строку

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

// ИЗМЕНИТЬ: добавить наследование от IStorage
class Storage : public IStorage {
public:
    explicit Storage(const std::string& db_path);
    ~Storage();
    
    // ДОБАВИТЬ override ко всем публичным методам
    
    /**
     * Сохраняет сообщение в БД со статусом PENDING
     * @return ID сообщения в БД
     */
    uint64_t SaveMessage(const Message& msg) override;
    
    /**
     * Помечает сообщение как доставленное (получен ACK)
     */
    void MarkDelivered(uint64_t message_id) override;
    
    /**
     * Помечает сообщение как отправленное (ждёт ACK)
     */
    void MarkSent(uint64_t message_id) override;
    
    /**
     * Проверяет, требуется ли для сообщения ACK
     */
    bool NeedsAck(uint64_t message_id) override;
    
    /**
     * Сохраняет связь между correlation_id и оригинальным отправителем
     */
    void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, 
                         const std::string& original_sender) override;
    
    /**
     * Находит оригинального отправителя по correlation_id
     * @return имя отправителя или пустую строку, если не найден
     */
    std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) override;
    
    /**
     * Находит message_id по correlation_id
     * @return message_id или 0 если не найден
     */
    uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) override;

    /**
     * Загружает сообщения со статусом SENT, отправленные давнее timeout секунд
     */
    std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) override;

    /**
     * Возвращает сообщение в статус PENDING
     */
    void MarkPending(uint64_t message_id) override;

    /**
     * Загружает только ответы со статусом PENDING или SENT для отправителя
     */
    std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name) override;
    
    /**
     * Удаляет correlation запись после получения ACK от оригинального отправителя
     * @param message_id ID сообщения
     * @param ack_sender Отправитель ACK (проверяется, что это original_sender)
     */
    void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) override;

    /**
     * Загружает только обычные сообщения (НЕ Reply) для клиента-получателя
     * Загружает сообщения со статусом PENDING
     */
    std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name) override;

    /**
     * Находит message_id по correlation_id и получателю
     * @return message_id или 0 если не найден
     */
    uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, 
                                                        const std::string& destination) override;

private:
    void CreateTables();
    void ThrowOnDbError(int rc, const std::string& msg);
    uint64_t SaveMessageWithStatus(const Message& msg, int status);   
    
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex db_mutex_;
};

} // namespace broker