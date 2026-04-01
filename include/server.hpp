#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <zmq.hpp>
#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

#include "router.hpp"
#include "storage.hpp"

namespace broker {

struct Config {
    int Port = 5555;
    std::string DbPath = "./broker.db";
    int Threads = 0;
    std::string LogLevel = "info";
};

/**
 * Класс Server с интеграцией ZeroMQ и Boost.Asio через файловые дескрипторы
 * 
 * Как это работает:
 * 1. ZeroMQ предоставляет файловый дескриптор через опцию ZMQ_FD
 * 2. Дескриптор оборачивается в boost::asio::posix::stream_descriptor
 * 3. Asio асинхронно ждёт события на этом дескрипторе
 * 4. При срабатывании проверяем реальные события через ZMQ_EVENTS
 * 5. Читаем сообщения и передаём в Router
 */
class Server {
public:
    explicit Server(const Config& config);
    ~Server();
    
    void Run();
    void Stop();

private:
    void SetupZmqSocket();
    void SetupAsioIntegration();
    void OnZmqEvent(const boost::system::error_code& ec);
    void HandleZmqMessage();
    void AsioThread();

private:
    Config config_;
    std::atomic<bool> running_{true};
    
    // ZeroMQ
    zmq::context_t zmq_context_;
    zmq::socket_t router_socket_;
    
    // Boost.Asio
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_guard_;
    std::vector<std::thread> threads_;
    std::unique_ptr<boost::asio::posix::stream_descriptor> zmq_fd_;
    
    // Бизнес-логика
    std::unique_ptr<Router> router_;
    std::unique_ptr<Storage> storage_;
};

} // namespace broker