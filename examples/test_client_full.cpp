#include <zmq.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <memory>

// Добавляем недостающие заголовки для сетевого порядка байт
#include <arpa/inet.h>  // для htons, ntohs

// Подключаем официальный Message класс брокера
// В реальном проекте путь будет: #include "message.hpp"
// Для теста копируем необходимые определения

namespace test {

// Минимальная копия необходимых определений из брокера
// В реальном проекте используйте #include "message.hpp"

enum class MessageType : uint8_t {
    Register = 1,
    Message = 2,
    Reply = 3,
    Ack = 4
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
            const std::vector<uint8_t>& payload)
        : type_(type)
        , flags_(flags)
        , correlation_id_(correlation_id)
        , sender_(sender)
        , destination_(destination)
        , payload_(payload)
    {}
    
    // Геттеры
    MessageType GetType() const { return type_; }
    uint8_t GetFlags() const { return flags_; }
    uint64_t GetCorrelationId() const { return correlation_id_; }
    const std::string& GetSender() const { return sender_; }
    const std::string& GetDestination() const { return destination_; }
    const std::vector<uint8_t>& GetPayload() const { return payload_; }
    
    // Флаги
    bool HasFlag(uint8_t flag) const { return (flags_ & flag) != 0; }
    bool NeedsReply() const { return HasFlag(FlagNeedsReply); }
    bool NeedsAck() const { return HasFlag(FlagNeedsAck); }
    
    // Сериализация
    std::vector<uint8_t> Serialize() const {
        constexpr uint8_t PROTOCOL_VERSION = 1;
        constexpr size_t HEADER_SIZE = 15;
        
        if (sender_.size() > 255) throw std::runtime_error("Sender too long");
        if (destination_.size() > 255) throw std::runtime_error("Destination too long");
        if (payload_.size() > 65535) throw std::runtime_error("Payload too large");
        
        std::vector<uint8_t> buffer;
        buffer.reserve(HEADER_SIZE + sender_.size() + destination_.size() + payload_.size());
        
        buffer.push_back(PROTOCOL_VERSION);
        buffer.push_back(static_cast<uint8_t>(type_));
        buffer.push_back(flags_);
        
        uint64_t net_corr = __builtin_bswap64(correlation_id_);
        const uint8_t* corr_bytes = reinterpret_cast<const uint8_t*>(&net_corr);
        buffer.insert(buffer.end(), corr_bytes, corr_bytes + 8);
        
        buffer.push_back(static_cast<uint8_t>(sender_.size()));
        buffer.push_back(static_cast<uint8_t>(destination_.size()));
        
        uint16_t net_len = htons(static_cast<uint16_t>(payload_.size()));
        const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&net_len);
        buffer.insert(buffer.end(), len_bytes, len_bytes + 2);
        
        buffer.insert(buffer.end(), sender_.begin(), sender_.end());
        buffer.insert(buffer.end(), destination_.begin(), destination_.end());
        buffer.insert(buffer.end(), payload_.begin(), payload_.end());
        
