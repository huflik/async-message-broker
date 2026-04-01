#pragma once

#include "message.hpp"
#include "storage.hpp"
#include "session.hpp"

namespace broker {

class OfflineQueueManager {
public:
    explicit OfflineQueueManager(Storage& storage) : storage_(storage) {}
    
    void Enqueue(const std::string& client, const Message& msg) {
        (void)client;
        (void)msg;
    }
    
    void Deliver(const std::string& client, Session& session) {
        (void)client;
        (void)session;
    }

private:
    Storage& storage_;
};

} // namespace broker