#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <vector>

struct SimpleMessage {
    uint8_t version = 1;
    uint8_t type = 2;
    uint8_t flags = 0;
    uint64_t correlation_id = 0;
    std::string sender;
    std::string destination;
    std::string payload;
    
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;
        buffer.push_back(version);
        buffer.push_back(type);
        buffer.push_back(flags);
        
        uint64_t net_corr = __builtin_bswap64(correlation_id);
        const uint8_t* corr_bytes = reinterpret_cast<const uint8_t*>(&net_corr);
        buffer.insert(buffer.end(), corr_bytes, corr_bytes + 8);
        
        buffer.push_back(static_cast<uint8_t>(sender.size()));
        buffer.push_back(static_cast<uint8_t>(destination.size()));
        
        uint16_t net_len = htons(static_cast<uint16_t>(payload.size()));
        const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&net_len);
        buffer.insert(buffer.end(), len_bytes, len_bytes + 2);
        
        buffer.insert(buffer.end(), sender.begin(), sender.end());
        buffer.insert(buffer.end(), destination.begin(), destination.end());
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        
        return buffer;
    }
    
    void deserialize(const std::vector<uint8_t>& data) {
        size_t pos = 0;
        version = data[pos++];
        type = data[pos++];
        flags = data[pos++];
        
        uint64_t net_corr;
        std::memcpy(&net_corr, &data[pos], 8);
        correlation_id = __builtin_bswap64(net_corr);
        pos += 8;
        
        uint8_t sender_len = data[pos++];
        uint8_t dest_len = data[pos++];
        
        uint16_t net_payload_len;
        std::memcpy(&net_payload_len, &data[pos], 2);
        uint16_t payload_len = ntohs(net_payload_len);
        pos += 2;
        
        sender.assign(reinterpret_cast<const char*>(&data[pos]), sender_len);
        pos += sender_len;
        
        destination.assign(reinterpret_cast<const char*>(&data[pos]), dest_len);
        pos += dest_len;
        
        payload.assign(reinterpret_cast<const char*>(&data[pos]), payload_len);
    }
};

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    
    // Устанавливаем identity, чтобы сервер мог отправлять нам сообщения
    socket.set(zmq::sockopt::routing_id, "alice");
    
    socket.connect("tcp://localhost:5555");
    std::cout << "Alice: Connected to broker" << std::endl;
    
    // Регистрация как alice
    SimpleMessage register_msg;
    register_msg.type = 1;  // Register
    register_msg.sender = "alice";
    register_msg.destination = "";
    
    auto data = register_msg.serialize();
    socket.send(zmq::buffer(data), zmq::send_flags::none);
    std::cout << "Alice: Registered as alice" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Сообщение для bob с запросом ответа
    SimpleMessage msg;
    msg.type = 2;  // Message
    msg.flags = 1; // NeedsReply
    msg.correlation_id = 12345;
    msg.sender = "alice";
    msg.destination = "bob";
    msg.payload = "Hello, Bob!";
    
    data = msg.serialize();
    socket.send(zmq::buffer(data), zmq::send_flags::none);
    std::cout << "Alice: Sent message to bob: " << msg.payload << std::endl;
    
    // Ожидание ответа
    zmq::pollitem_t items[] = {
        {socket, 0, ZMQ_POLLIN, 0}
    };
    
    zmq::poll(items, 1, std::chrono::milliseconds(5000));
    
    if (items[0].revents & ZMQ_POLLIN) {
        zmq::message_t reply;
        auto result = socket.recv(reply, zmq::recv_flags::none);
        if (result) {
            std::vector<uint8_t> reply_data(
                static_cast<uint8_t*>(reply.data()),
                static_cast<uint8_t*>(reply.data()) + reply.size()
            );
            
            SimpleMessage response;
            response.deserialize(reply_data);
            
            std::cout << "Alice: Received reply from " << response.sender 
                      << ": " << response.payload << std::endl;
        }
    } else {
        std::cout << "Alice: No reply received within 5 seconds" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    return 0;
}