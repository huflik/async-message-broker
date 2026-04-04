#include <zmq.hpp>
#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include <cstring>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "test_client");
    socket.connect("tcp://localhost:5555");
    
    // Регистрация
    std::vector<uint8_t> reg_msg;
    reg_msg.push_back(1); reg_msg.push_back(1); reg_msg.push_back(0);
    for (int i = 0; i < 8; i++) reg_msg.push_back(0);
    reg_msg.push_back(4); reg_msg.push_back(0);
    reg_msg.push_back(0); reg_msg.push_back(0);
    reg_msg.push_back('t'); reg_msg.push_back('e'); reg_msg.push_back('s'); reg_msg.push_back('t');
    
    socket.send(zmq::buffer(reg_msg), zmq::send_flags::none);
    std::cout << "Registered" << std::endl;
    
    // Ждём ответ
    zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
    int poll_result = zmq::poll(items, 1, std::chrono::milliseconds(10000));
    
    if (poll_result > 0 && (items[0].revents & ZMQ_POLLIN)) {
        // Читаем ВСЕ сообщения, которые есть в буфере
        int message_count = 0;
        
        while (true) {
            // Проверяем, есть ли ещё данные
            zmq::pollitem_t check[] = {{socket, 0, ZMQ_POLLIN, 0}};
            zmq::poll(check, 1, std::chrono::milliseconds(0));
            if (!(check[0].revents & ZMQ_POLLIN)) {
                break;
            }
            
            message_count++;
            std::cout << "\n=== Message " << message_count << " ===" << std::endl;
            
            // Читаем одно мультифреймовое сообщение
            std::vector<zmq::message_t> frames;
            bool more = true;
            while (more) {
                zmq::message_t frame;
                auto result = socket.recv(frame, zmq::recv_flags::none);
                if (!result) {
                    std::cerr << "Failed to receive frame" << std::endl;
                    break;
                }
                more = frame.more();
                frames.push_back(std::move(frame));
            }
            
            std::cout << "Frames in this message: " << frames.size() << std::endl;
            
            for (size_t i = 0; i < frames.size(); i++) {
                std::cout << "  Frame " << i << ", size: " << frames[i].size() << " bytes" << std::endl;
                
                if (frames[i].size() > 0) {
                    const uint8_t* data = static_cast<const uint8_t*>(frames[i].data());
                    std::cout << "    Hex: ";
                    for (size_t j = 0; j < std::min(frames[i].size(), size_t(32)); j++) {
                        printf("%02x ", data[j]);
                    }
                    std::cout << std::endl;
                    
                    if (frames[i].size() > 15) {
                        size_t pos = 0;
                        uint8_t version = data[pos++];
                        uint8_t type = data[pos++];
                        uint8_t flags = data[pos++];
                        
                        uint64_t net_corr;
                        std::memcpy(&net_corr, &data[pos], 8);
                        uint64_t correlation_id = __builtin_bswap64(net_corr);
                        pos += 8;
                        
                        uint8_t sender_len = data[pos++];
                        uint8_t dest_len = data[pos++];
                        
                        uint16_t net_payload_len;
                        std::memcpy(&net_payload_len, &data[pos], 2);
                        uint16_t payload_len = ntohs(net_payload_len);
                        pos += 2;
                        
                        if (pos + sender_len + dest_len + payload_len <= frames[i].size()) {
                            std::string sender(reinterpret_cast<const char*>(&data[pos]), sender_len);
                            pos += sender_len;
                            std::string dest(reinterpret_cast<const char*>(&data[pos]), dest_len);
                            pos += dest_len;
                            std::string payload(reinterpret_cast<const char*>(&data[pos]), payload_len);
                            
                            const char* type_str = "Unknown";
                            if (type == 1) type_str = "Register";
                            else if (type == 2) type_str = "Message";
                            else if (type == 3) type_str = "Reply";
                            else if (type == 4) type_str = "Ack";
                            
                            std::cout << "    Parsed: type=" << type_str 
                                      << " (" << (int)type << ")"
                                      << ", from=" << sender 
                                      << ", to=" << dest 
                                      << ", payload=" << payload << std::endl;
                        }
                    }
                }
            }
        }
        
        std::cout << "\nTotal messages received: " << message_count << std::endl;
    } else {
        std::cout << "No reply received within 10 seconds" << std::endl;
    }
    
    return 0;
}