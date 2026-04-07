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
    CheckError(rc, "Failed to open database");
    
    // Включаем WAL-режим для лучшей производительности и надёжности
    char* errmsg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("Failed to set WAL mode: {}", errmsg);
        sqlite3_free(errmsg);
    }
    
    // Устанавливаем таймаут ожидания блокировки
    rc = sqlite3_exec(db_, "PRAGMA busy_timeout=10000;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("Failed to set busy_timeout: {}", errmsg);
        sqlite3_free(errmsg);
    }
    
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
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            delivered_at TIMESTAMP
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
        CREATE INDEX IF NOT EXISTS idx_messages_type ON messages(type);
        CREATE INDEX IF NOT EXISTS idx_messages_dest_status ON messages(destination, status);
        CREATE INDEX IF NOT EXISTS idx_correlations_corr_id ON correlations(correlation_id);
        CREATE INDEX IF NOT EXISTS idx_correlations_original_sender ON correlations(original_sender);
        CREATE INDEX IF NOT EXISTS idx_correlations_corr_sender ON correlations(correlation_id, original_sender);
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
    return SaveMessageWithStatus(msg, STATUS_PENDING);
}

uint64_t Storage::SaveMessageWithStatus(const Message& msg, int status) {
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
    
    sqlite3_bind_int(stmt, 7, status);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert message: " + std::string(sqlite3_errmsg(db_)));
    }
    
    uint64_t message_id = sqlite3_last_insert_rowid(db_);
    
    sqlite3_finalize(stmt);
    
    spdlog::debug("Message saved to database, id: {}, status: {}", message_id, status);
    return message_id;
}

std::vector<PendingMessage> Storage::LoadPending(const std::string& client_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    // Загружаем сообщения со статусом PENDING или SENT (ещё не подтверждённые)
    const char* sql = R"(
        SELECT id, type, flags, correlation_id, sender, destination, payload
        FROM messages
        WHERE destination = ? AND status IN (?, ?)
        ORDER BY created_at ASC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare select statement");
    
    sqlite3_bind_text(stmt, 1, client_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    sqlite3_bind_int(stmt, 3, STATUS_SENT);
    
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

std::vector<PendingMessage> Storage::LoadPendingOnly(const std::string& client_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    // Загружаем только сообщения со статусом PENDING (не SENT)
    const char* sql = R"(
        SELECT id, type, flags, correlation_id, sender, destination, payload
        FROM messages
        WHERE destination = ? AND status = ?
        ORDER BY created_at ASC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to prepare select pending only statement: {}", sqlite3_errmsg(db_));
        return {};
    }
    
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
    
    spdlog::debug("Loaded {} pending (status=PENDING) messages for client: {}", messages.size(), client_name);
    return messages;
}

std::vector<PendingMessage> Storage::LoadPendingRepliesForSender(const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        SELECT m.id, m.type, m.flags, m.correlation_id, m.sender, m.destination, m.payload
        FROM messages m
        INNER JOIN correlations c ON m.correlation_id = c.correlation_id
        WHERE c.original_sender = ? 
          AND m.status IN (?, ?)
          AND m.type = ?
        ORDER BY m.created_at ASC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare select replies statement");
    
    sqlite3_bind_text(stmt, 1, sender_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    sqlite3_bind_int(stmt, 3, STATUS_SENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(MessageType::Reply));
    
    std::vector<PendingMessage> replies;
    
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
        replies.push_back(std::move(pending));
    }
    
    sqlite3_finalize(stmt);
    
    spdlog::debug("Loaded {} pending replies for sender: {}", replies.size(), sender_name);
    return replies;
}

std::vector<PendingMessage> Storage::LoadPendingRepliesForSenderOnly(const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    // Используем DISTINCT для исключения дубликатов
    const char* sql = R"(
        SELECT DISTINCT m.id, m.type, m.flags, m.correlation_id, m.sender, m.destination, m.payload
        FROM messages m
        INNER JOIN correlations c ON m.correlation_id = c.correlation_id
        WHERE c.original_sender = ? 
          AND m.status IN (?, ?)
          AND m.type = ?
        ORDER BY m.created_at ASC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to prepare select replies only statement: {}", sqlite3_errmsg(db_));
        return {};
    }
    
    sqlite3_bind_text(stmt, 1, sender_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    sqlite3_bind_int(stmt, 3, STATUS_SENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(MessageType::Reply));
    
    std::vector<PendingMessage> replies;
    std::set<uint64_t> unique_ids;  // для отслеживания уникальных ID
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint64_t id = sqlite3_column_int64(stmt, 0);
        
        // Пропускаем дубликаты
        if (unique_ids.find(id) != unique_ids.end()) {
            continue;
        }
        unique_ids.insert(id);
        
        PendingMessage pending;
        pending.id = id;
        
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
        replies.push_back(std::move(pending));
        
        spdlog::debug("Found pending reply id={} for sender {}", pending.id, sender_name);
    }
    
    sqlite3_finalize(stmt);
    
    spdlog::debug("Loaded {} unique pending replies for sender: {}", replies.size(), sender_name);
    return replies;
}

