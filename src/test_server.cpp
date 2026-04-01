// src/test_server.cpp
#include <zmq.hpp>
#include <iostream>
#include <cstdio>
#include <chrono>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::router);
    
    socket.bind("tcp://*:5555");
    std::cout << "Test server bound to tcp://*:5555" << std::endl;
    
    int poll_count = 0;
    
    while (true) {
        poll_count++;
        if (poll_count % 100 == 0) {
            std::cout << "Poll loop alive, count: " << poll_count << std::endl;
        }
        
        zmq::pollitem_t items[] = {
            {socket, 0, ZMQ_POLLIN, 0}
        };
        
        zmq::poll(items, 1, std::chrono::milliseconds(100));
        
        if (items[0].revents & ZMQ_POLLIN) {
            std::cout << "\n=== MESSAGE RECEIVED! ===" << std::endl;
            
            // Первый фрейм — identity отправителя
            zmq::message_t identity;
            socket.recv(identity, zmq::recv_flags::none);
            std::cout << "Identity (" << identity.size() << " bytes): ";
            const uint8_t* id_bytes = static_cast<const uint8_t*>(identity.data());
            for (size_t i = 0; i < identity.size(); i++) {
                printf("%c", id_bytes[i]);
            }
            std::cout << std::endl;
            
            // Второй фрейм — данные сообщения (без разделителя!)
            zmq::message_t data;
            socket.recv(data, zmq::recv_flags::none);
            std::cout << "Data size: " << data.size() << " bytes" << std::endl;
            
            const uint8_t* bytes = static_cast<const uint8_t*>(data.data());
            std::string hex;
            for (size_t i = 0; i < std::min(data.size(), size_t(32)); i++) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02x ", bytes[i]);
                hex += buf;
            }
            std::cout << "Data hex: " << hex << std::endl;
            
            // Вывод первых байт как строки
            if (data.size() > 0) {
                std::cout << "Data as string (first 20 chars): ";
                for (size_t i = 0; i < std::min(data.size(), size_t(20)); i++) {
                    if (bytes[i] >= 32 && bytes[i] <= 126) {
                        std::cout << (char)bytes[i];
                    } else {
                        std::cout << '.';
                    }
                }
                std::cout << std::endl;
            }
        }
    }
    
    return 0;
}