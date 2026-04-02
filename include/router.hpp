#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <zmq.hpp>
#include <spdlog/spdlog.h>

#include "message.hpp"
#include "storage.hpp"
#include "session.hpp"
#include "zmq_session.hpp"
#include "offline_queue.hpp"

namespace broker {

class Router {
public:
    Router(Storage& storage, zmq::socket_t& router_socket);
    
    void RouteMessage(const Message& msg, const zmq::message_t& identity);
    bool RegisterClient(const std::string& name, std::shared_ptr<ZmqSession> session);
    void UnregisterClient(const std::string& name);
    std::shared_ptr<ZmqSession> FindSession(const std::string& name);
    void HandleDisconnect(const zmq::message_t& identity);
    void DeliverOfflineMessages(const std::string& name);
    void PrintActiveClients();
    
    /**
     * Периодическая очистка неактивных сессий
     */
    void CleanupInactiveSessions();

private:
    void HandleRegister(const Message& msg, const zmq::message_t& identity);
    void HandleMessage(const Message& msg);
    void HandleReply(const Message& msg);
    void HandleAck(const Message& msg);
    std::shared_ptr<ZmqSession> FindSessionByIdentity(const zmq::message_t& identity);
    
    Storage& storage_;
    zmq::socket_t& router_socket_;
    OfflineQueueManager offline_manager_;
    
    std::unordered_map<std::string, std::shared_ptr<ZmqSession>> active_clients_;
    std::unordered_map<std::string, std::string> identity_to_name_;
    std::mutex registry_mutex_;
};

} // namespace broker