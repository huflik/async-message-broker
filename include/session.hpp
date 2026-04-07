#pragma once

#include <memory>
#include <string>
#include <zmq.hpp>
#include "message.hpp"

namespace broker {

class Session {
public:
    virtual ~Session() = default;
    
    virtual bool SendMessage(const Message& msg) = 0;
    virtual std::string GetName() const = 0;
    virtual bool IsOnline() const = 0;
    
    virtual const zmq::message_t& GetIdentity() const = 0;
    
    virtual void SetName(const std::string& name) = 0;
    virtual void EnqueueMessage(const Message& msg) = 0;
    virtual void FlushQueue() = 0;
    virtual void UpdateLastReceive() = 0;
};

} // namespace broker