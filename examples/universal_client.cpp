#include <zmq.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <iomanip>
#include <unordered_map>
#include <functional>
#include <random>
#include <mutex>
#include <cstring>
#include <arpa/inet.h>

// Protocol constants (must match broker)
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t HEADER_SIZE = 15;

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

// Serialization helpers
uint64_t HostToNetwork64(uint64_t host) {
    return __builtin_bswap64(host);
}

uint64_t NetworkToHost64(uint64_t net) {
    return __builtin_bswap64(net);
}

uint16_t HostToNetwork16(uint16_t host) {
    return htons(host);
}

uint16_t NetworkToHost16(uint16_t net) {
    return ntohs(net);
}

class Message {
public:
    MessageType type = MessageType::Message;
    uint8_t flags = FlagNone;
    uint64_t correlation_id = 0;
    std::string sender;
    std::string destination;
    std::vector<uint8_t> payload;
    
    std::vector<uint8_t> Serialize() const {
        if (sender.size() > 255 || destination.size() > 255) {
            throw std::runtime_error("Name too long");
        }
        
        size_t total_size = HEADER_SIZE + sender.size() + destination.size() + payload.size();
        std::vector<uint8_t> buffer;
        buffer.reserve(total_size);
        
        buffer.push_back(PROTOCOL_VERSION);
        buffer.push_back(static_cast<uint8_t>(type));
        buffer.push_back(flags);
        
        uint64_t net_corr = HostToNetwork64(correlation_id);
        const uint8_t* corr_bytes = reinterpret_cast<const uint8_t*>(&net_corr);
        buffer.insert(buffer.end(), corr_bytes, corr_bytes + 8);
        
        buffer.push_back(static_cast<uint8_t>(sender.size()));
        buffer.push_back(static_cast<uint8_t>(destination.size()));
        
        uint16_t net_payload_len = HostToNetwork16(static_cast<uint16_t>(payload.size()));
        const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&net_payload_len);
        buffer.insert(buffer.end(), len_bytes, len_bytes + 2);
        
        buffer.insert(buffer.end(), sender.begin(), sender.end());
        buffer.insert(buffer.end(), destination.begin(), destination.end());
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        
        return buffer;
    }
    
    static Message Deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < HEADER_SIZE) {
            throw std::runtime_error("Message too short");
        }
        
        size_t pos = 0;
        uint8_t version = data[pos++];
        if (version != PROTOCOL_VERSION) {
            throw std::runtime_error("Wrong protocol version");
        }
        
        Message msg;
        msg.type = static_cast<MessageType>(data[pos++]);
        msg.flags = data[pos++];
        
        uint64_t net_corr;
        std::memcpy(&net_corr, &data[pos], 8);
        msg.correlation_id = NetworkToHost64(net_corr);
        pos += 8;
        
        uint8_t sender_len = data[pos++];
        uint8_t dest_len = data[pos++];
        
        uint16_t net_payload_len;
        std::memcpy(&net_payload_len, &data[pos], 2);
        uint16_t payload_len = NetworkToHost16(net_payload_len);
        pos += 2;
        
        msg.sender.assign(reinterpret_cast<const char*>(&data[pos]), sender_len);
        pos += sender_len;
        
        msg.destination.assign(reinterpret_cast<const char*>(&data[pos]), dest_len);
        pos += dest_len;
        
        msg.payload.assign(data.begin() + pos, data.begin() + pos + payload_len);
        
        return msg;
    }
    
    std::string PayloadToString() const {
        return std::string(payload.begin(), payload.end());
    }
};

class UniversalClient {
public:
    UniversalClient(const std::string& broker_address, const std::string& client_name)
        : context_(1)
        , socket_(context_, zmq::socket_type::dealer)
        , client_name_(client_name)
        , running_(true)
    {
        // Set identity
        socket_.set(zmq::sockopt::routing_id, client_name);
        
        // Connect to broker
        socket_.connect(broker_address);
        std::cout << "Connected to broker at " << broker_address << std::endl;
        
        // Generate unique correlation IDs
        std::random_device rd;
        rng_.seed(rd());
    }
    
    ~UniversalClient() {
        Stop();
    }
    
