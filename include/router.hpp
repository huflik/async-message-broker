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
#include "metrics.hpp"

namespace broker {

class Router : public ISessionManager {
public:
    Router(IStorage& storage, 
           IMessageSender& message_sender,
           IConfigProvider& config_provider,
           std::shared_ptr<IMetrics> metrics = nullptr);
    
    void RouteMessage(const Message& msg, const zmq::message_t& identity);
    
    [[nodiscard]] std::shared_ptr<Session> FindSession(const std::string& name) override;
    [[nodiscard]] bool RegisterClient(const std::string& name, std::shared_ptr<Session> session) override;
    void UnregisterClient(const std::string& name) override;
    void PrintActiveClients() override;
    void CleanupInactiveSessions() override;
    
    void DeliverOfflineMessages(const std::string& name);
    void DeliverPendingReplies(const std::string& name);
    void PersistMessageForClient(const std::string& client_name, const Message& msg);
    void CheckExpiredAcks();

private:
    void UpdateSessionMetrics();   

    IStorage& storage_;
    IMessageSender& message_sender_;
    IConfigProvider& config_provider_;
    std::shared_ptr<IMetrics> metrics_;
    
    std::unordered_map<std::string, std::shared_ptr<Session>> active_clients_;
    std::unordered_map<std::string, std::string> identity_to_name_;
    std::mutex registry_mutex_;
};

}