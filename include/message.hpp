#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>

namespace broker {

enum class MessageType : uint8_t {
    Register = 1,   
    Message = 2,   
    Reply = 3,     
    Ack = 4,        
    Unregister = 5 
};

enum MessageFlag : uint8_t {
    FlagNone = 0,               
    FlagNeedsReply = 1 << 0,    
    FlagNeedsAck = 1 << 1       
};

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
    
    [[nodiscard]] std::vector<uint8_t> Serialize() const;
    [[nodiscard]] static Message Deserialize(const std::vector<uint8_t>& data);
    
    std::string ToString() const;

private:
    MessageType type_ = MessageType::Message;
    uint8_t flags_ = FlagNone;
    uint64_t correlation_id_ = 0;
    std::string sender_;
    std::string destination_;
    std::vector<uint8_t> payload_;
};

} 