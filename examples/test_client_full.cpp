#include <zmq.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <getopt.h>
#include <ctime>
#include <iomanip>
#include <sstream>

// Функция получения текущего времени в виде строки
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

// Структура сообщения
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

void register_client(zmq::socket_t& socket, const std::string& name) {
    SimpleMessage msg;
    msg.type = 1;
    msg.sender = name;
    socket.send(zmq::buffer(msg.serialize()), zmq::send_flags::none);
    std::cout << "[" << get_current_time() << "] [" << name << "] Registered" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void send_message(zmq::socket_t& socket, const std::string& from, const std::string& to, 
                  const std::string& payload, uint64_t corr_id, bool needs_reply) {
    SimpleMessage msg;
    msg.type = 2;
    msg.flags = needs_reply ? 1 : 0;
    msg.correlation_id = corr_id;
    msg.sender = from;
    msg.destination = to;
    msg.payload = payload;
    socket.send(zmq::buffer(msg.serialize()), zmq::send_flags::none);
    std::cout << "[" << get_current_time() << "] [" << from << "] Sent message to " << to 
              << ": \"" << payload << "\" (corr_id=" << corr_id << ")" << std::endl;
}

void send_reply(zmq::socket_t& socket, const std::string& from, const std::string& to,
                const std::string& payload, uint64_t corr_id) {
    SimpleMessage msg;
    msg.type = 3;
    msg.flags = 0;
    msg.correlation_id = corr_id;
    msg.sender = from;
    msg.destination = to;
    msg.payload = payload;
    socket.send(zmq::buffer(msg.serialize()), zmq::send_flags::none);
    std::cout << "[" << get_current_time() << "] [" << from << "] Sent reply to " << to 
              << ": \"" << payload << "\" (corr_id=" << corr_id << ")" << std::endl;
}

bool wait_for_message(zmq::socket_t& socket, SimpleMessage& received, int timeout_ms = 5000) {
    zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
    int poll_result = zmq::poll(items, 1, std::chrono::milliseconds(timeout_ms));
    
    if (poll_result > 0 && (items[0].revents & ZMQ_POLLIN)) {
        std::vector<zmq::message_t> frames;
        bool more = true;
        while (more) {
            zmq::message_t frame;
            auto recv_result = socket.recv(frame, zmq::recv_flags::none);
            if (!recv_result) break;
            more = frame.more();
            frames.push_back(std::move(frame));
        }
        
        for (const auto& frame : frames) {
            if (frame.size() > 0) {
                const uint8_t* data = static_cast<const uint8_t*>(frame.data());
                std::vector<uint8_t> msg_data(data, data + frame.size());
                received.deserialize(msg_data);
                return true;
            }
        }
    }
    return false;
}

// ==================== СЦЕНАРИЙ 1: Штатная работа ====================
void scenario1_alice() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "alice");
    socket.connect("tcp://localhost:5555");
    
    std::cout << "\n========== СЦЕНАРИЙ 1: Штатная работа ==========\n" << std::endl;
    
    register_client(socket, "alice");
    send_message(socket, "alice", "bob", "Hello, Bob!", 12345, true);
    
    SimpleMessage reply;
    if (wait_for_message(socket, reply, 5000)) {
        std::cout << "[" << get_current_time() << "] [alice] Received reply: \"" 
                  << reply.payload << "\" from " << reply.sender << std::endl;
    } else {
        std::cout << "[" << get_current_time() << "] [alice] No reply received" << std::endl;
    }
}

void scenario1_bob() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "bob");
    socket.connect("tcp://localhost:5555");
    
    std::cout << "\n========== СЦЕНАРИЙ 1: Штатная работа ==========\n" << std::endl;
    
    register_client(socket, "bob");
    std::cout << "[" << get_current_time() << "] [bob] Waiting for messages..." << std::endl;
    
    SimpleMessage msg;
    if (wait_for_message(socket, msg, 10000)) {
        std::cout << "[" << get_current_time() << "] [bob] Received: \"" << msg.payload 
                  << "\" from " << msg.sender << " (corr_id=" << msg.correlation_id << ")" << std::endl;
        if (msg.flags & 1) {
            send_reply(socket, "bob", msg.sender, "Hello, " + msg.sender + "!", msg.correlation_id);
        }
    }
}

// ==================== СЦЕНАРИЙ 2: Получатель офлайн ====================
void scenario2_alice() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "alice");
    socket.connect("tcp://localhost:5555");
    
    std::cout << "\n========== СЦЕНАРИЙ 2: Получатель офлайн ==========\n" << std::endl;
    
    register_client(socket, "alice");
    send_message(socket, "alice", "bob", "Hello, Bob! (delayed delivery)", 12345, true);
    std::cout << "[" << get_current_time() << "] [alice] Waiting for reply (Bob will come online later)..." << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Timeout set to 30 seconds" << std::endl;
    
    SimpleMessage reply;
    if (wait_for_message(socket, reply, 30000)) {
        std::cout << "[" << get_current_time() << "] [alice] Received reply: \"" 
                  << reply.payload << "\" from " << reply.sender << std::endl;
    } else {
        std::cout << "[" << get_current_time() << "] [alice] Timeout waiting for reply" << std::endl;
    }
}