        return buffer;
    }
    
    static Message Deserialize(const std::vector<uint8_t>& data) {
        constexpr uint8_t PROTOCOL_VERSION = 1;
        constexpr size_t HEADER_SIZE = 15;
        
        if (data.size() < HEADER_SIZE) {
            throw std::runtime_error("Message too short");
        }
        
        size_t pos = 0;
        uint8_t version = data[pos++];
        if (version != PROTOCOL_VERSION) {
            throw std::runtime_error("Unsupported protocol version");
        }
        
        Message msg;
        msg.type_ = static_cast<MessageType>(data[pos++]);
        msg.flags_ = data[pos++];
        
        uint64_t net_corr;
        std::memcpy(&net_corr, &data[pos], 8);
        msg.correlation_id_ = __builtin_bswap64(net_corr);
        pos += 8;
        
        uint8_t sender_len = data[pos++];
        uint8_t dest_len = data[pos++];
        
        uint16_t net_payload_len;
        std::memcpy(&net_payload_len, &data[pos], 2);
        uint16_t payload_len = ntohs(net_payload_len);
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
    
    std::string ToString() const {
        std::stringstream ss;
        ss << "Message{type=";
        switch (type_) {
            case MessageType::Register: ss << "Register"; break;
            case MessageType::Message: ss << "Message"; break;
            case MessageType::Reply: ss << "Reply"; break;
            case MessageType::Ack: ss << "Ack"; break;
        }
        ss << ", flags=" << (int)flags_
           << ", corr_id=" << correlation_id_
           << ", sender=" << sender_
           << ", dest=" << destination_
           << ", payload_size=" << payload_.size()
           << "}";
        return ss.str();
    }
    
private:
    MessageType type_ = MessageType::Message;
    uint8_t flags_ = 0;
    uint64_t correlation_id_ = 0;
    std::string sender_;
    std::string destination_;
    std::vector<uint8_t> payload_;
};

} // namespace test

using namespace test;

// ==================== Вспомогательные функции ====================

std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// ==================== Клиентский класс ====================

class TestClient {
public:
    TestClient(const std::string& name, const std::string& server_addr = "tcp://localhost:5555")
        : name_(name)
        , context_(1)
        , socket_(context_, zmq::socket_type::dealer)
    {
        // Устанавливаем identity для DEALER сокета
        socket_.set(zmq::sockopt::routing_id, name);
        socket_.connect(server_addr);
        std::cout << "[" << get_current_time() << "] [" << name_ << "] Connected to " << server_addr << std::endl;
    }
    
    ~TestClient() {
        socket_.close();
    }
    
