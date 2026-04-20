#include "metrics.hpp"
#include <spdlog/spdlog.h>

#ifdef BROKER_ENABLE_METRICS

namespace broker {

MetricsManager::MetricsManager() {
    registry_ = std::make_shared<prometheus::Registry>();
    InitializePrometheusMetrics();
    spdlog::info("Metrics manager initialized");
}

MetricsManager::~MetricsManager() {
    StopUpdater();
    spdlog::debug("Metrics manager destroyed");
}

void MetricsManager::InitializePrometheusMetrics() {
    auto& message_counter_family = prometheus::BuildCounter()
                                      .Name("broker_messages_total")
                                      .Help("Broker message counters")
                                      .Register(*registry_);
    
    counters_["messages_received"] = &message_counter_family.Add({{"type", "received"}});
    counters_["messages_sent"] = &message_counter_family.Add({{"type", "sent"}});
    counters_["messages_failed"] = &message_counter_family.Add({{"type", "failed"}});
    counters_["acks_received"] = &message_counter_family.Add({{"type", "ack"}});
    counters_["messages_expired"] = &message_counter_family.Add({{"type", "expired"}});
    counters_["offline_delivered"] = &message_counter_family.Add({{"type", "offline"}});
    
    auto& client_counter_family = prometheus::BuildCounter()
                                     .Name("broker_clients_total")
                                     .Help("Broker client counters")
                                     .Register(*registry_);
    
    counters_["clients_registered"] = &client_counter_family.Add({{"type", "register"}});
    counters_["clients_unregistered"] = &client_counter_family.Add({{"type", "unregister"}});
    counters_["clients_timeout"] = &client_counter_family.Add({{"type", "timeout"}});
    
    auto& gauge_family = prometheus::BuildGauge()
                            .Name("broker_state")
                            .Help("Broker state gauges")
                            .Register(*registry_);
    
    gauges_["active_sessions"] = &gauge_family.Add({{"state", "active_sessions"}});
    gauges_["pending_send_queue"] = &gauge_family.Add({{"state", "pending_send_queue"}});
    
    auto& processing_histogram_family = prometheus::BuildHistogram()
                                           .Name("broker_message_processing_duration_seconds")
                                           .Help("Message processing duration in seconds")
                                           .Register(*registry_);
    
    processing_time_histogram_ = &processing_histogram_family.Add(
        {{"operation", "message_processing"}}, 
        prometheus::Histogram::BucketBoundaries{0.0001, 0.001, 0.01, 0.1, 1.0}
    );
    
    auto& payload_histogram_family = prometheus::BuildHistogram()
                                        .Name("broker_payload_size_bytes")
                                        .Help("Message payload size in bytes")
                                        .Register(*registry_);
    
    payload_size_histogram_ = &payload_histogram_family.Add(
        {{"type", "payload"}}, 
        prometheus::Histogram::BucketBoundaries{64, 256, 1024, 4096, 16384, 65536}
    );
    
    spdlog::info("Prometheus metrics initialized");
}

void MetricsManager::InitExposer(const std::string& bind_address) {
    try {
        exposer_ = std::make_unique<prometheus::Exposer>(bind_address);
        exposer_->RegisterCollectable(registry_);
        spdlog::info("Metrics exposer started on {}", bind_address);
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize metrics exposer: {}", e.what());
        throw;
    }
}

void MetricsManager::StartUpdater(std::chrono::seconds interval) {
    if (updater_running_) return;
    
    update_interval_ = interval;
    updater_running_ = true;
    
    updater_thread_ = std::make_unique<std::thread>([this]() {
        spdlog::info("Metrics updater thread started");
        while (updater_running_) {
            std::this_thread::sleep_for(update_interval_);
            if (updater_running_) UpdateMetrics();
        }
        spdlog::info("Metrics updater thread stopped");
    });
}

void MetricsManager::StopUpdater() {
    updater_running_ = false;
    if (updater_thread_ && updater_thread_->joinable()) {
        updater_thread_->join();
    }
    updater_thread_.reset();
}

void MetricsManager::UpdateMetrics() {
    try {
        counters_["messages_received"]->Increment(messages_received_.exchange(0));
        counters_["messages_sent"]->Increment(messages_sent_.exchange(0));
        counters_["messages_failed"]->Increment(messages_failed_.exchange(0));
        counters_["acks_received"]->Increment(acks_received_.exchange(0));
        counters_["messages_expired"]->Increment(messages_expired_.exchange(0));
        counters_["offline_delivered"]->Increment(offline_delivered_.exchange(0));
        
        counters_["clients_registered"]->Increment(clients_registered_.exchange(0));
        counters_["clients_unregistered"]->Increment(clients_unregistered_.exchange(0));
        counters_["clients_timeout"]->Increment(clients_timeout_.exchange(0));
        
        gauges_["active_sessions"]->Set(static_cast<double>(active_sessions_.load()));
        gauges_["pending_send_queue"]->Set(static_cast<double>(pending_send_queue_.load()));
        
        spdlog::trace("Metrics updated");
    } catch (const std::exception& e) {
        spdlog::error("Failed to update metrics: {}", e.what());
    }
}

void MetricsManager::ObservePayloadSize(size_t bytes) {
    if (payload_size_histogram_) {
        payload_size_histogram_->Observe(static_cast<double>(bytes));
    }
}

void MetricsManager::AddMessageProcessingTime(double seconds) {
    if (processing_time_histogram_) {
        processing_time_histogram_->Observe(seconds);
    }
}

} 

#endif 