void scenario2_bob() {
    std::cout << "\n========== СЦЕНАРИЙ 2: Получатель офлайн ==========\n" << std::endl;
    std::cout << "[" << get_current_time() << "] [bob] Waiting 2 seconds before connecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "bob");
    socket.connect("tcp://localhost:5555");
    
    register_client(socket, "bob");
    std::cout << "[" << get_current_time() << "] [bob] Checking for pending messages..." << std::endl;
    
    SimpleMessage msg;
    if (wait_for_message(socket, msg, 10000)) {
        std::cout << "[" << get_current_time() << "] [bob] Received pending: \"" << msg.payload 
                  << "\" from " << msg.sender << " (corr_id=" << msg.correlation_id << ")" << std::endl;
        if (msg.flags & 1) {
            send_reply(socket, "bob", msg.sender, "Hello, " + msg.sender + "! (delayed reply)", msg.correlation_id);
            std::cout << "[" << get_current_time() << "] [bob] Reply sent" << std::endl;
        }
    } else {
        std::cout << "[" << get_current_time() << "] [bob] No pending messages found" << std::endl;
    }
    
    std::cout << "[" << get_current_time() << "] [bob] Keeping connection open for 2 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

// ==================== СЦЕНАРИЙ 3: Отправитель отключился ====================
void scenario3_alice() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "alice");
    socket.connect("tcp://localhost:5555");
    
    std::cout << "\n========== СЦЕНАРИЙ 3: Отправитель отключился ==========\n" << std::endl;
    
    register_client(socket, "alice");
    send_message(socket, "alice", "bob", "Hello, Bob! (sender will disconnect)", 12345, true);
    std::cout << "[" << get_current_time() << "] [alice] Message sent, disconnecting immediately!" << std::endl;
    // Сокет закрывается при выходе из функции
}

void scenario3_bob() {
    std::cout << "\n========== СЦЕНАРИЙ 3: Отправитель отключился ==========\n" << std::endl;
    std::cout << "[" << get_current_time() << "] [bob] Waiting 3 seconds before connecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "bob");
    socket.connect("tcp://localhost:5555");
    
    register_client(socket, "bob");
    std::cout << "[" << get_current_time() << "] [bob] Waiting for messages..." << std::endl;
    
    SimpleMessage msg;
    if (wait_for_message(socket, msg, 10000)) {
        std::cout << "[" << get_current_time() << "] [bob] Received: \"" << msg.payload 
                  << "\" from " << msg.sender << " (corr_id=" << msg.correlation_id << ")" << std::endl;
        if (msg.flags & 1) {
            send_reply(socket, "bob", msg.sender, "Hello, " + msg.sender + "! (reply)", msg.correlation_id);
            std::cout << "[" << get_current_time() << "] [bob] Reply sent" << std::endl;
        }
    } else {
        std::cout << "[" << get_current_time() << "] [bob] No message received" << std::endl;
    }
    
    // Держим соединение открытым, чтобы убедиться, что ответ отправлен
    std::cout << "[" << get_current_time() << "] [bob] Keeping connection open for 2 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void scenario3_alice_reconnect() {
    std::cout << "\n========== СЦЕНАРИЙ 3: Отправитель переподключается ==========\n" << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Waiting 5 seconds before reconnecting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::dealer);
    socket.set(zmq::sockopt::routing_id, "alice");
    socket.connect("tcp://localhost:5555");
    
    register_client(socket, "alice");
    std::cout << "[" << get_current_time() << "] [alice] Waiting for pending reply..." << std::endl;
    std::cout << "[" << get_current_time() << "] [alice] Timeout set to 15 seconds" << std::endl;
    
    SimpleMessage reply;
    if (wait_for_message(socket, reply, 15000)) {
        std::cout << "[" << get_current_time() << "] [alice] Received pending reply: \"" 
                  << reply.payload << "\" from " << reply.sender << std::endl;
    } else {
        std::cout << "[" << get_current_time() << "] [alice] No pending reply" << std::endl;
    }
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " --scenario N --role ROLE\n\n"
              << "SCENARIOS:\n"
              << "  1 - Normal work (both online)\n"
              << "  2 - Receiver offline (delayed delivery). Alice sends, Bob connects later,\n"
              << "      Bob replies, Alice (online) receives reply.\n"
              << "  3 - Sender offline, reconnects later to get reply\n\n"
              << "ROLES:\n"
              << "  Scenario 1: --role alice  or  --role bob\n"
              << "  Scenario 2: --role alice  or  --role bob\n"
              << "  Scenario 3: --role alice, then --role bob, then --role alice_reconnect\n\n"
              << "EXAMPLES:\n"
              << "  # Scenario 1 (terminal 1): " << prog << " --scenario 1 --role bob\n"
              << "  # Scenario 1 (terminal 2): " << prog << " --scenario 1 --role alice\n\n"
              << "  # Scenario 2 (terminal 1): " << prog << " --scenario 2 --role alice\n"
              << "  # Scenario 2 (terminal 2): " << prog << " --scenario 2 --role bob\n\n"
              << "  # Scenario 3 (terminal 1): " << prog << " --scenario 3 --role alice\n"
              << "  # Scenario 3 (terminal 2): " << prog << " --scenario 3 --role bob\n"
              << "  # Scenario 3 (terminal 1): " << prog << " --scenario 3 --role alice_reconnect\n";
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
        default:
            std::cerr << "Error: Invalid scenario " << scenario << std::endl;
            return 1;
    }
    
    return 0;
}