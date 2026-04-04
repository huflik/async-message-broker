#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <zmq.hpp>
#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>

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
    void SetupAsioIntegration();
    void SetupTcpKeepAlive();
    void OnZmqEvent(const boost::system::error_code& ec);
    void AsioThread();
    void SetupCleanupTimer();

private:
    Config config_;
    std::atomic<bool> running_{true};
    
    zmq::context_t zmq_context_;
    zmq::socket_t router_socket_;
    
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_guard_;
    std::vector<std::thread> threads_;
    std::unique_ptr<boost::asio::posix::stream_descriptor> zmq_fd_;
    std::unique_ptr<boost::asio::steady_timer> cleanup_timer_;
    
    std::unique_ptr<Router> router_;
    std::unique_ptr<Storage> storage_;
};

} // namespace broker