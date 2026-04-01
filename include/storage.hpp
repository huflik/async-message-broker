#pragma once

#include <string>
#include <vector>
#include "message.hpp"

namespace broker {

class Storage {
public:
    explicit Storage(const std::string& db_path);
    ~Storage();
    
    uint64_t SaveMessage(const Message& msg);
    std::vector<Message> LoadPending(const std::string& client_name);
    void MarkDelivered(uint64_t message_id);
    void SaveCorrelation(uint64_t message_id, uint64_t correlation_id);
    Message FindByCorrelation(uint64_t correlation_id);

private:
    void CreateTables();
    void CheckError(int rc, const std::string& msg);
    
    void* db_ = nullptr;
    std::string db_path_;
};

} // namespace broker