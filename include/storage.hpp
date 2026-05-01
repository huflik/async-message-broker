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

class SqliteStmt {
public:
    explicit SqliteStmt(sqlite3* db, const char* sql) {
        sqlite3_stmt* raw_stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, sql, -1, &raw_stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare statement: " + 
                                     std::string(sqlite3_errmsg(db)));
        }
        stmt_.reset(raw_stmt);
    }
    
    sqlite3_stmt* get() const { return stmt_.get(); }
    operator sqlite3_stmt*() const { return stmt_.get(); }
    
private:
    struct Deleter {
        void operator()(sqlite3_stmt* stmt) const {
            if (stmt) sqlite3_finalize(stmt);
        }
    };
    std::unique_ptr<sqlite3_stmt, Deleter> stmt_;
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
    void PrepareStatements();
    uint64_t SaveMessageWithStatus(const Message& msg, int status);
    
    void ResetStatement(sqlite3_stmt* stmt);
    
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex db_mutex_;
    
    std::unique_ptr<SqliteStmt> insert_message_stmt_;
    std::unique_ptr<SqliteStmt> update_delivered_stmt_;
    std::unique_ptr<SqliteStmt> update_sent_stmt_;
    std::unique_ptr<SqliteStmt> update_pending_stmt_;
    std::unique_ptr<SqliteStmt> select_needs_ack_stmt_;
    std::unique_ptr<SqliteStmt> insert_correlation_stmt_;
    std::unique_ptr<SqliteStmt> select_original_sender_stmt_;
    std::unique_ptr<SqliteStmt> select_message_id_by_corr_stmt_;
    std::unique_ptr<SqliteStmt> select_message_id_by_corr_dest_stmt_;
    std::unique_ptr<SqliteStmt> select_original_sender_by_msg_id_stmt_;
    std::unique_ptr<SqliteStmt> delete_correlation_stmt_;
    std::unique_ptr<SqliteStmt> select_expired_sent_stmt_;
    std::unique_ptr<SqliteStmt> select_pending_messages_stmt_;
    std::unique_ptr<SqliteStmt> select_pending_replies_stmt_;
};

}