#include "storage.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <set>

namespace broker {

Storage::Storage(const std::string& db_path)
    : db_path_(db_path)
{
    spdlog::info("Opening database: {}", db_path);
    
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to open database: " + std::string(sqlite3_errmsg(db_)));
    }
    
    char* errmsg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("Failed to set WAL mode: {}", errmsg);
        sqlite3_free(errmsg);
    }
    
    rc = sqlite3_exec(db_, "PRAGMA busy_timeout=10000;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("Failed to set busy_timeout: {}", errmsg);
        sqlite3_free(errmsg);
    }
    
    rc = sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("Failed to enable foreign keys: {}", errmsg);
        sqlite3_free(errmsg);
    }
    
    CreateTables();
    PrepareStatements();
    
    spdlog::info("Storage initialized");
}

Storage::~Storage() {
    if (db_) {
        sqlite3_close(db_);
        spdlog::debug("Database closed");
    }
}

void Storage::ResetStatement(sqlite3_stmt* stmt) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
}

void Storage::PrepareStatements() {
    const char* insert_message_sql = R"(
        INSERT INTO messages (type, flags, correlation_id, sender, destination, payload, status)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";
    insert_message_stmt_ = std::make_unique<SqliteStmt>(db_, insert_message_sql);
    
    const char* update_delivered_sql = "UPDATE messages SET status = ?, delivered_at = CURRENT_TIMESTAMP WHERE id = ?";
    update_delivered_stmt_ = std::make_unique<SqliteStmt>(db_, update_delivered_sql);
    
    const char* update_sent_sql = "UPDATE messages SET status = ? WHERE id = ? AND status = ?";
    update_sent_stmt_ = std::make_unique<SqliteStmt>(db_, update_sent_sql);
    
    const char* update_pending_sql = "UPDATE messages SET status = ? WHERE id = ? AND status = ?";
    update_pending_stmt_ = std::make_unique<SqliteStmt>(db_, update_pending_sql);
    
    const char* select_needs_ack_sql = "SELECT flags, status FROM messages WHERE id = ?";
    select_needs_ack_stmt_ = std::make_unique<SqliteStmt>(db_, select_needs_ack_sql);
    
    const char* insert_correlation_sql = R"(
        INSERT OR REPLACE INTO correlations (message_id, correlation_id, original_sender)
        VALUES (?, ?, ?)
    )";
    insert_correlation_stmt_ = std::make_unique<SqliteStmt>(db_, insert_correlation_sql);
    
    const char* select_original_sender_sql = R"(
        SELECT original_sender 
        FROM correlations 
        WHERE correlation_id = ?
        ORDER BY created_at DESC
        LIMIT 1
    )";
    select_original_sender_stmt_ = std::make_unique<SqliteStmt>(db_, select_original_sender_sql);
    
    const char* select_message_id_by_corr_sql = R"(
        SELECT message_id 
        FROM correlations 
        WHERE correlation_id = ?
        ORDER BY created_at DESC
        LIMIT 1
    )";
    select_message_id_by_corr_stmt_ = std::make_unique<SqliteStmt>(db_, select_message_id_by_corr_sql);
    
    const char* select_message_id_by_corr_dest_sql = R"(
        SELECT m.id
        FROM messages m
        INNER JOIN correlations c ON m.id = c.message_id
        WHERE c.correlation_id = ? AND m.destination = ?
        ORDER BY c.created_at DESC
        LIMIT 1
    )";
    select_message_id_by_corr_dest_stmt_ = std::make_unique<SqliteStmt>(db_, select_message_id_by_corr_dest_sql);
    
    const char* select_original_sender_by_msg_id_sql = R"(
        SELECT original_sender 
        FROM correlations 
        WHERE message_id = ?
    )";
    select_original_sender_by_msg_id_stmt_ = std::make_unique<SqliteStmt>(db_, select_original_sender_by_msg_id_sql);
    
    const char* delete_correlation_sql = "DELETE FROM correlations WHERE message_id = ?";
    delete_correlation_stmt_ = std::make_unique<SqliteStmt>(db_, delete_correlation_sql);
    
    const char* select_expired_sent_sql = R"(
        SELECT id, type, flags, correlation_id, sender, destination, payload
        FROM messages
        WHERE status = ? 
          AND strftime('%s', 'now') - strftime('%s', created_at) > ?
        ORDER BY created_at ASC
    )";
    select_expired_sent_stmt_ = std::make_unique<SqliteStmt>(db_, select_expired_sent_sql);
    
    const char* select_pending_messages_sql = R"(
        SELECT id, type, flags, correlation_id, sender, destination, payload
        FROM messages
        WHERE destination = ? 
          AND status = ?
          AND type != ?
        ORDER BY created_at ASC
    )";
    select_pending_messages_stmt_ = std::make_unique<SqliteStmt>(db_, select_pending_messages_sql);
    
    const char* select_pending_replies_sql = R"(
        SELECT DISTINCT m.id, m.type, m.flags, m.correlation_id, m.sender, m.destination, m.payload
        FROM messages m
        INNER JOIN correlations c ON m.correlation_id = c.correlation_id
        WHERE c.original_sender = ? 
          AND m.status IN (?, ?)
          AND m.type = ?
        ORDER BY m.created_at ASC
    )";
    select_pending_replies_stmt_ = std::make_unique<SqliteStmt>(db_, select_pending_replies_sql);
    
    spdlog::debug("All SQL statements prepared");
}