    void Run() {
        // Register first
        Register();
        
        // Start receiver thread
        receiver_thread_ = std::thread(&UniversalClient::ReceiverLoop, this);
        
        // Main loop - process commands
        PrintHelp();
        std::string line;
        while (running_) {
            std::cout << "\n[" << client_name_ << "] > " << std::flush;
            if (!std::getline(std::cin, line)) {
                break;
            }
            
            if (line.empty()) continue;
            
            ProcessCommand(line);
        }
        
        // Unregister before exit
        Unregister();
    }
    
    void Stop() {
        running_ = false;
        if (receiver_thread_.joinable()) {
            receiver_thread_.join();
        }
    }

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
    std::string client_name_;
    std::atomic<bool> running_;
    std::thread receiver_thread_;
    std::mt19937_64 rng_;
    
    std::unordered_map<uint64_t, std::string> pending_requests_;
    std::mutex pending_mutex_;
    
    void PrintHelp() {
        std::cout << "\n=== Universal Client Commands ===" << std::endl;
        std::cout << "  send <dest> <message>     - Send message to destination" << std::endl;
        std::cout << "  send_ack <dest> <msg>     - Send message requiring ACK" << std::endl;
        std::cout << "  request <dest> <message>  - Send request and wait for reply" << std::endl;
        std::cout << "  status                    - Show client status" << std::endl;
        std::cout << "  help                      - Show this help" << std::endl;
        std::cout << "  quit                      - Exit client" << std::endl;
        std::cout << "==================================" << std::endl;
    }
    
