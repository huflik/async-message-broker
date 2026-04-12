// router.hpp
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
#include "interfaces.hpp"

namespace broker {

class Router : public ISessionManager {
public:
    Router(IStorage& storage, 
           IMessageSender& message_sender,
           IConfigProvider& config_provider);
    
    void RouteMessage(const Message& msg, const zmq::message_t& identity);
    
    // Реализация ISessionManager
    std::shared_ptr<Session> FindSession(const std::string& name) override;
    bool RegisterClient(const std::string& name, std::shared_ptr<Session> session) override;
    void UnregisterClient(const std::string& name) override;
    void PrintActiveClients() override;
    void CleanupInactiveSessions() override;
    
    // Методы для доставки сообщений
    void DeliverOfflineMessages(const std::string& name);
    void DeliverPendingReplies(const std::string& name);
    void PersistMessageForClient(const std::string& client_name, const Message& msg);
    void CheckExpiredAcks();

private:
    void HandleRegister(const Message& msg, const zmq::message_t& identity);
    void HandleMessage(const Message& msg);
    void HandleReply(const Message& msg);
    void HandleAck(const Message& msg);
    void HandleUnregister(const Message& msg, const zmq::message_t& identity);
    
    IStorage& storage_;
    IMessageSender& message_sender_;
    IConfigProvider& config_provider_;
    
    std::unordered_map<std::string, std::shared_ptr<Session>> active_clients_;
    std::unordered_map<std::string, std::string> identity_to_name_;
    std::mutex registry_mutex_;
};

} // namespace broker