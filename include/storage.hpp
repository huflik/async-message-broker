#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include "message.hpp"
#include "interfaces.hpp"  

namespace broker {

struct PendingMessage {
    uint64_t id;
    Message msg;

    PendingMessage() = default;

    PendingMessage(const PendingMessage&) = default;
    PendingMessage& operator=(const PendingMessage&) = default;

    PendingMessage(PendingMessage&&) noexcept = default;
    PendingMessage& operator=(PendingMessage&&) noexcept = default;
};

enum MessageStatus : int {
    STATUS_PENDING = 0,       
    STATUS_DELIVERED = 1,    
    STATUS_SENT = 2          
};

class Storage : public IStorage {
public:
    explicit Storage(const std::string& db_path);
    ~Storage();
    
    [[nodiscard]] uint64_t SaveMessage(const Message& msg) override;
    void MarkDelivered(uint64_t message_id) override;
    void MarkSent(uint64_t message_id) override;
    bool NeedsAck(uint64_t message_id) override;
    void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender) override;
    [[nodiscard]] std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) override;
    [[nodiscard]] uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) override;
    [[nodiscard]] std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) override;
    void MarkPending(uint64_t message_id) override;
    [[nodiscard]] std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name) override;
    void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) override;
    [[nodiscard]] std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name) override;
    [[nodiscard]] uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, const std::string& destination) override;

private:
    void CreateTables();
    uint64_t SaveMessageWithStatus(const Message& msg, int status);   
    
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex db_mutex_;
};

}