    void Register() {
        Message msg(MessageType::Register, 0, 0, name_, "", {});
        SendMessage(msg);
        std::cout << "[" << get_current_time() << "] [" << name_ << "] Registered" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void SendMessage(const std::string& to, const std::string& payload, 
                     uint64_t corr_id, bool needs_reply, bool needs_ack = false) {
        uint8_t flags = 0;
        if (needs_reply) flags |= FlagNeedsReply;
        if (needs_ack) flags |= FlagNeedsAck;
        
        std::vector<uint8_t> payload_bytes(payload.begin(), payload.end());
        Message msg(MessageType::Message, flags, corr_id, name_, to, payload_bytes);
        SendMessage(msg);
        
        std::cout << "[" << get_current_time() << "] [" << name_ << "] Sent to " << to 
                  << ": \"" << payload << "\" (corr_id=" << corr_id 
                  << ", needs_reply=" << needs_reply 
                  << ", needs_ack=" << needs_ack << ")" << std::endl;
    }
    
    void SendReply(const std::string& to, const std::string& payload, 
                   uint64_t corr_id, bool needs_ack = false) {
        uint8_t flags = needs_ack ? FlagNeedsAck : 0;
        std::vector<uint8_t> payload_bytes(payload.begin(), payload.end());
        Message msg(MessageType::Reply, flags, corr_id, name_, to, payload_bytes);
        SendMessage(msg);
        
        std::cout << "[" << get_current_time() << "] [" << name_ << "] Sent reply to " << to 
                  << ": \"" << payload << "\" (corr_id=" << corr_id 
                  << ", needs_ack=" << needs_ack << ")" << std::endl;
    }
    
    void SendAck(uint64_t corr_id) {
        Message msg(MessageType::Ack, 0, corr_id, name_, "", {});
        SendMessage(msg);
        std::cout << "[" << get_current_time() << "] [" << name_ << "] Sent ACK for corr_id=" << corr_id << std::endl;
    }
    
    bool Receive(Message& received, int timeout_ms = 5000) {
    zmq::pollitem_t items[] = {{socket_, 0, ZMQ_POLLIN, 0}};
    int poll_result = zmq::poll(items, 1, std::chrono::milliseconds(timeout_ms));
    
    if (poll_result > 0 && (items[0].revents & ZMQ_POLLIN)) {
        // Читаем все фреймы
        std::vector<zmq::message_t> frames;
        bool more = true;
        while (more) {
            zmq::message_t frame;
            auto recv_result = socket_.recv(frame, zmq::recv_flags::none);
            if (!recv_result) break;
            more = frame.more();
            frames.push_back(std::move(frame));
        }
        
        // Ищем фрейм с валидным сообщением (первый байт = 1 - версия протокола)
        for (const auto& frame : frames) {
            if (frame.size() > 0) {
                const uint8_t* data = static_cast<const uint8_t*>(frame.data());
                // Проверяем, что это не identity (слишком короткий или не начинается с 1)
                if (frame.size() >= 3 && data[0] == 1) {
                    std::vector<uint8_t> msg_data(data, data + frame.size());
                    received = Message::Deserialize(msg_data);
                    return true;
                }
            }
        }
        
        // Если не нашли, выводим отладочную информацию
        std::cout << "[" << get_current_time() << "] [" << name_ 
                  << "] Received " << frames.size() << " frames, none valid" << std::endl;
        for (size_t i = 0; i < frames.size(); ++i) {
            std::cout << "  Frame " << i << ": size=" << frames[i].size();
            if (frames[i].size() > 0) {
                const uint8_t* d = static_cast<const uint8_t*>(frames[i].data());
                std::cout << " first_byte=" << (int)d[0];
            }
            std::cout << std::endl;
        }
        return false;
    }
    return false;
}
    
    const std::string& GetName() const { return name_; }
    
private:
    void SendMessage(const Message& msg) {
        auto serialized = msg.Serialize();
        socket_.send(zmq::buffer(serialized), zmq::send_flags::none);
    }
    
    std::string name_;
    zmq::context_t context_;
    zmq::socket_t socket_;
};

// ==================== СЦЕНАРИЙ 1: Штатная работа с ACK ====================

void scenario1_alice() {
    TestClient alice("alice");
    alice.Register();
    
    // Отправляем сообщение Bob с NEEDS_REPLY и NEEDS_ACK
    alice.SendMessage("bob", "Hello, Bob! (with ACK)", 12345, true, true);
    
    Message reply;
    if (alice.Receive(reply, 5000)) {
        std::cout << "[" << get_current_time() << "] [alice] ✓ Received reply: \"" 
                  << std::string(reply.GetPayload().begin(), reply.GetPayload().end())
                  << "\" from " << reply.GetSender() << std::endl;
        
        if (reply.NeedsAck()) {
            alice.SendAck(reply.GetCorrelationId());
        }
    } else {
        std::cout << "[" << get_current_time() << "] [alice] ✗ No reply received" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

void scenario1_bob() {
    TestClient bob("bob");
    bob.Register();
    std::cout << "[" << get_current_time() << "] [bob] Waiting for messages..." << std::endl;
    
    Message msg;
    if (bob.Receive(msg, 10000)) {
        std::string payload(msg.GetPayload().begin(), msg.GetPayload().end());
        std::cout << "[" << get_current_time() << "] [bob] Received: \"" << payload 
                  << "\" from " << msg.GetSender() << " (corr_id=" << msg.GetCorrelationId() 
                  << ", needs_ack=" << (msg.NeedsAck() ? "true" : "false") << ")" << std::endl;
        
        if (msg.NeedsAck()) {
            bob.SendAck(msg.GetCorrelationId());
        }
        
        if (msg.NeedsReply()) {
            bob.SendReply(msg.GetSender(), "Hello, " + msg.GetSender() + "!", 
                          msg.GetCorrelationId(), true);
            std::cout << "[" << get_current_time() << "] [bob] Reply sent with ACK requested" << std::endl;
        }
    } else {
        std::cout << "[" << get_current_time() << "] [bob] ✗ No message received" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ==================== СЦЕНАРИЙ 2: Получатель офлайн с ACK ====================

void scenario2_alice() {
    TestClient alice("alice");
    alice.Register();
    
    alice.SendMessage("bob", "Hello, Bob! (delayed delivery with ACK)", 12345, true, true);
    std::cout << "[" << get_current_time() << "] [alice] Waiting for reply (Bob will come online later)..." << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Timeout set to 30 seconds" << std::endl;
    
    Message reply;
    if (alice.Receive(reply, 30000)) {
        std::cout << "[" << get_current_time() << "] [alice] ✓ Received reply: \"" 
                  << std::string(reply.GetPayload().begin(), reply.GetPayload().end())
                  << "\" from " << reply.GetSender() << std::endl;
        
        if (reply.NeedsAck()) {
            alice.SendAck(reply.GetCorrelationId());
        }
    } else {
        std::cout << "[" << get_current_time() << "] [alice] ✗ Timeout waiting for reply" << std::endl;
    }
}

void scenario2_bob() {
    std::cout << "[" << get_current_time() << "] [bob] Waiting 2 seconds before connecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    TestClient bob("bob");
    bob.Register();
    std::cout << "[" << get_current_time() << "] [bob] Checking for pending messages..." << std::endl;
    
    Message msg;
    if (bob.Receive(msg, 10000)) {
        std::string payload(msg.GetPayload().begin(), msg.GetPayload().end());
        std::cout << "[" << get_current_time() << "] [bob] Received pending: \"" << payload 
                  << "\" from " << msg.GetSender() << " (corr_id=" << msg.GetCorrelationId() 
                  << ", needs_ack=" << (msg.NeedsAck() ? "true" : "false") << ")" << std::endl;
        
        if (msg.NeedsAck()) {
            bob.SendAck(msg.GetCorrelationId());
        }
        
        if (msg.NeedsReply()) {
            bob.SendReply(msg.GetSender(), "Hello, " + msg.GetSender() + "! (delayed reply)", 
                          msg.GetCorrelationId(), true);
            std::cout << "[" << get_current_time() << "] [bob] Reply sent" << std::endl;
        }
    } else {
        std::cout << "[" << get_current_time() << "] [bob] ✗ No pending messages found" << std::endl;
    }
    
    std::cout << "[" << get_current_time() << "] [bob] Keeping connection open for 2 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

// ==================== СЦЕНАРИЙ 3: Отправитель офлайн с ACK ====================

void scenario3_alice() {
    TestClient alice("alice");
    alice.Register();
    
    alice.SendMessage("bob", "Hello, Bob! (sender will disconnect)", 12345, true, true);
    std::cout << "[" << get_current_time() << "] [alice] Message sent with NEEDS_REPLY and NEEDS_ACK, disconnecting immediately!" << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Socket will be destroyed, but reply should be saved" << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Disconnecting..." << std::endl;
    // alice уничтожится при выходе из функции
}

void scenario3_bob() {
    std::cout << "[" << get_current_time() << "] [bob] Waiting 3 seconds for Alice to disconnect..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    TestClient bob("bob");
    bob.Register();
    std::cout << "[" << get_current_time() << "] [bob] Waiting for message from Alice..." << std::endl;
    
    Message msg;
    if (bob.Receive(msg, 10000)) {
        std::string payload(msg.GetPayload().begin(), msg.GetPayload().end());
        std::cout << "[" << get_current_time() << "] [bob] Received: \"" << payload 
                  << "\" from " << msg.GetSender() << " (corr_id=" << msg.GetCorrelationId() 
                  << ", needs_ack=" << (msg.NeedsAck() ? "true" : "false") << ")" << std::endl;
        
        if (msg.NeedsAck()) {
            bob.SendAck(msg.GetCorrelationId());
        }
        
        if (msg.NeedsReply()) {
            std::cout << "[" << get_current_time() << "] [bob] Message requires reply, sending response with ACK..." << std::endl;
            bob.SendReply(msg.GetSender(), "Hello, Alice! Your reply is here!", 
                          msg.GetCorrelationId(), true);
            std::cout << "[" << get_current_time() << "] [bob] ✓ Reply sent, but Alice is offline!" << std::endl;
            std::cout << "[" << get_current_time() << "] [bob] Reply should be stored in database and delivered when Alice reconnects" << std::endl;
        }
    } else {
        std::cout << "[" << get_current_time() << "] [bob] ✗ No message received" << std::endl;
    }
    
    std::cout << "[" << get_current_time() << "] [bob] Keeping connection open for 2 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void scenario3_alice_reconnect() {
    std::cout << "[" << get_current_time() << "] [alice] Waiting 5 seconds before reconnecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    TestClient alice("alice");
    alice.Register();
    std::cout << "[" << get_current_time() << "] [alice] Reconnected! Checking for pending replies..." << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Timeout set to 15 seconds" << std::endl;
    
    Message reply;
    if (alice.Receive(reply, 15000)) {
        std::cout << "[" << get_current_time() << "] [alice] ✓ SUCCESS: Received pending reply! \"" 
                  << std::string(reply.GetPayload().begin(), reply.GetPayload().end())
                  << "\" from " << reply.GetSender() << std::endl;
        std::cout << "[" << get_current_time() << "] [alice] Correlation ID: " << reply.GetCorrelationId() << std::endl;
        
        if (reply.NeedsAck()) {
            alice.SendAck(reply.GetCorrelationId());
        }
    } else {
        std::cout << "[" << get_current_time() << "] [alice] ✗ FAILED: No pending reply received!" << std::endl;
        std::cout << "[" << get_current_time() << "] [alice] This indicates correlation delivery is not working" << std::endl;
    }
    
    std::cout << "\n[" << get_current_time() << "] [alice] Keeping connection for 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

// ==================== СЦЕНАРИЙ 4: Полная отказоустойчивость с ACK ====================

void scenario4_alice() {
    TestClient alice("alice");
    alice.Register();
    
    alice.SendMessage("bob", "Critical message that must survive!", 54321, true, true);
    std::cout << "[" << get_current_time() << "] [alice] Message sent with ACK, both Alice and Bob will disconnect" << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Disconnecting..." << std::endl;
}

void scenario4_bob() {
    std::cout << "[" << get_current_time() << "] [bob] Waiting 3 seconds before connecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    TestClient bob("bob");
    bob.Register();
    std::cout << "[" << get_current_time() << "] [bob] Checking for pending messages..." << std::endl;
    
    Message msg;
    if (bob.Receive(msg, 10000)) {
        std::string payload(msg.GetPayload().begin(), msg.GetPayload().end());
        std::cout << "[" << get_current_time() << "] [bob] Received: \"" << payload 
                  << "\" from " << msg.GetSender() << " (corr_id=" << msg.GetCorrelationId() 
                  << ", needs_ack=" << (msg.NeedsAck() ? "true" : "false") << ")" << std::endl;
        
        if (msg.NeedsAck()) {
            bob.SendAck(msg.GetCorrelationId());
        }
        
        if (msg.NeedsReply()) {
            bob.SendReply(msg.GetSender(), "Reply from Bob after both were offline!", 
                          msg.GetCorrelationId(), true);
            std::cout << "[" << get_current_time() << "] [bob] Reply sent with ACK, but Alice is still offline" << std::endl;
        }
    } else {
        std::cout << "[" << get_current_time() << "] [bob] ✗ No pending messages found" << std::endl;
    }
    
    std::cout << "[" << get_current_time() << "] [bob] Keeping connection open for 2 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void scenario4_alice_reconnect() {
    std::cout << "[" << get_current_time() << "] [alice] Waiting 8 seconds before reconnecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(8));
    
    TestClient alice("alice");
    alice.Register();
    std::cout << "[" << get_current_time() << "] [alice] Reconnected! Checking for pending replies..." << std::endl;
    
    Message reply;
    if (alice.Receive(reply, 15000)) {
        std::cout << "[" << get_current_time() << "] [alice] ✓ SUCCESS: Received pending reply! \"" 
                  << std::string(reply.GetPayload().begin(), reply.GetPayload().end())
                  << "\" from " << reply.GetSender() << std::endl;
        std::cout << "[" << get_current_time() << "] [alice] Correlation ID: " << reply.GetCorrelationId() << std::endl;
        
        if (reply.NeedsAck()) {
            alice.SendAck(reply.GetCorrelationId());
        }
    } else {
        std::cout << "[" << get_current_time() << "] [alice] ✗ No pending reply received" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

// ==================== Парсинг аргументов и main ====================

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " --scenario N --role ROLE\n\n"
              << "SCENARIOS (with ACK support):\n"
              << "  1 - Normal work (both online) with ACK\n"
              << "  2 - Receiver offline (delayed delivery) with ACK\n"
              << "  3 - Sender offline, reconnects later to get reply with ACK\n"
              << "  4 - Both offline (full fault tolerance) with ACK\n\n"
              << "ROLES:\n"
              << "  Scenario 1: --role alice  or  --role bob\n"
              << "  Scenario 2: --role alice  or  --role bob\n"
              << "  Scenario 3: --role alice, then --role bob, then --role alice_reconnect\n"
              << "  Scenario 4: --role alice, then --role bob, then --role alice_reconnect\n\n"
              << "EXAMPLES:\n"
              << "  # Scenario 1 (terminal 1): " << prog << " --scenario 1 --role bob\n"
              << "  # Scenario 1 (terminal 2): " << prog << " --scenario 1 --role alice\n\n"
              << "  # Scenario 2 (terminal 1): " << prog << " --scenario 2 --role alice\n"
              << "  # Scenario 2 (terminal 2): " << prog << " --scenario 2 --role bob\n\n"
              << "  # Scenario 3 (terminal 1): " << prog << " --scenario 3 --role alice\n"
              << "  # Scenario 3 (terminal 2): " << prog << " --scenario 3 --role bob\n"
              << "  # Scenario 3 (terminal 1): " << prog << " --scenario 3 --role alice_reconnect\n\n"
              << "  # Scenario 4 (terminal 1): " << prog << " --scenario 4 --role alice\n"
              << "  # Scenario 4 (terminal 2): " << prog << " --scenario 4 --role bob\n"
              << "  # Scenario 4 (terminal 1): " << prog << " --scenario 4 --role alice_reconnect\n";
}

int main(int argc, char* argv[]) {
    int scenario = 0;
    std::string role;
    
    static struct option long_options[] = {
        {"role", required_argument, 0, 'r'},
        {"scenario", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "r:s:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'r': role = optarg; break;
            case 's': scenario = std::stoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    
    if (scenario == 0 || role.empty()) {
        std::cerr << "Error: --scenario and --role are required" << std::endl;
        print_usage(argv[0]);
        return 1;
    }
    
    try {
        switch (scenario) {
            case 1:
                if (role == "alice") scenario1_alice();
                else if (role == "bob") scenario1_bob();
                else std::cerr << "Error: role must be alice or bob for scenario 1" << std::endl;
                break;
            case 2:
                if (role == "alice") scenario2_alice();
                else if (role == "bob") scenario2_bob();
                else std::cerr << "Error: role must be alice or bob for scenario 2" << std::endl;
                break;
            case 3:
                if (role == "alice") scenario3_alice();
                else if (role == "bob") scenario3_bob();
                else if (role == "alice_reconnect") scenario3_alice_reconnect();
                else std::cerr << "Error: role must be alice, bob, or alice_reconnect for scenario 3" << std::endl;
                break;
            case 4:
                if (role == "alice") scenario4_alice();
                else if (role == "bob") scenario4_bob();
                else if (role == "alice_reconnect") scenario4_alice_reconnect();
                else std::cerr << "Error: role must be alice, bob, or alice_reconnect for scenario 4" << std::endl;
                break;
            default:
                std::cerr << "Error: Invalid scenario " << scenario << std::endl;
                return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}