void Storage::CreateTables() {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string error = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to begin transaction: " + error);
    }
    
    const char* create_messages_sql = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER NOT NULL,
            flags INTEGER NOT NULL,
            correlation_id INTEGER NOT NULL,
            sender TEXT NOT NULL,
            destination TEXT NOT NULL,
            payload BLOB,
            status INTEGER NOT NULL DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            delivered_at TIMESTAMP
        );
    )";
    
    rc = sqlite3_exec(db_, create_messages_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        std::string error = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create messages table: " + error);
    }
    
    const char* create_correlations_sql = R"(
        CREATE TABLE IF NOT EXISTS correlations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            message_id INTEGER NOT NULL,
            correlation_id INTEGER NOT NULL,
            original_sender TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE
        );
    )";
    
    rc = sqlite3_exec(db_, create_correlations_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        std::string error = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create correlations table: " + error);
    }
    
    const char* create_indexes_sql = R"(
        CREATE INDEX IF NOT EXISTS idx_messages_destination ON messages(destination);
        CREATE INDEX IF NOT EXISTS idx_messages_status ON messages(status);
        CREATE INDEX IF NOT EXISTS idx_messages_type ON messages(type);
        CREATE INDEX IF NOT EXISTS idx_messages_dest_status ON messages(destination, status);
        CREATE INDEX IF NOT EXISTS idx_messages_correlation ON messages(correlation_id);
        CREATE INDEX IF NOT EXISTS idx_correlations_corr_id ON correlations(correlation_id);
        CREATE INDEX IF NOT EXISTS idx_correlations_original_sender ON correlations(original_sender);
        CREATE INDEX IF NOT EXISTS idx_correlations_message_id ON correlations(message_id);
        CREATE UNIQUE INDEX IF NOT EXISTS idx_correlations_unique ON correlations(correlation_id, original_sender);
    )";
    
    rc = sqlite3_exec(db_, create_indexes_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        std::string error = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create indexes: " + error);
    }
    
    rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string error = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to commit transaction: " + error);
    }
    
    spdlog::debug("Database tables created/verified");
}

uint64_t Storage::SaveMessage(const Message& msg) {
    return SaveMessageWithStatus(msg, STATUS_PENDING);
}

uint64_t Storage::SaveMessageWithStatus(const Message& msg, int status) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*insert_message_stmt_);
    
    sqlite3_bind_int(*insert_message_stmt_, 1, static_cast<int>(msg.GetType()));
    sqlite3_bind_int(*insert_message_stmt_, 2, msg.GetFlags());
    sqlite3_bind_int64(*insert_message_stmt_, 3, static_cast<sqlite3_int64>(msg.GetCorrelationId()));
    sqlite3_bind_text(*insert_message_stmt_, 4, msg.GetSender().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(*insert_message_stmt_, 5, msg.GetDestination().c_str(), -1, SQLITE_TRANSIENT);
    
    const auto& payload = msg.GetPayload();
    if (payload.empty()) {
        sqlite3_bind_null(*insert_message_stmt_, 6);
    } else {
        sqlite3_bind_blob(*insert_message_stmt_, 6, payload.data(), payload.size(), SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(*insert_message_stmt_, 7, status);
    
    int rc = sqlite3_step(*insert_message_stmt_);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to insert message: " + std::string(sqlite3_errmsg(db_)));
    }
    
    return static_cast<uint64_t>(sqlite3_last_insert_rowid(db_));
}

void Storage::MarkDelivered(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*update_delivered_stmt_);
    
    sqlite3_bind_int(*update_delivered_stmt_, 1, STATUS_DELIVERED);
    sqlite3_bind_int64(*update_delivered_stmt_, 2, static_cast<sqlite3_int64>(message_id));
    
    int rc = sqlite3_step(*update_delivered_stmt_);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to mark message as delivered: " + std::string(sqlite3_errmsg(db_)));
    }
    
    spdlog::debug("Message {} marked as delivered", message_id);
}

