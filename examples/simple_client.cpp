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
    
    socket.set(zmq::sockopt::routing_id, "test_client");
    socket.connect("tcp://localhost:5555");
    std::cout << "Connected to broker" << std::endl;
    
    // ========== Регистрационное сообщение ==========
    std::vector<uint8_t> reg_msg;
    reg_msg.push_back(1);  // version
    reg_msg.push_back(1);  // type = Register
    reg_msg.push_back(0);  // flags
    
    for (int i = 0; i < 8; i++) reg_msg.push_back(0);
    reg_msg.push_back(4);  // sender_len = 4 ("test")
    reg_msg.push_back(0);  // dest_len = 0
    reg_msg.push_back(0);  // payload_len = 0
    reg_msg.push_back(0);
    reg_msg.push_back('t');
    reg_msg.push_back('e');
    reg_msg.push_back('s');
    reg_msg.push_back('t');
    
    socket.send(zmq::buffer(reg_msg), zmq::send_flags::none);
    std::cout << "Sent registration" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // ========== Сообщение ==========
    std::vector<uint8_t> msg;
    msg.push_back(1);  // version
    msg.push_back(2);  // type = Message
    msg.push_back(1);  // flags = NeedsReply
    
    uint64_t corr = 12345;
    uint64_t net_corr = __builtin_bswap64(corr);
    for (int i = 0; i < 8; i++) {
        msg.push_back(((const uint8_t*)&net_corr)[i]);
    }
    
    msg.push_back(4);  // sender_len = 4
    msg.push_back(3);  // dest_len = 3 ("bob")
    
    uint16_t payload_len = 5;
    uint16_t net_len = htons(payload_len);
    msg.push_back(((const uint8_t*)&net_len)[0]);
    msg.push_back(((const uint8_t*)&net_len)[1]);
    
    msg.push_back('t');
    msg.push_back('e');
    msg.push_back('s');
    msg.push_back('t');
    msg.push_back('b');
    msg.push_back('o');
    msg.push_back('b');
    msg.push_back('H');
    msg.push_back('e');
    msg.push_back('l');
    msg.push_back('l');
    msg.push_back('o');
    
    std::cout << "Sending message to bob..." << std::endl;
    socket.send(zmq::buffer(msg), zmq::send_flags::none);
    std::cout << "Sent message, waiting for reply..." << std::endl;
    
    // ========== Ожидание ответа — читаем ДВА фрейма ==========
    zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
    int poll_result = zmq::poll(items, 1, std::chrono::milliseconds(30000));
    
    if (poll_result > 0 && (items[0].revents & ZMQ_POLLIN)) {
        // Первый фрейм — пустой разделитель
        zmq::message_t delimiter;
        auto recv_result = socket.recv(delimiter, zmq::recv_flags::none);
        if (recv_result) {
            std::cout << "Received delimiter, size: " << delimiter.size() << " bytes" << std::endl;
        }
        
        // Второй фрейм — данные ответа
        zmq::message_t reply;
        recv_result = socket.recv(reply, zmq::recv_flags::none);
        
        if (recv_result) {
            std::vector<uint8_t> reply_data(
                static_cast<uint8_t*>(reply.data()),
                static_cast<uint8_t*>(reply.data()) + reply.size()
            );
            
            std::cout << "Received reply, size: " << reply_data.size() << " bytes" << std::endl;
            
            // Выводим первые байты
            std::string hex;
            for (size_t i = 0; i < std::min(reply_data.size(), size_t(32)); i++) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02x ", reply_data[i]);
                hex += buf;
            }
            std::cout << "Hex: " << hex << std::endl;
            
            if (reply_data.size() > 15) {
                size_t pos = 0;
                pos++; // version
                uint8_t type = reply_data[pos++];
                uint8_t flags = reply_data[pos++];
                pos += 8; // correlation_id
                uint8_t sender_len = reply_data[pos++];
                uint8_t dest_len = reply_data[pos++];
                pos += 2; // payload_len
                
                std::string sender(reinterpret_cast<const char*>(&reply_data[pos]), sender_len);
                pos += sender_len;
                pos += dest_len;
                std::string payload(reinterpret_cast<const char*>(&reply_data[pos]), reply_data.size() - pos);
                
                std::cout << "Reply from " << sender << ": " << payload << std::endl;
            }
        } else {
            std::cout << "Failed to receive reply data" << std::endl;
        }
    } else {
        std::cout << "No reply received" << std::endl;
    }
    
    return 0;
}