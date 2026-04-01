#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <vector>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    
    // Устанавливаем identity
    socket.set(zmq::sockopt::routing_id, "bob_client");
    
    socket.connect("tcp://localhost:5555");
    std::cout << "Bob: Connected to broker" << std::endl;
    
    // Регистрация как bob
    std::vector<uint8_t> reg_msg;
    reg_msg.push_back(1);  // version
    reg_msg.push_back(1);  // type = Register
    reg_msg.push_back(0);  // flags
    
    // correlation_id = 0
    for (int i = 0; i < 8; i++) reg_msg.push_back(0);
    
    reg_msg.push_back(3);  // sender_len = 3 ("bob")
    reg_msg.push_back(0);  // dest_len = 0
    reg_msg.push_back(0);  // payload_len = 0
    reg_msg.push_back(0);
    
    // sender = "bob"
    reg_msg.push_back('b');
    reg_msg.push_back('o');
    reg_msg.push_back('b');
    
    auto result = socket.send(zmq::buffer(reg_msg), zmq::send_flags::none);
    if (!result) {
        std::cerr << "Bob: Failed to send registration" << std::endl;
        return 1;
    }
    std::cout << "Bob: Registered as bob" << std::endl;
    
    // Ожидание сообщений
    std::cout << "Bob: Waiting for messages..." << std::endl;
    
    while (true) {
        zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
        zmq::poll(items, 1, std::chrono::milliseconds(1000));
        
        if (items[0].revents & ZMQ_POLLIN) {
            // Получаем сообщение
            zmq::message_t data;
            auto recv_result = socket.recv(data, zmq::recv_flags::none);
            if (!recv_result) {
                std::cerr << "Bob: Failed to receive message" << std::endl;
                continue;
            }
            
            std::vector<uint8_t> msg_data(
                static_cast<uint8_t*>(data.data()),
                static_cast<uint8_t*>(data.data()) + data.size()
            );
            
            // Простой парсинг для вывода
            if (msg_data.size() > 15) {
                // Пропускаем заголовок
                size_t pos = 0;
                uint8_t version = msg_data[pos++];  // version (не используется)
                uint8_t type = msg_data[pos++];
                uint8_t flags = msg_data[pos++];
                
                // correlation_id (8 bytes)
                uint64_t net_corr = 0;
                std::memcpy(&net_corr, &msg_data[pos], 8);
                uint64_t correlation_id = __builtin_bswap64(net_corr);
                pos += 8;
                
                uint8_t sender_len = msg_data[pos++];
                uint8_t dest_len = msg_data[pos++];
                
                uint16_t net_payload_len = 0;
                std::memcpy(&net_payload_len, &msg_data[pos], 2);
                uint16_t payload_len = ntohs(net_payload_len);
                pos += 2;
                
                std::string sender(reinterpret_cast<const char*>(&msg_data[pos]), sender_len);
                pos += sender_len;
                std::string dest(reinterpret_cast<const char*>(&msg_data[pos]), dest_len);
                pos += dest_len;
                std::string payload(reinterpret_cast<const char*>(&msg_data[pos]), payload_len);
                
                std::cout << "Bob: Received message from " << sender 
                          << " (corr_id=" << correlation_id << "): " << payload << std::endl;
                
                // Отправляем ответ
                std::vector<uint8_t> reply;
                reply.push_back(1);  // version
                reply.push_back(3);  // type = Reply
                reply.push_back(0);  // flags
                
                // correlation_id = тот же, что в запросе
                uint64_t net_corr_reply = __builtin_bswap64(correlation_id);
                for (int i = 0; i < 8; i++) {
                    reply.push_back(((const uint8_t*)&net_corr_reply)[i]);
                }
                
                reply.push_back(3);  // sender_len = 3 ("bob")
                reply.push_back(sender_len);  // dest_len = длина отправителя
                
                // payload_len
                std::string reply_payload = "Hello, " + sender + "!";
                uint16_t reply_payload_len = reply_payload.size();
                uint16_t net_len = htons(reply_payload_len);
                reply.push_back(((const uint8_t*)&net_len)[0]);
                reply.push_back(((const uint8_t*)&net_len)[1]);
                
                // sender = "bob"
                reply.push_back('b');
                reply.push_back('o');
                reply.push_back('b');
                
                // destination = исходный отправитель
                reply.insert(reply.end(), sender.begin(), sender.end());
                
                // payload
                reply.insert(reply.end(), reply_payload.begin(), reply_payload.end());
                
                auto send_result = socket.send(zmq::buffer(reply), zmq::send_flags::none);
                if (send_result) {
                    std::cout << "Bob: Sent reply to " << sender << std::endl;
                } else {
                    std::cerr << "Bob: Failed to send reply" << std::endl;
                }
            }
        }
    }
    
    return 0;
}