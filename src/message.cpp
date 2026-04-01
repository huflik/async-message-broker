#include "message.hpp"
#include <cstring>      // для std::memcpy
#include <arpa/inet.h>  // для htons/ntohs (сетевой порядок байт)

namespace broker {

namespace {
    // Константы протокола
    constexpr uint8_t PROTOCOL_VERSION = 1;      // Текущая версия протокола
    
    // Размер фиксированного заголовка в байтах:
    // version(1) + type(1) + flags(1) + correlation_id(8) = 11
    // + sender_len(1) + dest_len(1) + payload_len(2) = 4
    // Итого: 15 байт
    constexpr size_t HEADER_SIZE = 15;
    
    /**
     * Преобразует 16-битное значение из порядка хоста в сетевой (big-endian)
     * htons = host to network short
     */
    uint16_t HostToNetwork16(uint16_t host) {
        return htons(host);
    }
    
    /**
     * Преобразует 16-битное значение из сетевого порядка в порядок хоста
     */
    uint16_t NetworkToHost16(uint16_t net) {
        return ntohs(net);
    }
    
    /**
     * Преобразует 64-битное значение из порядка хоста в сетевой (big-endian)
     */
    uint64_t HostToNetwork64(uint64_t host) {
        return __builtin_bswap64(host);
    }
    
    /**
     * Преобразует 64-битное значение из сетевого порядка в порядок хоста
     */
    uint64_t NetworkToHost64(uint64_t net) {
        return __builtin_bswap64(net);
    }
}

Message::Message(MessageType type, 
                 uint8_t flags,
                 uint64_t correlation_id,
                 const std::string& sender,
                 const std::string& destination,
                 const std::vector<uint8_t>& payload)
    : type_(type)
    , flags_(flags)
    , correlation_id_(correlation_id)
    , sender_(sender)
    , destination_(destination)
    , payload_(payload)
{}

std::vector<uint8_t> Message::Serialize() const {
    // Проверка ограничений протокола
    if (sender_.size() > 255) {
        throw std::runtime_error("Sender name too long (max 255 bytes)");
    }
    if (destination_.size() > 255) {
        throw std::runtime_error("Destination name too long (max 255 bytes)");
    }
    if (payload_.size() > 65535) {
        throw std::runtime_error("Payload too large (max 65535 bytes)");
    }
    
    // Вычисляем общий размер сообщения
    size_t total_size = HEADER_SIZE + sender_.size() + destination_.size() + payload_.size();
    std::vector<uint8_t> buffer;
    buffer.reserve(total_size);  // Резервируем память для оптимизации
    
    // ==== Заголовок фиксированной длины ====
    
    // version (1 байт) - позволяет в будущем менять формат
    buffer.push_back(PROTOCOL_VERSION);
    
    // type (1 байт) - определяет как интерпретировать сообщение
    buffer.push_back(static_cast<uint8_t>(type_));
    
    // flags (1 байт) - битовые флаги (NeedsReply)
    buffer.push_back(flags_);
    
    // correlation_id (8 байт) в сетевом порядке байт
    uint64_t net_corr = HostToNetwork64(correlation_id_);
    const uint8_t* corr_bytes = reinterpret_cast<const uint8_t*>(&net_corr);
    buffer.insert(buffer.end(), corr_bytes, corr_bytes + 8);
    
    // sender_len (1 байт) - длина имени отправителя
    buffer.push_back(static_cast<uint8_t>(sender_.size()));
    
    // dest_len (1 байт) - длина имени получателя
    buffer.push_back(static_cast<uint8_t>(destination_.size()));
    
    // payload_len (2 байта) в сетевом порядке
    uint16_t net_payload_len = HostToNetwork16(static_cast<uint16_t>(payload_.size()));
    const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&net_payload_len);
    buffer.insert(buffer.end(), len_bytes, len_bytes + 2);
    
    // ==== Переменные поля ====
    
    // Имя отправителя (UTF-8 строка)
    buffer.insert(buffer.end(), sender_.begin(), sender_.end());
    
    // Имя получателя (UTF-8 строка)
    buffer.insert(buffer.end(), destination_.begin(), destination_.end());
    
    // Полезная нагрузка (бинарные данные)
    buffer.insert(buffer.end(), payload_.begin(), payload_.end());
    
    return buffer;
}

Message Message::Deserialize(const std::vector<uint8_t>& data) {
    // Проверка минимального размера
    if (data.size() < HEADER_SIZE) {
        throw std::runtime_error("Message too short, expected at least " + 
                                 std::to_string(HEADER_SIZE) + " bytes, got " +
                                 std::to_string(data.size()));
    }
    
    size_t pos = 0;
    
    // Чтение и проверка версии протокола
    uint8_t version = data[pos++];
    if (version != PROTOCOL_VERSION) {
        throw std::runtime_error("Unsupported protocol version: " + std::to_string(version));
    }
    
    Message msg;
    
    // type - как интерпретировать сообщение
    msg.type_ = static_cast<MessageType>(data[pos++]);
    
    // flags - битовые флаги
    msg.flags_ = data[pos++];
    
    // correlation_id (8 байт) - для связывания запроса и ответа
    uint64_t net_corr;
    std::memcpy(&net_corr, &data[pos], 8);
    msg.correlation_id_ = NetworkToHost64(net_corr);
    pos += 8;
    
    // Длины переменных полей
    uint8_t sender_len = data[pos++];
    uint8_t dest_len = data[pos++];
    
    // Длина payload
    uint16_t net_payload_len;
    std::memcpy(&net_payload_len, &data[pos], 2);
    uint16_t payload_len = NetworkToHost16(net_payload_len);
    pos += 2;
    
    // Проверка соответствия размера
    if (data.size() != HEADER_SIZE + sender_len + dest_len + payload_len) {
        throw std::runtime_error("Message size mismatch: expected " + 
                                 std::to_string(HEADER_SIZE + sender_len + dest_len + payload_len) +
                                 ", got " + std::to_string(data.size()));
    }
    
    // Чтение имени отправителя
    msg.sender_.assign(reinterpret_cast<const char*>(&data[pos]), sender_len);
    pos += sender_len;
    
    // Чтение имени получателя
    msg.destination_.assign(reinterpret_cast<const char*>(&data[pos]), dest_len);
    pos += dest_len;
    
    // Чтение полезной нагрузки
    msg.payload_.assign(data.begin() + pos, data.begin() + pos + payload_len);
    
    return msg;
}

std::string Message::ToString() const {
    std::stringstream ss;
    ss << "Message{";
    
    // Тип сообщения
    ss << "type=";
    switch (type_) {
        case MessageType::Register: ss << "Register"; break;
        case MessageType::Message: ss << "Message"; break;
        case MessageType::Reply: ss << "Reply"; break;
        case MessageType::Ack: ss << "Ack"; break;
        default: ss << static_cast<int>(type_); break;
    }
    
    // Флаг needs_reply
    if (flags_ != FlagNone) {
        ss << ", needs_reply=true";
    } else {
        ss << ", needs_reply=false";
    }
    
    // Correlation ID
    ss << ", corr_id=" << correlation_id_;
    
    // Отправитель и получатель
    ss << ", sender=" << sender_;
    ss << ", dest=" << destination_;
    
    // Размер полезной нагрузки
    ss << ", payload_size=" << payload_.size();
    
    ss << "}";
    return ss.str();
}

} // namespace broker