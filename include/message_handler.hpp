#pragma once

#include "message.hpp"
#include "interfaces.hpp"
#include "session.hpp"
#include "metrics.hpp"
#include <memory>
#include <functional>
#include <zmq.hpp>

namespace broker {

class Router;
struct HandlerContext {
    IStorage& storage;
    ISessionManager& session_manager;
    IMessageSender& message_sender;
    IConfigProvider& config_provider;
    const zmq::message_t& identity;
    std::shared_ptr<IMetrics> metrics;
};
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual void Handle(const Message& msg, const HandlerContext& ctx) = 0;
};
class RegisterHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

class MessageHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

class ReplyHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

class AckHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

class UnregisterHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

class MessageHandlerFactory {
public:
    [[nodiscard]] static std::unique_ptr<IMessageHandler> Create(MessageType type);
};

} 