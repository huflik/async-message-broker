#pragma once

#include "message.hpp"
#include "config.hpp"
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <zmq.hpp>

namespace broker {

class Session;
struct PendingMessage;
class IMetrics;
class IStorage {
public:
    virtual ~IStorage() = default;
    
    [[nodiscard]] virtual uint64_t SaveMessage(const Message& msg) = 0;
    virtual void MarkDelivered(uint64_t message_id) = 0;
    virtual void MarkSent(uint64_t message_id) = 0;
    virtual bool NeedsAck(uint64_t message_id) = 0;
    virtual void MarkPending(uint64_t message_id) = 0;
    virtual void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, 
                                  const std::string& original_sender) = 0;
    [[nodiscard]] virtual std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) = 0;
    [[nodiscard]] virtual uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) = 0;
    [[nodiscard]] virtual uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, 
                                                                const std::string& destination) = 0;
    virtual void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) = 0;
    [[nodiscard]] virtual std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) = 0;
    [[nodiscard]] virtual std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name) = 0;
    [[nodiscard]] virtual std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name) = 0;
};
class IMessageSender {
public:
    virtual ~IMessageSender() = default;
    virtual void SendToClient(zmq::message_t identity, 
                              zmq::message_t data,
                              std::function<void(bool)> callback = nullptr) = 0;
};

class IConfigProvider {
public:
    virtual ~IConfigProvider() = default;
    virtual const Config& GetConfig() const = 0;
};

class ISessionManager {
public:
    virtual ~ISessionManager() = default;
    
    [[nodiscard]] virtual std::shared_ptr<Session> FindSession(const std::string& name) = 0;
    [[nodiscard]] virtual bool RegisterClient(const std::string& name, std::shared_ptr<Session> session) = 0;
    virtual void UnregisterClient(const std::string& name) = 0;
    virtual void PrintActiveClients() = 0;
    virtual void CleanupInactiveSessions() = 0;
    
    virtual void DeliverOfflineMessages(const std::string& name) = 0;
    virtual void DeliverPendingReplies(const std::string& name) = 0;
    virtual void PersistMessageForClient(const std::string& client_name, const Message& msg) = 0;
    virtual void CheckExpiredAcks() = 0;

};

class ISessionCallbacks {
public:
    virtual ~ISessionCallbacks() = default;
    virtual void OnMessagePersist(const std::string& client_name, const Message& msg) = 0;
    virtual void OnMessageSent(const std::string& client_name, const Message& msg) = 0;
    virtual void OnMessageFailed(const std::string& client_name, const Message& msg) = 0;
};

class IMetricsProvider {
public:
    virtual ~IMetricsProvider() = default;
    [[nodiscard]] virtual std::shared_ptr<IMetrics> GetMetrics() const = 0;
};

}