#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include "message.hpp"

namespace broker {

/**
 * Структура для отложенного сообщения с ID
 */
struct PendingMessage {
    uint64_t id;
    Message msg;
};

class Storage {
public:
    explicit Storage(const std::string& db_path);
    ~Storage();
    
    uint64_t SaveMessage(const Message& msg);
    
    /**
     * Загружает все отложенные сообщения для клиента с их ID
     */
    std::vector<PendingMessage> LoadPending(const std::string& client_name);
    
    void MarkDelivered(uint64_t message_id);
    void SaveCorrelation(uint64_t message_id, uint64_t correlation_id);
    Message FindByCorrelation(uint64_t correlation_id);
    uint64_t GetPendingCount(const std::string& client_name);
    void CleanupOldMessages(int days = 7);

private:
    void CreateTables();
    void CheckError(int rc, const std::string& msg);
    
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex db_mutex_;
};

} // namespace broker