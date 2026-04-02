// reconnect_client.cpp
#include <zmq.hpp>
#include <iostream>
#include <vector>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    
    socket.set(zmq::sockopt::routing_id, "test_client");
    socket.connect("tcp://localhost:5555");
    std::cout << "Reconnecting..." << std::endl;
    
    // Только регистрация
    std::vector<uint8_t> reg_msg;
    reg_msg.push_back(1);
    reg_msg.push_back(1);  // Register
    reg_msg.push_back(0);
    for (int i = 0; i < 8; i++) reg_msg.push_back(0);
    reg_msg.push_back(4);
    reg_msg.push_back(0);
    reg_msg.push_back(0);
    reg_msg.push_back(0);
    reg_msg.push_back('t');
    reg_msg.push_back('e');
    reg_msg.push_back('s');
    reg_msg.push_back('t');
    
    socket.send(zmq::buffer(reg_msg), zmq::send_flags::none);
    std::cout << "Registered" << std::endl;
    
    // Ожидание ответа
    zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
    zmq::poll(items, 1, std::chrono::milliseconds(5000));
    
    if (items[0].revents & ZMQ_POLLIN) {
        zmq::message_t delimiter;
        socket.recv(delimiter, zmq::recv_flags::none);
        
        zmq::message_t reply;
        socket.recv(reply, zmq::recv_flags::none);
        
        std::vector<uint8_t> data(
            static_cast<uint8_t*>(reply.data()),
            static_cast<uint8_t*>(reply.data()) + reply.size()
        );
        
        std::cout << "Received reply, size: " << data.size() << std::endl;
    } else {
        std::cout << "No pending messages" << std::endl;
    }
    
    return 0;
}