#include "storage.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

namespace broker {

namespace {
    constexpr int STATUS_PENDING = 0;
    constexpr int STATUS_DELIVERED = 1;
}

Storage::Storage(const std::string& db_path)
    : db_path_(db_path)
{
    spdlog::info("Opening database: {}", db_path);
    
    int rc = sqlite3_open(db_path.c_str(), &db_);
    CheckError(rc, "Failed to open database");
    
    CreateTables();
    
    spdlog::info("Storage initialized");
}

Storage::~Storage() {
    if (db_) {
        sqlite3_close(db_);
        spdlog::debug("Database closed");
    }
}

void Storage::CreateTables() {
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
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
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
    
    const char* create_indexes_sql = R"(
        CREATE INDEX IF NOT EXISTS idx_messages_destination ON messages(destination);
        CREATE INDEX IF NOT EXISTS idx_messages_status ON messages(status);
        CREATE INDEX IF NOT EXISTS idx_correlations_corr_id ON correlations(correlation_id);
    )";
    
    char* errmsg = nullptr;
    
    int rc = sqlite3_exec(db_, create_messages_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string error = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create messages table: " + error);
    }
    
    rc = sqlite3_exec(db_, create_correlations_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string error = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create correlations table: " + error);
    }
    
    rc = sqlite3_exec(db_, create_indexes_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string error = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error("Failed to create indexes: " + error);
    }
    
    spdlog::debug("Database tables created/verified");
}

void Storage::CheckError(int rc, const std::string& msg) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE) {
        throw std::runtime_error(msg + ": " + sqlite3_errmsg(db_));
    }
}

uint64_t Storage::SaveMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        INSERT INTO messages (type, flags, correlation_id, sender, destination, payload, status)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare insert statement");
    
    sqlite3_bind_int(stmt, 1, static_cast<int>(msg.GetType()));
    sqlite3_bind_int(stmt, 2, msg.GetFlags());
    sqlite3_bind_int64(stmt, 3, msg.GetCorrelationId());
    sqlite3_bind_text(stmt, 4, msg.GetSender().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, msg.GetDestination().c_str(), -1, SQLITE_TRANSIENT);
    
    const auto& payload = msg.GetPayload();
    if (payload.empty()) {
        sqlite3_bind_null(stmt, 6);
    } else {
        sqlite3_bind_blob(stmt, 6, payload.data(), payload.size(), SQLITE_TRANSIENT);
    }
    
    sqlite3_bind_int(stmt, 7, STATUS_PENDING);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert message: " + std::string(sqlite3_errmsg(db_)));
    }
    
    uint64_t message_id = sqlite3_last_insert_rowid(db_);
    
    sqlite3_finalize(stmt);
    
    spdlog::debug("Message saved to database, id: {}", message_id);
    return message_id;
}

std::vector<PendingMessage> Storage::LoadPending(const std::string& client_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        SELECT id, type, flags, correlation_id, sender, destination, payload
        FROM messages
        WHERE destination = ? AND status = ?
        ORDER BY created_at ASC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare select statement");
    
    sqlite3_bind_text(stmt, 1, client_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    
    std::vector<PendingMessage> messages;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingMessage pending;
        pending.id = sqlite3_column_int64(stmt, 0);
        
        Message msg;
        msg.SetType(static_cast<MessageType>(sqlite3_column_int(stmt, 1)));
        msg.SetFlags(static_cast<uint8_t>(sqlite3_column_int(stmt, 2)));
        msg.SetCorrelationId(sqlite3_column_int64(stmt, 3));
        
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sender) msg.SetSender(sender);
        
        const char* destination = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (destination) msg.SetDestination(destination);
        
        const void* blob = sqlite3_column_blob(stmt, 6);
        int blob_size = sqlite3_column_bytes(stmt, 6);
        if (blob && blob_size > 0) {
            std::vector<uint8_t> payload(static_cast<const uint8_t*>(blob), 
                                          static_cast<const uint8_t*>(blob) + blob_size);
            msg.SetPayload(payload);
        }
        
        pending.msg = std::move(msg);
        messages.push_back(std::move(pending));
    }
    
    sqlite3_finalize(stmt);
    
    spdlog::debug("Loaded {} pending messages for client: {}", messages.size(), client_name);
    return messages;
}

void Storage::MarkDelivered(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = "UPDATE messages SET status = ? WHERE id = ?";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare update statement");
    
    sqlite3_bind_int(stmt, 1, STATUS_DELIVERED);
    sqlite3_bind_int64(stmt, 2, message_id);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to mark message as delivered: " + 
                                 std::string(sqlite3_errmsg(db_)));
    }
    
    sqlite3_finalize(stmt);
    spdlog::debug("Message {} marked as delivered", message_id);
}

void Storage::SaveCorrelation(uint64_t message_id, uint64_t correlation_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        INSERT INTO correlations (message_id, correlation_id, original_sender)
        VALUES (?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare correlation insert statement");
    
    sqlite3_bind_int64(stmt, 1, message_id);
    sqlite3_bind_int64(stmt, 2, correlation_id);
    sqlite3_bind_text(stmt, 3, "", -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to save correlation: " + 
                                 std::string(sqlite3_errmsg(db_)));
    }
    
    sqlite3_finalize(stmt);
    spdlog::debug("Correlation saved: message_id={}, corr_id={}", message_id, correlation_id);
}

Message Storage::FindByCorrelation(uint64_t correlation_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        SELECT type, flags, correlation_id, sender, destination, payload
        FROM messages
        WHERE correlation_id = ? AND status = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare find by correlation statement");
    
    sqlite3_bind_int64(stmt, 1, correlation_id);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    
    Message msg;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        msg.SetType(static_cast<MessageType>(sqlite3_column_int(stmt, 0)));
        msg.SetFlags(static_cast<uint8_t>(sqlite3_column_int(stmt, 1)));
        msg.SetCorrelationId(sqlite3_column_int64(stmt, 2));
        
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sender) msg.SetSender(sender);
        
        const char* destination = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (destination) msg.SetDestination(destination);
        
        const void* blob = sqlite3_column_blob(stmt, 5);
        int blob_size = sqlite3_column_bytes(stmt, 5);
        if (blob && blob_size > 0) {
            std::vector<uint8_t> payload(static_cast<const uint8_t*>(blob), 
                                          static_cast<const uint8_t*>(blob) + blob_size);
            msg.SetPayload(payload);
        }
    }
    
    sqlite3_finalize(stmt);
    return msg;
}

uint64_t Storage::GetPendingCount(const std::string& client_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = "SELECT COUNT(*) FROM messages WHERE destination = ? AND status = ?";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare count statement");
    
    sqlite3_bind_text(stmt, 1, client_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    
    uint64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

void Storage::CleanupOldMessages(int days) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        DELETE FROM messages 
        WHERE status = ? AND created_at < datetime('now', ?)
    )";
    
    std::string date_modifier = "-" + std::to_string(days) + " days";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare cleanup statement");
    
    sqlite3_bind_int(stmt, 1, STATUS_DELIVERED);
    sqlite3_bind_text(stmt, 2, date_modifier.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to cleanup old messages: " + 
                                 std::string(sqlite3_errmsg(db_)));
    }
    
    int deleted = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    
    if (deleted > 0) {
        spdlog::info("Cleaned up {} old delivered messages", deleted);
    }
}

} // namespace broker