#include "storage.hpp"
#include <spdlog/spdlog.h>

namespace broker {

Storage::Storage(const std::string& db_path)
    : db_path_(db_path)
{
    spdlog::info("Storage initialized with path: {}", db_path);
}

Storage::~Storage() {
}

uint64_t Storage::SaveMessage(const Message& msg) {
    spdlog::debug("Storage::SaveMessage (stub)");
    return 0;
}

std::vector<Message> Storage::LoadPending(const std::string& client_name) {
    spdlog::debug("Storage::LoadPending for {} (stub)", client_name);
    return {};
}

void Storage::MarkDelivered(uint64_t message_id) {
    spdlog::debug("Storage::MarkDelivered for {} (stub)", message_id);
}

void Storage::SaveCorrelation(uint64_t message_id, uint64_t correlation_id) {
    spdlog::debug("Storage::SaveCorrelation (stub)");
}

Message Storage::FindByCorrelation(uint64_t correlation_id) {
    spdlog::debug("Storage::FindByCorrelation (stub)");
    return Message();
}

void Storage::CreateTables() {
}

void Storage::CheckError(int rc, const std::string& msg) {
    (void)rc;
    (void)msg;
}

} // namespace broker