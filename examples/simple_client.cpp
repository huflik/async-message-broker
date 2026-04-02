#include <zmq.hpp>
#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <chrono>
#include <thread>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "test_client");
    socket.connect("tcp://localhost:5555");
    
    // Даём время на установку соединения
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Регистрация
    std::vector<uint8_t> reg_msg;
    reg_msg.push_back(1); reg_msg.push_back(1); reg_msg.push_back(0);
    for (int i = 0; i < 8; i++) reg_msg.push_back(0);
    reg_msg.push_back(4); reg_msg.push_back(0);
    reg_msg.push_back(0); reg_msg.push_back(0);
    reg_msg.push_back('t'); reg_msg.push_back('e'); reg_msg.push_back('s'); reg_msg.push_back('t');
    socket.send(zmq::buffer(reg_msg), zmq::send_flags::none);
    std::cout << "Registered" << std::endl;
    
    // Небольшая задержка для регистрации на сервере
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Сообщение
    std::vector<uint8_t> msg;
    msg.push_back(1); msg.push_back(2); msg.push_back(1);
    uint64_t corr = 12345;
    uint64_t net_corr = __builtin_bswap64(corr);
    for (int i = 0; i < 8; i++) msg.push_back(((const uint8_t*)&net_corr)[i]);
    msg.push_back(4); msg.push_back(3);
    uint16_t len = 5;
    uint16_t net_len = htons(len);
    msg.push_back(((const uint8_t*)&net_len)[0]);
    msg.push_back(((const uint8_t*)&net_len)[1]);
    msg.push_back('t'); msg.push_back('e'); msg.push_back('s'); msg.push_back('t');
    msg.push_back('b'); msg.push_back('o'); msg.push_back('b');
    msg.push_back('H'); msg.push_back('e'); msg.push_back('l'); msg.push_back('l'); msg.push_back('o');
    
    socket.send(zmq::buffer(msg), zmq::send_flags::none);
    std::cout << "Message sent, exiting..." << std::endl;
    
    // Закрываем сокет и контекст перед выходом
    socket.close();
    context.close();
    
    return 0;
}