void Storage::MarkSent(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*update_sent_stmt_);
    
    sqlite3_bind_int(*update_sent_stmt_, 1, STATUS_SENT);
    sqlite3_bind_int64(*update_sent_stmt_, 2, static_cast<sqlite3_int64>(message_id));
    sqlite3_bind_int(*update_sent_stmt_, 3, STATUS_PENDING);
    
    int rc = sqlite3_step(*update_sent_stmt_);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to mark message as sent: " + std::string(sqlite3_errmsg(db_)));
    }
    
    int changes = sqlite3_changes(db_);
    spdlog::debug("Message {} marked as sent (waiting for ACK), {} rows affected", message_id, changes);
}

void Storage::MarkPending(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*update_pending_stmt_);
    
    sqlite3_bind_int(*update_pending_stmt_, 1, STATUS_PENDING);
    sqlite3_bind_int64(*update_pending_stmt_, 2, static_cast<sqlite3_int64>(message_id));
    sqlite3_bind_int(*update_pending_stmt_, 3, STATUS_SENT);
    
    int rc = sqlite3_step(*update_pending_stmt_);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to mark message as pending: " + std::string(sqlite3_errmsg(db_)));
    }
    
    int changes = sqlite3_changes(db_);
    spdlog::debug("Message {} marked as pending, {} rows affected", message_id, changes);
}

bool Storage::NeedsAck(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_needs_ack_stmt_);
    sqlite3_bind_int64(*select_needs_ack_stmt_, 1, static_cast<sqlite3_int64>(message_id));
    
    bool needs_ack = false;
    
    if (sqlite3_step(*select_needs_ack_stmt_) == SQLITE_ROW) {
        uint8_t flags = static_cast<uint8_t>(sqlite3_column_int(*select_needs_ack_stmt_, 0));
        int status = sqlite3_column_int(*select_needs_ack_stmt_, 1);
        needs_ack = ((flags & FlagNeedsAck) != 0) && (status == STATUS_SENT);
    }
    
    return needs_ack;
}

void Storage::SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    spdlog::debug("Storage::SaveCorrelation: msg_id={}, corr_id={}, sender={}", 
                  message_id, correlation_id, original_sender);
    
    ResetStatement(*insert_correlation_stmt_);
    
    sqlite3_bind_int64(*insert_correlation_stmt_, 1, static_cast<sqlite3_int64>(message_id));
    sqlite3_bind_int64(*insert_correlation_stmt_, 2, static_cast<sqlite3_int64>(correlation_id));
    sqlite3_bind_text(*insert_correlation_stmt_, 3, original_sender.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(*insert_correlation_stmt_);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to save correlation: " + std::string(sqlite3_errmsg(db_)));
    }
    
    spdlog::info("Correlation saved: message_id={}, corr_id={}, original_sender={}", 
                 message_id, correlation_id, original_sender);
}

std::string Storage::FindOriginalSenderByCorrelation(uint64_t correlation_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_original_sender_stmt_);
    sqlite3_bind_int64(*select_original_sender_stmt_, 1, static_cast<sqlite3_int64>(correlation_id));
    
    std::string original_sender;
    
    if (sqlite3_step(*select_original_sender_stmt_) == SQLITE_ROW) {
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(*select_original_sender_stmt_, 0));
        if (sender) {
            original_sender = sender;
        }
    }
    
    if (!original_sender.empty()) {
        spdlog::debug("Found original sender '{}' for correlation_id={}", 
                      original_sender, correlation_id);
    } else {
        spdlog::debug("No original sender found for correlation_id={}", correlation_id);
    }
    
    return original_sender;
}

