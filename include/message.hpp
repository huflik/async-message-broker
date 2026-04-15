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
 * Unregister - клиент явно отключается
 */
enum class MessageType : uint8_t {
    Register = 1,   // Регистрация клиента
    Message = 2,    // Обычное сообщение
    Reply = 3,      // Ответ на сообщение
    Ack = 4,        // Подтверждение получения
    Unregister = 5  // Явное отключение клиента
};

/**
 * Флаги сообщения (битовые маски)
 */
enum MessageFlag : uint8_t {
    FlagNone = 0,               // 0x00 - флаги не установлены
    FlagNeedsReply = 1 << 0,    // 0x01 - отправитель ожидает ответ
    FlagNeedsAck = 1 << 1       // 0x02 - требуется подтверждение доставки
};

/**
 * Класс Message представляет собой сообщение, передаваемое между клиентами
 */
class Message {
public:
    Message() = default;
    
    Message(MessageType type, 
            uint8_t flags,
            uint64_t correlation_id,
            const std::string& sender,
            const std::string& destination,
            const std::vector<uint8_t>& payload);
    
    MessageType GetType() const noexcept { return type_; }
    uint8_t GetFlags() const noexcept { return flags_; }
    uint64_t GetCorrelationId() const noexcept { return correlation_id_; }
    const std::string& GetSender() const noexcept { return sender_; }
    const std::string& GetDestination() const noexcept { return destination_; }
    const std::vector<uint8_t>& GetPayload() const noexcept { return payload_; }
    
    void SetType(MessageType type) { type_ = type; }
    void SetFlags(uint8_t flags) { flags_ = flags; }
    void SetCorrelationId(uint64_t id) { correlation_id_ = id; }
    void SetSender(const std::string& sender) { sender_ = sender; }
    void SetDestination(const std::string& dest) { destination_ = dest; }
    void SetPayload(const std::vector<uint8_t>& payload) { payload_ = payload; }
    
    bool HasFlag(uint8_t flag) const noexcept { return (flags_ & flag) != 0; }
    void SetFlag(uint8_t flag) { flags_ |= flag; }
    void ClearFlag(uint8_t flag) { flags_ &= ~flag; }
    bool NeedsReply() const noexcept { return HasFlag(FlagNeedsReply); }
    void SetNeedsReply(bool value) {
        if (value) SetFlag(FlagNeedsReply);
        else ClearFlag(FlagNeedsReply);
    }
    bool NeedsAck() const noexcept { return HasFlag(FlagNeedsAck); }
    void SetNeedsAck(bool value) {
        if (value) SetFlag(FlagNeedsAck);
        else ClearFlag(FlagNeedsAck);
    }
    
    // Сериализация
    [[nodiscard]] std::vector<uint8_t> Serialize() const;
    [[nodiscard]] static Message Deserialize(const std::vector<uint8_t>& data);
    
    // Отладка
    std::string ToString() const;

private:
    MessageType type_ = MessageType::Message;
    uint8_t flags_ = FlagNone;
    uint64_t correlation_id_ = 0;
    std::string sender_;
    std::string destination_;
    std::vector<uint8_t> payload_;
};

} // namespace broker