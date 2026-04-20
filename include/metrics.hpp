#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <thread>

#ifdef BROKER_ENABLE_METRICS
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/exposer.h>
#endif

#include <spdlog/spdlog.h>

namespace broker {

class IMetrics {
public:
    virtual ~IMetrics() = default;
    
    virtual void IncrementMessagesReceived() = 0;
    virtual void IncrementMessagesSent() = 0;
    virtual void IncrementMessagesFailed() = 0;
    virtual void ObservePayloadSize(size_t bytes) = 0;
    virtual void AddMessageProcessingTime(double seconds) = 0;
    
    virtual void IncrementClientsRegistered() = 0;
    virtual void IncrementClientsUnregistered() = 0;
    virtual void IncrementClientsTimeout() = 0;
    virtual void SetActiveSessions(int count) = 0;
    
    virtual void IncrementOfflineDelivered() = 0;
    virtual void IncrementMessagesExpired() = 0;
    
    virtual void IncrementAcksReceived() = 0;
    
    virtual void SetPendingSendQueueSize(size_t size) = 0;
};

#ifdef BROKER_ENABLE_METRICS

class MetricsManager : public IMetrics {
public:
    explicit MetricsManager();
    ~MetricsManager();
    
    void InitExposer(const std::string& bind_address = "0.0.0.0:8080");
    void StartUpdater(std::chrono::seconds interval = std::chrono::seconds(2));
    void StopUpdater();
    
    void IncrementMessagesReceived() override { messages_received_++; }
    void IncrementMessagesSent() override { messages_sent_++; }
    void IncrementMessagesFailed() override { messages_failed_++; }
    void ObservePayloadSize(size_t bytes) override;
    void AddMessageProcessingTime(double seconds) override;
    
    void IncrementClientsRegistered() override { clients_registered_++; }
    void IncrementClientsUnregistered() override { clients_unregistered_++; }
    void IncrementClientsTimeout() override { clients_timeout_++; }
    void SetActiveSessions(int count) override { active_sessions_ = count; }
    
    void IncrementOfflineDelivered() override { offline_delivered_++; }
    void IncrementMessagesExpired() override { messages_expired_++; }
    void IncrementAcksReceived() override { acks_received_++; }
    
    void SetPendingSendQueueSize(size_t size) override { pending_send_queue_ = static_cast<int64_t>(size); }
    
private:
    void UpdateMetrics();
    void InitializePrometheusMetrics();
    
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_failed_{0};
    std::atomic<uint64_t> acks_received_{0};
    std::atomic<uint64_t> messages_expired_{0};
    std::atomic<uint64_t> offline_delivered_{0};
    
    std::atomic<uint64_t> clients_registered_{0};
    std::atomic<uint64_t> clients_unregistered_{0};
    std::atomic<uint64_t> clients_timeout_{0};
    
    std::atomic<int64_t> active_sessions_{0};
    std::atomic<int64_t> pending_send_queue_{0};
    
    std::unique_ptr<prometheus::Exposer> exposer_;
    std::shared_ptr<prometheus::Registry> registry_;
    std::map<std::string, prometheus::Counter*> counters_;
    std::map<std::string, prometheus::Gauge*> gauges_;
    prometheus::Histogram* processing_time_histogram_ = nullptr;
    prometheus::Histogram* payload_size_histogram_ = nullptr;
    
    std::atomic<bool> updater_running_{false};
    std::unique_ptr<std::thread> updater_thread_;
    std::chrono::seconds update_interval_{2};
};

#else 
class MetricsManager : public IMetrics {
public:
    explicit MetricsManager() = default;
    ~MetricsManager() = default;
    
    void InitExposer(const std::string&) {}
    void StartUpdater(std::chrono::seconds = std::chrono::seconds(2)) {}
    void StopUpdater() {}
    
    void IncrementMessagesReceived() override {}
    void IncrementMessagesSent() override {}
    void IncrementMessagesFailed() override {}
    void ObservePayloadSize(size_t) override {}
    void AddMessageProcessingTime(double) override {}
    
    void IncrementClientsRegistered() override {}
    void IncrementClientsUnregistered() override {}
    void IncrementClientsTimeout() override {}
    void SetActiveSessions(int) override {}
    
    void IncrementOfflineDelivered() override {}
    void IncrementMessagesExpired() override {}
    void IncrementAcksReceived() override {}
    
    void SetPendingSendQueueSize(size_t) override {}
};

#endif 

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

}