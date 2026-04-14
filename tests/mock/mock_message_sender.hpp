// tests/mock/mock_message_sender.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces.hpp"

namespace broker {

class MockMessageSender : public IMessageSender {
public:
    MOCK_METHOD(void, SendToClient, 
                (zmq::message_t identity, zmq::message_t data, std::function<void(bool)> callback),
                (override));
};

} // namespace broker