void Storage::MarkDelivered(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = "UPDATE messages SET status = ?, delivered_at = CURRENT_TIMESTAMP WHERE id = ?";
    
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
    
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    
    spdlog::debug("Message {} marked as delivered, {} rows affected", message_id, changes);
}

void Storage::MarkSent(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = "UPDATE messages SET status = ? WHERE id = ? AND status = ?";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare update statement");
    
    sqlite3_bind_int(stmt, 1, STATUS_SENT);
    sqlite3_bind_int64(stmt, 2, message_id);
    sqlite3_bind_int(stmt, 3, STATUS_PENDING);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to mark message as sent: " + 
                                 std::string(sqlite3_errmsg(db_)));
    }
    
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    
    spdlog::debug("Message {} marked as sent (waiting for ACK), {} rows affected", message_id, changes);
}

bool Storage::NeedsAck(uint64_t message_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = "SELECT flags FROM messages WHERE id = ?";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare select statement");
    
    sqlite3_bind_int64(stmt, 1, message_id);
    
    bool needs_ack = false;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        uint8_t flags = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
        needs_ack = (flags & FlagNeedsAck) != 0;
    }
    
    sqlite3_finalize(stmt);
    return needs_ack;
}

void Storage::SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        INSERT OR REPLACE INTO correlations (message_id, correlation_id, original_sender)
        VALUES (?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare correlation insert statement");
    
    sqlite3_bind_int64(stmt, 1, message_id);
    sqlite3_bind_int64(stmt, 2, correlation_id);
    sqlite3_bind_text(stmt, 3, original_sender.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to save correlation: " + 
                                 std::string(sqlite3_errmsg(db_)));
    }
    
    sqlite3_finalize(stmt);
    spdlog::debug("Correlation saved: message_id={}, corr_id={}, original_sender={}", 
                  message_id, correlation_id, original_sender);
}

std::string Storage::FindOriginalSenderByCorrelation(uint64_t correlation_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* sql = R"(
        SELECT original_sender 
        FROM correlations 
        WHERE correlation_id = ?
        ORDER BY created_at DESC
        LIMIT 1
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare find sender statement");
    
    sqlite3_bind_int64(stmt, 1, correlation_id);
    
    std::string original_sender;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (sender) {
            original_sender = sender;
        }
    }
    
    sqlite3_finalize(stmt);
    
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
    
    const char* sql = R"(
        SELECT message_id 
        FROM correlations 
        WHERE correlation_id = ?
        ORDER BY created_at DESC
        LIMIT 1
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to prepare find message_id statement: {}", sqlite3_errmsg(db_));
        return 0;
    }
    
    sqlite3_bind_int64(stmt, 1, correlation_id);
    
    uint64_t message_id = 0;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        message_id = sqlite3_column_int64(stmt, 0);
        spdlog::debug("Found message_id={} for correlation_id={}", message_id, correlation_id);
    } else {
        spdlog::debug("No message_id found for correlation_id={}", correlation_id);
    }
    
    sqlite3_finalize(stmt);
    return message_id;
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
    
    const char* sql = "SELECT COUNT(*) FROM messages WHERE destination = ? AND status IN (?, ?)";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    CheckError(rc, "Failed to prepare count statement");
    
    sqlite3_bind_text(stmt, 1, client_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, STATUS_PENDING);
    sqlite3_bind_int(stmt, 3, STATUS_SENT);
    
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