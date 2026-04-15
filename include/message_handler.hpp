// message_handler.hpp
#pragma once

#include "message.hpp"
#include "interfaces.hpp"
#include "session.hpp"
#include <memory>
#include <functional>
#include <zmq.hpp>

namespace broker {

// Forward declarations
class Router;

// Контекст для обработчика сообщения
struct HandlerContext {
    IStorage& storage;
    ISessionManager& session_manager;
    IMessageSender& message_sender;
    IConfigProvider& config_provider;
    const zmq::message_t& identity;
};

// Базовый класс для всех обработчиков
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual void Handle(const Message& msg, const HandlerContext& ctx) = 0;
};

// Обработчик регистрации
class RegisterHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

// Обработчик обычных сообщений
class MessageHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

// Обработчик ответов
class ReplyHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

// Обработчик ACK
class AckHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

// Обработчик отключения
class UnregisterHandler : public IMessageHandler {
public:
    void Handle(const Message& msg, const HandlerContext& ctx) override;
};

// Фабрика обработчиков
class MessageHandlerFactory {
public:
    [[nodiscard]] static std::unique_ptr<IMessageHandler> Create(MessageType type);
};

} // namespace broker