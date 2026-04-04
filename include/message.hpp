#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>

namespace broker {

/**
 * Типы сообщений, поддерживаемые брокером
 * 
 * Register - клиент регистрируется под логическим именем
 * Message  - обычное сообщение от одного клиента другому
 * Reply    - ответ на предыдущее сообщение (request-reply паттерн)
 * Ack      - подтверждение получения сообщения (application-level ACK)
 */
enum class MessageType : uint8_t {
    Register = 1,   // Регистрация клиента
    Message = 2,    // Обычное сообщение
    Reply = 3,      // Ответ на сообщение
    Ack = 4         // Подтверждение получения
};

/**
 * Флаги сообщения (битовые маски)
 * 
 * Используются для передачи дополнительной информации о сообщении.
 * Битовая маска позволяет комбинировать несколько флагов в одном байте.
 */
enum MessageFlag : uint8_t {
    FlagNone = 0,               // 0x00 - флаги не установлены
    FlagNeedsReply = 1 << 0,    // 0x01 - отправитель ожидает ответ
    FlagNeedsAck = 1 << 1       // 0x02 - требуется подтверждение доставки
};

/**
 * Класс Message представляет собой сообщение, передаваемое между клиентами
 * 
 * Отвечает за:
 * - Хранение всех полей сообщения
 * - Сериализацию в бинарный формат для передачи по сети
 * - Десериализацию из бинарного формата
 * 
 * Формат сериализации (15 байт заголовка + переменные поля):
 * [0] version      - 1 байт, версия протокола (1)
 * [1] type         - 1 байт, тип сообщения (1-4)
 * [2] flags        - 1 байт, битовые флаги (NeedsReply, NeedsAck)
 * [3-10] corr_id   - 8 байт, correlation_id в сетевом порядке (big-endian)
 * [11] sender_len  - 1 байт, длина имени отправителя (0-255)
 * [12] dest_len    - 1 байт, длина имени получателя (0-255)
 * [13-14] payload_len - 2 байта, длина payload (0-65535) в сетевом порядке
 * [15+] sender     - sender_len байт, имя отправителя (UTF-8)
 * [15+sender_len] destination - dest_len байт, имя получателя (UTF-8)
 * [15+sender_len+dest_len] payload - payload_len байт, полезные данные
 */
class Message {
public:
    // Конструктор по умолчанию, создаёт пустое сообщение
    Message() = default;
    
    /**
     * Конструктор с параметрами
     * 
     * @param type тип сообщения (Register, Message, Reply, Ack)
     * @param flags битовые флаги (NeedsReply, NeedsAck)
     * @param correlation_id идентификатор для связывания запроса и ответа
     * @param sender логическое имя отправителя
     * @param destination логическое имя получателя
     * @param payload бинарные данные (может быть пустым)
     */
    Message(MessageType type, 
            uint8_t flags,
            uint64_t correlation_id,
            const std::string& sender,
            const std::string& destination,
            const std::vector<uint8_t>& payload);
    
    // ==================== Геттеры ====================
    MessageType GetType() const { return type_; }
    uint8_t GetFlags() const { return flags_; }
    uint64_t GetCorrelationId() const { return correlation_id_; }
    const std::string& GetSender() const { return sender_; }
    const std::string& GetDestination() const { return destination_; }
    const std::vector<uint8_t>& GetPayload() const { return payload_; }
    
    // ==================== Сеттеры ====================
    void SetType(MessageType type) { type_ = type; }
    void SetFlags(uint8_t flags) { flags_ = flags; }
    void SetCorrelationId(uint64_t id) { correlation_id_ = id; }
    void SetSender(const std::string& sender) { sender_ = sender; }
    void SetDestination(const std::string& dest) { destination_ = dest; }
    void SetPayload(const std::vector<uint8_t>& payload) { payload_ = payload; }
    
    // ==================== Работа с флагами ====================
    
    /**
     * Проверяет, установлен ли указанный флаг
     */
    bool HasFlag(uint8_t flag) const {
        return (flags_ & flag) != 0;
    }
    
    /**
     * Устанавливает указанный флаг
     */
    void SetFlag(uint8_t flag) {
        flags_ |= flag;
    }
    
    /**
     * Снимает указанный флаг
     */
    void ClearFlag(uint8_t flag) {
        flags_ &= ~flag;
    }
    
    /**
     * Проверяет, ожидает ли отправитель ответ на это сообщение
     */
    bool NeedsReply() const { 
        return HasFlag(FlagNeedsReply); 
    }
    
    /**
     * Устанавливает или снимает флаг NEEDS_REPLY
     */
    void SetNeedsReply(bool value) {
        if (value) SetFlag(FlagNeedsReply);
        else ClearFlag(FlagNeedsReply);
    }
    
    /**
     * Проверяет, требуется ли подтверждение доставки
     */
    bool NeedsAck() const {
        return HasFlag(FlagNeedsAck);
    }
    
    /**
     * Устанавливает или снимает флаг NEEDS_ACK
     */
    void SetNeedsAck(bool value) {
        if (value) SetFlag(FlagNeedsAck);
        else ClearFlag(FlagNeedsAck);
    }
    
    // ==================== Сериализация ====================
    
    /**
     * Сериализует сообщение в бинарный формат
     * @return вектор байт, готовый для отправки через ZeroMQ
     */
    std::vector<uint8_t> Serialize() const;
    
    /**
     * Десериализует сообщение из бинарного формата
     * @param data байтовый массив, полученный из сети
     * @return восстановленный объект Message
     */
    static Message Deserialize(const std::vector<uint8_t>& data);
    
    // ==================== Отладка ====================
    
    /**
     * Преобразует сообщение в строку для логирования
     */
    std::string ToString() const;

private:
    MessageType type_ = MessageType::Message;  // Тип сообщения (1 байт)
    uint8_t flags_ = FlagNone;                  // Битовые флаги (1 байт)
    uint64_t correlation_id_ = 0;              // ID для связывания запроса и ответа (8 байт)
    std::string sender_;                       // Логическое имя отправителя
    std::string destination_;                  // Логическое имя получателя
    std::vector<uint8_t> payload_;             // Полезная нагрузка
};

} // namespace broker