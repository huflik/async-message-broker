// src/metrics.cpp
#include "metrics.hpp"
#include <spdlog/spdlog.h>

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
    // Создаем ОТДЕЛЬНЫЕ семейства для каждого типа метрик с УНИКАЛЬНЫМИ именами
    
    // 1. Счетчики сообщений
    auto& message_counter_family = prometheus::BuildCounter()
                                      .Name("broker_messages_total")
                                      .Help("Broker message counters")
                                      .Register(*registry_);
    
    counters_["messages_received"] = &message_counter_family.Add({{"type", "received"}});
    counters_["messages_sent"] = &message_counter_family.Add({{"type", "sent"}});
    counters_["messages_failed"] = &message_counter_family.Add({{"type", "failed"}});
    counters_["acks_received"] = &message_counter_family.Add({{"type", "ack"}});
    counters_["messages_expired"] = &message_counter_family.Add({{"type", "expired"}});
    counters_["offline_delivered"] = &message_counter_family.Add({{"type", "offline_delivered"}});
    
    // 2. Счетчики клиентов
    auto& client_counter_family = prometheus::BuildCounter()
                                     .Name("broker_clients_total")
                                     .Help("Broker client counters")
                                     .Register(*registry_);
    
    counters_["clients_registered"] = &client_counter_family.Add({{"type", "register"}});
    counters_["clients_unregistered"] = &client_counter_family.Add({{"type", "unregister"}});
    counters_["clients_timeout"] = &client_counter_family.Add({{"type", "timeout"}});
    
    // 3. Счетчики базы данных
    auto& db_counter_family = prometheus::BuildCounter()
                                 .Name("broker_database_operations_total")
                                 .Help("Broker database operation counters")
                                 .Register(*registry_);
    
    counters_["db_writes"] = &db_counter_family.Add({{"type", "write"}});
    counters_["db_reads"] = &db_counter_family.Add({{"type", "read"}});
    counters_["db_errors"] = &db_counter_family.Add({{"type", "error"}});
    
    // 4. Gauges (состояние)
    auto& gauge_family = prometheus::BuildGauge()
                            .Name("broker_state")
                            .Help("Broker state gauges")
                            .Register(*registry_);
    
    gauges_["active_connections"] = &gauge_family.Add({{"state", "active_connections"}});
    gauges_["active_sessions"] = &gauge_family.Add({{"state", "active_sessions"}});
    gauges_["pending_send_queue"] = &gauge_family.Add({{"state", "pending_send_queue"}});
    gauges_["total_queued"] = &gauge_family.Add({{"state", "total_queued"}});
    
    // 5. Гистограмма времени обработки
    auto& processing_histogram_family = prometheus::BuildHistogram()
                                           .Name("broker_message_processing_duration_seconds")
                                           .Help("Message processing duration in seconds")
                                           .Register(*registry_);
    
    processing_time_histogram_ = &processing_histogram_family.Add(
        {{"operation", "message_processing"}}, 
        prometheus::Histogram::BucketBoundaries{0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0}
    );
    
    // 6. Гистограмма размера payload
    auto& payload_histogram_family = prometheus::BuildHistogram()
                                        .Name("broker_payload_size_bytes")
                                        .Help("Message payload size in bytes")
                                        .Register(*registry_);
    
    payload_size_histogram_ = &payload_histogram_family.Add(
        {{"type", "payload"}}, 
        prometheus::Histogram::BucketBoundaries{64, 128, 256, 512, 1024, 4096, 16384, 65536}
    );
    
    spdlog::info("Prometheus metrics initialized with unique names");
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
    if (updater_running_) {
        spdlog::warn("Metrics updater already running");
        return;
    }
    
    update_interval_ = interval;
    updater_running_ = true;
    
    updater_thread_ = std::make_unique<std::thread>([this]() {
        spdlog::info("Metrics updater thread started");
        
        while (updater_running_) {
            std::this_thread::sleep_for(update_interval_);
            if (updater_running_) {
                UpdateMetrics();
            }
        }
        
        spdlog::info("Metrics updater thread stopped");
    });
}

void MetricsManager::StopUpdater() {
    if (!updater_running_) return;
    
    updater_running_ = false;
    if (updater_thread_ && updater_thread_->joinable()) {
        updater_thread_->join();
    }
    updater_thread_.reset();
}

void MetricsManager::UpdateMetrics() {
    try {
        // Обновление счетчиков сообщений
        counters_["messages_received"]->Increment(data_.messages_received_total.exchange(0));
        counters_["messages_sent"]->Increment(data_.messages_sent_total.exchange(0));
        counters_["messages_failed"]->Increment(data_.messages_failed_total.exchange(0));
        counters_["acks_received"]->Increment(data_.acks_received_total.exchange(0));
        counters_["messages_expired"]->Increment(data_.messages_expired_total.exchange(0));
        counters_["offline_delivered"]->Increment(data_.offline_messages_delivered_total.exchange(0));
        
        // Обновление счетчиков клиентов
        counters_["clients_registered"]->Increment(data_.clients_registered_total.exchange(0));
        counters_["clients_unregistered"]->Increment(data_.clients_unregistered_total.exchange(0));
        counters_["clients_timeout"]->Increment(data_.clients_timeout_total.exchange(0));
        
        // Обновление счетчиков БД
        counters_["db_writes"]->Increment(data_.db_write_ops_total.exchange(0));
        counters_["db_reads"]->Increment(data_.db_read_ops_total.exchange(0));
        counters_["db_errors"]->Increment(data_.db_errors_total.exchange(0));
        
        // Обновление gauges
        gauges_["active_connections"]->Set(static_cast<double>(data_.active_connections.load()));
        gauges_["active_sessions"]->Set(static_cast<double>(data_.active_sessions.load()));
        gauges_["pending_send_queue"]->Set(static_cast<double>(data_.pending_send_queue_size.load()));
        gauges_["total_queued"]->Set(static_cast<double>(data_.total_queued_messages.load()));
        
        spdlog::trace("Metrics updated successfully");
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to update metrics: {}", e.what());
    }
}

void MetricsManager::AddMessageProcessingTime(double seconds) {
    if (processing_time_histogram_) {
        processing_time_histogram_->Observe(seconds);
    }
}

void MetricsManager::ObservePayloadSize(size_t bytes) {
    if (payload_size_histogram_) {
        payload_size_histogram_->Observe(static_cast<double>(bytes));
    }
}

} // namespace broker