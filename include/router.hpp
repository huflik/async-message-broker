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

class Server;

class Router {
public:
    Router(Storage& storage, zmq::socket_t& router_socket, Server& server);
    
    void RouteMessage(const Message& msg, const zmq::message_t& identity);
    bool RegisterClient(const std::string& name, std::shared_ptr<ZmqSession> session);
    void UnregisterClient(const std::string& name);
    
    std::shared_ptr<ZmqSession> FindSession(const std::string& name);
    
    void HandleDisconnect(const zmq::message_t& identity);
    void DeliverOfflineMessages(const std::string& name);
    void DeliverPendingReplies(const std::string& name);
    
    void PrintActiveClients();
    void CleanupInactiveSessions();
    void PersistMessageForClient(const std::string& client_name, const Message& msg);
    void CheckHeartbeats();

private:
    void HandleRegister(const Message& msg, const zmq::message_t& identity);
    void HandleMessage(const Message& msg);
    void HandleReply(const Message& msg);
    void HandleAck(const Message& msg);
    std::shared_ptr<ZmqSession> FindSessionByIdentity(const zmq::message_t& identity);
    
    Storage& storage_;
    zmq::socket_t& router_socket_;
    Server& server_;
    OfflineQueueManager offline_manager_;
    
    std::unordered_map<std::string, std::shared_ptr<ZmqSession>> active_clients_;
    std::unordered_map<std::string, std::string> identity_to_name_;
    std::mutex registry_mutex_;
};

} // namespace broker