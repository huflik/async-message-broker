// include/metrics.hpp
#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <functional>

// Предполагаем что prometheus-cpp установлен
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/exposer.h>
#include <spdlog/spdlog.h>

namespace broker {

/**
 * Интерфейс для сбора метрик (для dependency injection)
 */
class IMetrics {
public:
    virtual ~IMetrics() = default;
    
    // Сообщения
    virtual void IncrementMessagesReceived() = 0;
    virtual void IncrementMessagesSent() = 0;
    virtual void IncrementMessagesFailed() = 0;
    virtual void IncrementAcksReceived() = 0;
    virtual void IncrementMessagesExpired() = 0;
    virtual void IncrementOfflineDelivered() = 0;
    virtual void AddMessageProcessingTime(double seconds) = 0;
    virtual void ObservePayloadSize(size_t bytes) = 0;
    
    // Клиенты
    virtual void IncrementClientsRegistered() = 0;
    virtual void IncrementClientsUnregistered() = 0;
    virtual void IncrementClientsTimeout() = 0;
    virtual void SetActiveConnections(int count) = 0;
    virtual void SetActiveSessions(int count) = 0;
    
    // База данных
    virtual void IncrementDbWrites() = 0;
    virtual void IncrementDbReads() = 0;
    virtual void IncrementDbErrors() = 0;
    
    // Очереди
    virtual void SetPendingSendQueueSize(size_t size) = 0;
    virtual void SetTotalQueuedMessages(size_t size) = 0;
};

/**
 * Структура для хранения атомарных метрик
 */
struct MetricsData {
    std::atomic<uint64_t> messages_received_total{0};
    std::atomic<uint64_t> messages_sent_total{0};
    std::atomic<uint64_t> messages_failed_total{0};
    std::atomic<uint64_t> acks_received_total{0};
    std::atomic<uint64_t> messages_expired_total{0};
    std::atomic<uint64_t> offline_messages_delivered_total{0};
    
    std::atomic<uint64_t> clients_registered_total{0};
    std::atomic<uint64_t> clients_unregistered_total{0};
    std::atomic<uint64_t> clients_timeout_total{0};
    std::atomic<int64_t> active_connections{0};
    std::atomic<int64_t> active_sessions{0};
    
    std::atomic<uint64_t> db_write_ops_total{0};
    std::atomic<uint64_t> db_read_ops_total{0};
    std::atomic<uint64_t> db_errors_total{0};
    
    std::atomic<int64_t> pending_send_queue_size{0};
    std::atomic<int64_t> total_queued_messages{0};
};

/**
 * Основной менеджер метрик
 */
class MetricsManager : public IMetrics {
public:
    explicit MetricsManager();
    ~MetricsManager();
    
    // Инициализация HTTP экспортера
    void InitExposer(const std::string& bind_address = "0.0.0.0:8080");
    
    // Запуск фонового потока обновления
    void StartUpdater(std::chrono::seconds interval = std::chrono::seconds(2));
    void StopUpdater();
    
    // Реализация IMetrics
    void IncrementMessagesReceived() override { data_.messages_received_total++; }
    void IncrementMessagesSent() override { data_.messages_sent_total++; }
    void IncrementMessagesFailed() override { data_.messages_failed_total++; }
    void IncrementAcksReceived() override { data_.acks_received_total++; }
    void IncrementMessagesExpired() override { data_.messages_expired_total++; }
    void IncrementOfflineDelivered() override { data_.offline_messages_delivered_total++; }
    
    void AddMessageProcessingTime(double seconds) override;
    void ObservePayloadSize(size_t bytes) override;
    
    void IncrementClientsRegistered() override { data_.clients_registered_total++; }
    void IncrementClientsUnregistered() override { data_.clients_unregistered_total++; }
    void IncrementClientsTimeout() override { data_.clients_timeout_total++; }
    void SetActiveConnections(int count) override { data_.active_connections = count; }
    void SetActiveSessions(int count) override { data_.active_sessions = count; }
    
    void IncrementDbWrites() override { data_.db_write_ops_total++; }
    void IncrementDbReads() override { data_.db_read_ops_total++; }
    void IncrementDbErrors() override { data_.db_errors_total++; }
    
    void SetPendingSendQueueSize(size_t size) override { data_.pending_send_queue_size = static_cast<int64_t>(size); }
    void SetTotalQueuedMessages(size_t size) override { data_.total_queued_messages = static_cast<int64_t>(size); }
    
    // Доступ к сырым данным (для тестов)
    const MetricsData& GetData() const { return data_; }
    
private:
    void UpdateMetrics();
    void InitializePrometheusMetrics();
    
private:
    MetricsData data_;
    
    std::unique_ptr<prometheus::Exposer> exposer_;
    std::shared_ptr<prometheus::Registry> registry_;
    
    // Указатели на конкретные метрики (без Family, так как они разные)
    std::map<std::string, prometheus::Counter*> counters_;
    std::map<std::string, prometheus::Gauge*> gauges_;
    prometheus::Histogram* processing_time_histogram_ = nullptr;
    prometheus::Histogram* payload_size_histogram_ = nullptr;
    
    // Фоновый поток обновления
    std::atomic<bool> updater_running_{false};
    std::unique_ptr<std::thread> updater_thread_;
    std::chrono::seconds update_interval_{2};
};

/**
 * RAII таймер для замера времени обработки
 */
class ScopedMetricsTimer {
public:
    explicit ScopedMetricsTimer(std::shared_ptr<IMetrics> metrics)
        : metrics_(metrics)
        , start_(std::chrono::steady_clock::now()) {}
    
    ~ScopedMetricsTimer() {
        if (auto m = metrics_.lock()) {
            auto duration = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start_);
            m->AddMessageProcessingTime(duration.count());
        }
    }
    
private:
    std::weak_ptr<IMetrics> metrics_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace broker