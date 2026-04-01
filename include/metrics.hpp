#pragma once

#include <atomic>
#include <string>

namespace broker {

class MetricsCollector {
public:
    void IncrementReceived(size_t bytes = 0) {
        messages_received_++;
        bytes_received_ += bytes;
    }
    
    void IncrementDelivered(size_t bytes = 0) {
        messages_delivered_++;
        bytes_delivered_ += bytes;
    }
    
    void SetPending(uint64_t count) {
        messages_pending_ = count;
    }
    
    void SetActiveClients(uint64_t count) {
        active_clients_ = count;
    }
    
    std::string GetJsonStats() const { return "{}"; }

private:
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_delivered_{0};
    std::atomic<uint64_t> messages_pending_{0};
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> bytes_delivered_{0};
    std::atomic<uint64_t> active_clients_{0};
};

} // namespace broker