#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <zmq.hpp>
#include <boost/asio.hpp>

#include "router.hpp"
#include "storage.hpp"

namespace broker {

struct Config {
    int Port = 5555;
    std::string DbPath = "./broker.db";
    int Threads = 0;
    std::string LogLevel = "info";
};

class Server {
public:
    explicit Server(const Config& config);
    ~Server();
    
    void Run();
    void Stop();

private:
    void SetupZmqSocket();
    void ZmqPollThread();
    void AsioThread();
    
    Config config_;
    std::atomic<bool> running_{true};
    
    // ZeroMQ
    zmq::context_t zmq_context_;
    zmq::socket_t router_socket_;
    std::thread zmq_thread_;
    
    // Boost.Asio (для будущих расширений)
    boost::asio::io_context io_context_;
    std::vector<std::thread> threads_;
    
    // Бизнес-логика
    std::unique_ptr<Router> router_;
    std::unique_ptr<Storage> storage_;
};

} // namespace broker