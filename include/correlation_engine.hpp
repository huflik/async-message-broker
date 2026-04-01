#pragma once

#include "message.hpp"

namespace broker {

class CorrelationEngine {
public:
    void RegisterRequest(uint64_t correlation_id, const std::string& original_sender) {
        (void)correlation_id;
        (void)original_sender;
    }
    
    void HandleResponse(const Message& response) {
        (void)response;
    }
};

} // namespace broker