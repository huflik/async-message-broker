#include "message.hpp"
#include <cstring>
#include <arpa/inet.h>
#include <sstream>

namespace broker {

namespace {
    constexpr uint8_t PROTOCOL_VERSION = 1;
    constexpr size_t HEADER_SIZE = 15;
    
    uint16_t HostToNetwork16(uint16_t host) { return htons(host); }
    uint16_t NetworkToHost16(uint16_t net) { return ntohs(net); }
    
    uint64_t HostToNetwork64(uint64_t host) {
        return __builtin_bswap64(host);
    }
    
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
    if (sender_.size() > 255) {
        throw std::runtime_error("Sender name too long (max 255 bytes)");
    }
    if (destination_.size() > 255) {
        throw std::runtime_error("Destination name too long (max 255 bytes)");
    }
    if (payload_.size() > 65535) {
        throw std::runtime_error("Payload too large (max 65535 bytes)");
    }
    
    size_t total_size = HEADER_SIZE + sender_.size() + destination_.size() + payload_.size();
    std::vector<uint8_t> buffer;
    buffer.reserve(total_size);
    
    buffer.push_back(PROTOCOL_VERSION);
    buffer.push_back(static_cast<uint8_t>(type_));
    buffer.push_back(flags_);
    
    uint64_t net_corr = HostToNetwork64(correlation_id_);
    const uint8_t* corr_bytes = reinterpret_cast<const uint8_t*>(&net_corr);
    buffer.insert(buffer.end(), corr_bytes, corr_bytes + 8);
    
    buffer.push_back(static_cast<uint8_t>(sender_.size()));
    buffer.push_back(static_cast<uint8_t>(destination_.size()));
    
    uint16_t net_payload_len = HostToNetwork16(static_cast<uint16_t>(payload_.size()));
    const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&net_payload_len);
    buffer.insert(buffer.end(), len_bytes, len_bytes + 2);
    
    buffer.insert(buffer.end(), sender_.begin(), sender_.end());
    buffer.insert(buffer.end(), destination_.begin(), destination_.end());
    buffer.insert(buffer.end(), payload_.begin(), payload_.end());
    
    return buffer;
}

Message Message::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < HEADER_SIZE) {
        throw std::runtime_error("Message too short");
    }
    
    size_t pos = 0;
    uint8_t version = data[pos++];
    if (version != PROTOCOL_VERSION) {
        throw std::runtime_error("Unsupported protocol version: " + std::to_string(version));
    }
    
    Message msg;
    msg.type_ = static_cast<MessageType>(data[pos++]);
    msg.flags_ = data[pos++];
    
    uint64_t net_corr;
    std::memcpy(&net_corr, &data[pos], 8);
    msg.correlation_id_ = NetworkToHost64(net_corr);
    pos += 8;
    
    uint8_t sender_len = data[pos++];
    uint8_t dest_len = data[pos++];
    
    uint16_t net_payload_len;
    std::memcpy(&net_payload_len, &data[pos], 2);
    uint16_t payload_len = NetworkToHost16(net_payload_len);
    pos += 2;
    
    if (data.size() != HEADER_SIZE + sender_len + dest_len + payload_len) {
        throw std::runtime_error("Message size mismatch");
    }
    
    msg.sender_.assign(reinterpret_cast<const char*>(&data[pos]), sender_len);
    pos += sender_len;
    
    msg.destination_.assign(reinterpret_cast<const char*>(&data[pos]), dest_len);
    pos += dest_len;
    
    msg.payload_.assign(data.begin() + pos, data.begin() + pos + payload_len);
    
    return msg;
}

std::string Message::ToString() const {
    std::stringstream ss;
    ss << "Message{type=";
    switch (type_) {
        case MessageType::Register: ss << "Register"; break;
        case MessageType::Message: ss << "Message"; break;
        case MessageType::Reply: ss << "Reply"; break;
        case MessageType::Ack: ss << "Ack"; break;
        case MessageType::Unregister: ss << "Unregister"; break;
        default: ss << static_cast<int>(type_); break;
    }
    ss << ", flags=" << (int)flags_
       << ", corr_id=" << correlation_id_
       << ", sender=" << sender_
       << ", dest=" << destination_
       << ", payload_size=" << payload_.size()
       << "}";
    return ss.str();
}

} 