uint64_t Storage::FindMessageIdByCorrelation(uint64_t correlation_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_message_id_by_corr_stmt_);
    sqlite3_bind_int64(*select_message_id_by_corr_stmt_, 1, static_cast<sqlite3_int64>(correlation_id));
    
    uint64_t message_id = 0;
    
    if (sqlite3_step(*select_message_id_by_corr_stmt_) == SQLITE_ROW) {
        message_id = static_cast<uint64_t>(sqlite3_column_int64(*select_message_id_by_corr_stmt_, 0));
        spdlog::debug("Found message_id={} for correlation_id={}", message_id, correlation_id);
    } else {
        spdlog::warn("No message_id found for correlation_id={}", correlation_id);
    }
    
    return message_id;
}

uint64_t Storage::FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, const std::string& destination) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_message_id_by_corr_dest_stmt_);
    sqlite3_bind_int64(*select_message_id_by_corr_dest_stmt_, 1, static_cast<sqlite3_int64>(correlation_id));
    sqlite3_bind_text(*select_message_id_by_corr_dest_stmt_, 2, destination.c_str(), -1, SQLITE_TRANSIENT);
    
    uint64_t message_id = 0;
    
    if (sqlite3_step(*select_message_id_by_corr_dest_stmt_) == SQLITE_ROW) {
        message_id = static_cast<uint64_t>(sqlite3_column_int64(*select_message_id_by_corr_dest_stmt_, 0));
        spdlog::debug("Found message_id={} for correlation_id={}, destination={}", 
                      message_id, correlation_id, destination);
    } else {
        spdlog::debug("No message_id found for correlation_id={}, destination={}", 
                      correlation_id, destination);
    }
    
    return message_id;
}

void Storage::MarkAckReceived(uint64_t message_id, const std::string& ack_sender) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_original_sender_by_msg_id_stmt_);
    sqlite3_bind_int64(*select_original_sender_by_msg_id_stmt_, 1, static_cast<sqlite3_int64>(message_id));
    
    std::string original_sender;
    if (sqlite3_step(*select_original_sender_by_msg_id_stmt_) == SQLITE_ROW) {
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(*select_original_sender_by_msg_id_stmt_, 0));
        if (sender) {
            original_sender = sender;
        }
    }
    
    if (original_sender == ack_sender) {
        ResetStatement(*delete_correlation_stmt_);
        sqlite3_bind_int64(*delete_correlation_stmt_, 1, static_cast<sqlite3_int64>(message_id));
        sqlite3_step(*delete_correlation_stmt_);
        spdlog::debug("Deleted correlation for message {} after ACK from {}", 
                      message_id, ack_sender);
    } else {
        spdlog::debug("Preserved correlation for message {}: ACK from {} != original sender {}", 
                      message_id, ack_sender, original_sender);
    }
}

std::vector<PendingMessage> Storage::LoadExpiredSent(int timeout_seconds) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_expired_sent_stmt_);
    sqlite3_bind_int(*select_expired_sent_stmt_, 1, STATUS_SENT);
    sqlite3_bind_int(*select_expired_sent_stmt_, 2, timeout_seconds);
    
    std::vector<PendingMessage> messages;
    
    while (sqlite3_step(*select_expired_sent_stmt_) == SQLITE_ROW) {
        PendingMessage pending;
        pending.id = static_cast<uint64_t>(sqlite3_column_int64(*select_expired_sent_stmt_, 0));
        
        Message msg;
        msg.SetType(static_cast<MessageType>(sqlite3_column_int(*select_expired_sent_stmt_, 1)));
        msg.SetFlags(static_cast<uint8_t>(sqlite3_column_int(*select_expired_sent_stmt_, 2)));
        msg.SetCorrelationId(static_cast<uint64_t>(sqlite3_column_int64(*select_expired_sent_stmt_, 3)));
        
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(*select_expired_sent_stmt_, 4));
        if (sender) msg.SetSender(sender);
        
        const char* destination = reinterpret_cast<const char*>(sqlite3_column_text(*select_expired_sent_stmt_, 5));
        if (destination) msg.SetDestination(destination);
        
        const void* blob = sqlite3_column_blob(*select_expired_sent_stmt_, 6);
        int blob_size = sqlite3_column_bytes(*select_expired_sent_stmt_, 6);
        if (blob && blob_size > 0) {
            std::vector<uint8_t> payload(static_cast<const uint8_t*>(blob), 
                                          static_cast<const uint8_t*>(blob) + blob_size);
            msg.SetPayload(payload);
        }
        
        pending.msg = std::move(msg);
        messages.push_back(std::move(pending));
    }
    
    if (!messages.empty()) {
        spdlog::info("Loaded {} expired sent messages", messages.size());
    }
    
    return messages;
}

