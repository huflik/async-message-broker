// tests/mock/mock_storage.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces.hpp"

namespace broker {

class MockStorage : public IStorage {
public:
    MOCK_METHOD(uint64_t, SaveMessage, (const Message& msg), (override));
    MOCK_METHOD(void, MarkDelivered, (uint64_t message_id), (override));
    MOCK_METHOD(void, MarkSent, (uint64_t message_id), (override));
    MOCK_METHOD(bool, NeedsAck, (uint64_t message_id), (override));
    MOCK_METHOD(void, MarkPending, (uint64_t message_id), (override));
    MOCK_METHOD(void, SaveCorrelation, (uint64_t message_id, uint64_t correlation_id, const std::string& original_sender), (override));
    MOCK_METHOD(std::string, FindOriginalSenderByCorrelation, (uint64_t correlation_id), (override));
    MOCK_METHOD(uint64_t, FindMessageIdByCorrelation, (uint64_t correlation_id), (override));
    MOCK_METHOD(uint64_t, FindMessageIdByCorrelationAndDestination, (uint64_t correlation_id, const std::string& destination), (override));
    MOCK_METHOD(void, MarkAckReceived, (uint64_t message_id, const std::string& ack_sender), (override));
    MOCK_METHOD(std::vector<PendingMessage>, LoadExpiredSent, (int timeout_seconds), (override));
    MOCK_METHOD(std::vector<PendingMessage>, LoadPendingRepliesForSenderOnly, (const std::string& sender_name), (override));
    MOCK_METHOD(std::vector<PendingMessage>, LoadPendingMessagesOnly, (const std::string& client_name), (override));
};

} // namespace broker