    void ProcessCommand(const std::string& line) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        
        if (cmd == "send") {
            std::string dest, message;
            iss >> dest;
            std::getline(iss, message);
            if (!dest.empty() && !message.empty()) {
                message = message.substr(1); // Remove leading space
                SendMessage(dest, message, false, false);
            } else {
                std::cout << "Usage: send <destination> <message>" << std::endl;
            }
        }
        else if (cmd == "send_ack") {
            std::string dest, message;
            iss >> dest;
            std::getline(iss, message);
            if (!dest.empty() && !message.empty()) {
                message = message.substr(1);
                SendMessage(dest, message, false, true);
            } else {
                std::cout << "Usage: send_ack <destination> <message>" << std::endl;
            }
        }
        else if (cmd == "request") {
            std::string dest, message;
            iss >> dest;
            std::getline(iss, message);
            if (!dest.empty() && !message.empty()) {
                message = message.substr(1);
                SendMessage(dest, message, true, false);
            } else {
                std::cout << "Usage: request <destination> <message>" << std::endl;
            }
        }
        else if (cmd == "status") {
            ShowStatus();
        }
        else if (cmd == "help") {
            PrintHelp();
        }
        else if (cmd == "quit" || cmd == "exit") {
            running_ = false;
        }
        else {
            std::cout << "Unknown command: " << cmd << std::endl;
        }
    }
    
    void Register() {
        Message msg;
        msg.type = MessageType::Register;
        msg.sender = client_name_;
        msg.destination = "";
        msg.correlation_id = 0;
        
        SendRawMessage(msg);
        std::cout << "Registered as '" << client_name_ << "'" << std::endl;
    }
    
    void Unregister() {
        Message msg;
        msg.type = MessageType::Unregister;
        msg.sender = client_name_;
        msg.destination = "";
        msg.correlation_id = 0;
        
        SendRawMessage(msg);
        std::cout << "Unregistered" << std::endl;
    }
    
    uint64_t GenerateCorrelationId() {
        return rng_();
    }
    
    void SendMessage(const std::string& dest, const std::string& text, 
                     bool needs_reply, bool needs_ack) {
        Message msg;
        msg.type = MessageType::Message;
        msg.sender = client_name_;
        msg.destination = dest;
        msg.correlation_id = GenerateCorrelationId();
        msg.payload = std::vector<uint8_t>(text.begin(), text.end());
        
        if (needs_reply) {
            msg.flags |= FlagNeedsReply;
        }
        if (needs_ack) {
            msg.flags |= FlagNeedsAck;
        }
        
        if (needs_reply) {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_[msg.correlation_id] = dest;
        }
        
        SendRawMessage(msg);
        
        std::cout << "Sent to " << dest << " [id=" << msg.correlation_id;
        if (needs_reply) std::cout << ", needs_reply";
        if (needs_ack) std::cout << ", needs_ack";
        std::cout << "]: " << text << std::endl;
    }
    
    void SendReply(const Message& original, const std::string& text) {
        Message reply;
        reply.type = MessageType::Reply;
        reply.sender = client_name_;
        reply.destination = original.sender;
        reply.correlation_id = original.correlation_id;
        reply.payload = std::vector<uint8_t>(text.begin(), text.end());
        
        SendRawMessage(reply);
        std::cout << "Sent reply to " << original.sender 
                  << " [id=" << reply.correlation_id << "]: " << text << std::endl;
    }
    
    void SendAck(uint64_t correlation_id, const std::string& original_sender) {
        Message ack;
        ack.type = MessageType::Ack;
        ack.sender = client_name_;
        ack.destination = original_sender;
        ack.correlation_id = correlation_id;
        
        SendRawMessage(ack);
        std::cout << "Sent ACK for id=" << correlation_id << std::endl;
    }
    
    void SendRawMessage(const Message& msg) {
        auto data = msg.Serialize();
        zmq::message_t zmq_msg(data.data(), data.size());
        socket_.send(zmq_msg, zmq::send_flags::none);
    }
    
    void ReceiverLoop() {
        while (running_) {
            zmq::message_t zmq_msg;
            auto result = socket_.recv(zmq_msg, zmq::recv_flags::dontwait);
            
            if (result) {
                try {
                    std::vector<uint8_t> data(
                        static_cast<uint8_t*>(zmq_msg.data()),
                        static_cast<uint8_t*>(zmq_msg.data()) + zmq_msg.size()
                    );
                    
                    Message msg = Message::Deserialize(data);
                    HandleMessage(msg);
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing message: " << e.what() << std::endl;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void HandleMessage(const Message& msg) {
        std::string payload_str = msg.PayloadToString();
        
        switch (msg.type) {
            case MessageType::Message:
                std::cout << "\n[RECEIVED] Message from " << msg.sender 
                          << " [id=" << msg.correlation_id << "]: " << payload_str << std::endl;
                
                if (msg.flags & FlagNeedsAck) {
                    SendAck(msg.correlation_id, msg.sender);
                }
                
                // Auto-reply for demonstration
                if (msg.flags & FlagNeedsReply) {
                    std::string reply_text = "Auto-reply to: " + payload_str;
                    SendReply(msg, reply_text);
                }
                break;
                
            case MessageType::Reply:
                std::cout << "\n[REPLY] From " << msg.sender 
                          << " [id=" << msg.correlation_id << "]: " << payload_str << std::endl;
                
                {
                    std::lock_guard<std::mutex> lock(pending_mutex_);
                    pending_requests_.erase(msg.correlation_id);
                }
                break;
                
            case MessageType::Ack:
                std::cout << "\n[ACK] Message delivered to " << msg.sender 
                          << " [id=" << msg.correlation_id << "]" << std::endl;
                break;
                
            default:
                break;
        }
        
        std::cout << "[" << client_name_ << "] > " << std::flush;
    }
    
    void ShowStatus() {
        std::cout << "\n=== Client Status ===" << std::endl;
        std::cout << "Name: " << client_name_ << std::endl;
        std::cout << "Connected: Yes" << std::endl;
        
        std::lock_guard<std::mutex> lock(pending_mutex_);
        std::cout << "Pending requests: " << pending_requests_.size() << std::endl;
        for (const auto& [id, dest] : pending_requests_) {
            std::cout << "  - id=" << id << " -> " << dest << std::endl;
        }
        std::cout << "=====================" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <broker_address> <client_name>" << std::endl;
        std::cerr << "Example: " << argv[0] << " tcp://localhost:5555 client1" << std::endl;
        return 1;
    }
    
    std::string broker_address = argv[1];
    std::string client_name = argv[2];
    
    try {
        UniversalClient client(broker_address, client_name);
        client.Run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}