std::vector<PendingMessage> Storage::LoadPendingMessagesOnly(const std::string& client_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_pending_messages_stmt_);
    sqlite3_bind_text(*select_pending_messages_stmt_, 1, client_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(*select_pending_messages_stmt_, 2, STATUS_PENDING);
    sqlite3_bind_int(*select_pending_messages_stmt_, 3, static_cast<int>(MessageType::Reply));
    
    std::vector<PendingMessage> messages;
    
    while (sqlite3_step(*select_pending_messages_stmt_) == SQLITE_ROW) {
        PendingMessage pending;
        pending.id = static_cast<uint64_t>(sqlite3_column_int64(*select_pending_messages_stmt_, 0));
        
        Message msg;
        msg.SetType(static_cast<MessageType>(sqlite3_column_int(*select_pending_messages_stmt_, 1)));
        msg.SetFlags(static_cast<uint8_t>(sqlite3_column_int(*select_pending_messages_stmt_, 2)));
        msg.SetCorrelationId(static_cast<uint64_t>(sqlite3_column_int64(*select_pending_messages_stmt_, 3)));
        
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(*select_pending_messages_stmt_, 4));
        if (sender) msg.SetSender(sender);
        
        const char* destination = reinterpret_cast<const char*>(sqlite3_column_text(*select_pending_messages_stmt_, 5));
        if (destination) msg.SetDestination(destination);
        
        const void* blob = sqlite3_column_blob(*select_pending_messages_stmt_, 6);
        int blob_size = sqlite3_column_bytes(*select_pending_messages_stmt_, 6);
        if (blob && blob_size > 0) {
            std::vector<uint8_t> payload(static_cast<const uint8_t*>(blob), 
                                          static_cast<const uint8_t*>(blob) + blob_size);
            msg.SetPayload(payload);
        }
        
        pending.msg = std::move(msg);
        messages.push_back(std::move(pending));
    }
    
    spdlog::debug("Loaded {} pending messages for client: {}", messages.size(), client_name);
    return messages;
}

std::vector<PendingMessage> Storage::LoadPendingRepliesForSenderOnly(const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    ResetStatement(*select_pending_replies_stmt_);
    sqlite3_bind_text(*select_pending_replies_stmt_, 1, sender_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(*select_pending_replies_stmt_, 2, STATUS_PENDING);
    sqlite3_bind_int(*select_pending_replies_stmt_, 3, STATUS_SENT);
    sqlite3_bind_int(*select_pending_replies_stmt_, 4, static_cast<int>(MessageType::Reply));
    
    std::vector<PendingMessage> replies;
    std::set<uint64_t> unique_ids;
    
    while (sqlite3_step(*select_pending_replies_stmt_) == SQLITE_ROW) {
        uint64_t id = static_cast<uint64_t>(sqlite3_column_int64(*select_pending_replies_stmt_, 0));
        
        if (unique_ids.find(id) != unique_ids.end()) {
            continue;
        }
        unique_ids.insert(id);
        
        PendingMessage pending;
        pending.id = id;
        
        Message msg;
        msg.SetType(static_cast<MessageType>(sqlite3_column_int(*select_pending_replies_stmt_, 1)));
        msg.SetFlags(static_cast<uint8_t>(sqlite3_column_int(*select_pending_replies_stmt_, 2)));
        msg.SetCorrelationId(static_cast<uint64_t>(sqlite3_column_int64(*select_pending_replies_stmt_, 3)));
        
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(*select_pending_replies_stmt_, 4));
        if (sender) msg.SetSender(sender);
        
        const char* destination = reinterpret_cast<const char*>(sqlite3_column_text(*select_pending_replies_stmt_, 5));
        if (destination) msg.SetDestination(destination);
        
        const void* blob = sqlite3_column_blob(*select_pending_replies_stmt_, 6);
        int blob_size = sqlite3_column_bytes(*select_pending_replies_stmt_, 6);
        if (blob && blob_size > 0) {
            std::vector<uint8_t> payload(static_cast<const uint8_t*>(blob), 
                                          static_cast<const uint8_t*>(blob) + blob_size);
            msg.SetPayload(payload);
        }
        
        pending.msg = std::move(msg);
        replies.push_back(std::move(pending));
    }
    
    spdlog::debug("Loaded {} unique pending replies for sender: {}", replies.size(), sender_name);
    return replies;
}

}