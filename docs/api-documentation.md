# Async Message Broker API Documentation

## Overview

Async Message Broker is a high-performance message broker with guaranteed delivery, using ZeroMQ for transport, SQLite for persistence, and Prometheus for metrics.

## Core Components

### Message Class

```cpp
#include "message.hpp"

namespace broker {

class Message {
public:
    // Constructors
    Message() = default;
    Message(MessageType type, uint8_t flags, uint64_t correlation_id,
            const std::string& sender, const std::string& destination,
            const std::vector<uint8_t>& payload);

    // Getters
    MessageType GetType() const noexcept;
    uint8_t GetFlags() const noexcept;
    uint64_t GetCorrelationId() const noexcept;
    const std::string& GetSender() const noexcept;
    const std::string& GetDestination() const noexcept;
    const std::vector<uint8_t>& GetPayload() const noexcept;

    // Setters
    void SetType(MessageType type);
    void SetFlags(uint8_t flags);
    void SetCorrelationId(uint64_t id);
    void SetSender(const std::string& sender);
    void SetDestination(const std::string& dest);
    void SetPayload(const std::vector<uint8_t>& payload);

    // Flag operations
    bool HasFlag(uint8_t flag) const noexcept;
    void SetFlag(uint8_t flag);
    void ClearFlag(uint8_t flag);
    bool NeedsReply() const noexcept;
    void SetNeedsReply(bool value);
    bool NeedsAck() const noexcept;
    void SetNeedsAck(bool value);


    std::vector<uint8_t> Serialize() const;
    static Message Deserialize(const std::vector<uint8_t>& data);


    std::string ToString() const;
};

} 
```

### Message Types

```cpp
enum class MessageType : uint8_t {
    Register = 1,     
    Message = 2,      
    Reply = 3,        
    Ack = 4,          
    Unregister = 5    
};

enum MessageFlag : uint8_t {
    FlagNone = 0,            
    FlagNeedsReply = 1 << 0, 
    FlagNeedsAck = 1 << 1    
};
```

### Server Class

```cpp
#include "server.hpp"

namespace broker {

class Server : public IMessageSender, public IConfigProvider, public IMetricsProvider {
public:
    explicit Server(const Config& config);
    ~Server();

    // Lifecycle
    void Run();
    void Stop();
    bool IsRunning() const noexcept;

    // IMessageSender
    void SendToClient(zmq::message_t identity, zmq::message_t data,
                      std::function<void(bool)> callback = nullptr) override;

    // IConfigProvider
    const Config& GetConfig() const noexcept override;

    // IMetricsProvider
    std::shared_ptr<IMetrics> GetMetrics() const override;
};

} // namespace broker
```

### Config Struct

```cpp
#include "config.hpp"

namespace broker {

struct Config {
    // Network
    int Port = 5555;

    // Storage
    std::string DbPath = "./broker.db";

    // Performance
    int Threads = 0;          // 0 = auto-detect CPU cores

    // Logging
    std::string LogLevel = "info";  // trace, debug, info, warn, error

    // Timeouts
    int SessionTimeout = 60;   // seconds
    int AckTimeout = 30;        // seconds

    // Metrics
    bool EnableMetrics = true;
    std::string MetricsBindAddress = "0.0.0.0:8080";
    int MetricsUpdateInterval = 2;  // seconds

    // Methods
    static Config ParseArgs(int argc, char* argv[]);
    static void PrintHelp(const char* program_name);
};

} // namespace broker
```

### Storage Interface

```cpp
#include "interfaces.hpp"

namespace broker {

struct PendingMessage {
    uint64_t id;
    Message msg;
};

class IStorage {
public:
    virtual ~IStorage() = default;

    // Message operations
    virtual uint64_t SaveMessage(const Message& msg) = 0;
    virtual void MarkDelivered(uint64_t message_id) = 0;
    virtual void MarkSent(uint64_t message_id) = 0;
    virtual void MarkPending(uint64_t message_id) = 0;
    virtual bool NeedsAck(uint64_t message_id) = 0;

    // Correlation operations
    virtual void SaveCorrelation(uint64_t message_id, uint64_t correlation_id,
                                  const std::string& original_sender) = 0;
    virtual std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) = 0;
    virtual uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) = 0;
    virtual uint64_t FindMessageIdByCorrelationAndDestination(
        uint64_t correlation_id, const std::string& destination) = 0;
    virtual void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) = 0;

    // Loading operations
    virtual std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) = 0;
    virtual std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(
        const std::string& sender_name) = 0;
    virtual std::vector<PendingMessage> LoadPendingMessagesOnly(
        const std::string& client_name) = 0;
};

} // namespace broker
```

### Session Manager Interface

```cpp
#include "interfaces.hpp"

namespace broker {

class ISessionManager {
public:
    virtual ~ISessionManager() = default;

    // Session management
    virtual std::shared_ptr<Session> FindSession(const std::string& name) = 0;
    virtual bool RegisterClient(const std::string& name,
                                 std::shared_ptr<Session> session) = 0;
    virtual void UnregisterClient(const std::string& name) = 0;
    virtual void CleanupInactiveSessions() = 0;

    // Message delivery
    virtual void DeliverOfflineMessages(const std::string& name) = 0;
    virtual void DeliverPendingReplies(const std::string& name) = 0;
    virtual void PersistMessageForClient(const std::string& client_name,
                                          const Message& msg) = 0;
    virtual void CheckExpiredAcks() = 0;

    // Utility
    virtual void PrintActiveClients() = 0;
};

} // namespace broker
```

### Session Class

```cpp
#include "session.hpp"

namespace broker {

class Session {
public:
    explicit Session(zmq::message_t identity, IMessageSender& message_sender,
                     const Config& config);
    ~Session();

    // Message sending
    bool SendMessage(const Message& msg);
    void FlushQueue();

    // Status
    std::string GetName() const noexcept;
    bool IsOnline() const noexcept;
    size_t GetQueueSize() const noexcept;
    const zmq::message_t& GetIdentity() const noexcept;

    // State management
    void SetName(const std::string& name);
    void MarkOffline() noexcept;
    void MarkOnline() noexcept;

    // Activity tracking
    void UpdateLastReceive() noexcept;
    void UpdateLastActivity() noexcept;
    bool IsExpired(int timeout_seconds) const noexcept;

    // Persistence
    void PersistQueueToDatabase();
    void SetPersistCallback(std::function<void(const std::string&, const Message&)> callback);
};

} // namespace broker
```

### Metrics Interface

```cpp
#include "metrics.hpp"

namespace broker {

class IMetrics {
public:
    virtual ~IMetrics() = default;

    // Message counters
    virtual void IncrementMessagesReceived() = 0;
    virtual void IncrementMessagesSent() = 0;
    virtual void IncrementMessagesFailed() = 0;
    virtual void IncrementAcksReceived() = 0;
    virtual void IncrementMessagesExpired() = 0;
    virtual void IncrementOfflineDelivered() = 0;

    // Client counters
    virtual void IncrementClientsRegistered() = 0;
    virtual void IncrementClientsUnregistered() = 0;
    virtual void IncrementClientsTimeout() = 0;

    // Gauges
    virtual void SetActiveSessions(int count) = 0;
    virtual void SetPendingSendQueueSize(size_t size) = 0;

    // Histograms
    virtual void ObservePayloadSize(size_t bytes) = 0;
    virtual void AddMessageProcessingTime(double seconds) = 0;
};

class MetricsManager : public IMetrics {
public:
    explicit MetricsManager();
    ~MetricsManager();

    void InitExposer(const std::string& bind_address = "0.0.0.0:8080");
    void StartUpdater(std::chrono::seconds interval = std::chrono::seconds(2));
    void StopUpdater();

    // IMetrics implementation
    void IncrementMessagesReceived() override;
    void IncrementMessagesSent() override;
    void IncrementMessagesFailed() override;
    void IncrementAcksReceived() override;
    void IncrementMessagesExpired() override;
    void IncrementOfflineDelivered() override;
    void IncrementClientsRegistered() override;
    void IncrementClientsUnregistered() override;
    void IncrementClientsTimeout() override;
    void SetActiveSessions(int count) override;
    void SetPendingSendQueueSize(size_t size) override;
    void ObservePayloadSize(size_t bytes) override;
    void AddMessageProcessingTime(double seconds) override;
};

class ScopedMetricsTimer {
public:
    explicit ScopedMetricsTimer(std::shared_ptr<IMetrics> metrics);
    ~ScopedMetricsTimer();
};

} // namespace broker
```

### Message Sender Interface

```cpp
#include "interfaces.hpp"

namespace broker {

class IMessageSender {
public:
    virtual ~IMessageSender() = default;

    virtual void SendToClient(zmq::message_t identity, zmq::message_t data,
                              std::function<void(bool)> callback = nullptr) = 0;
};

} // namespace broker
```

### Config Provider Interface

```cpp
#include "interfaces.hpp"

namespace broker {

class IConfigProvider {
public:
    virtual ~IConfigProvider() = default;
    virtual const Config& GetConfig() const = 0;
};

} // namespace broker
```

### Universal Client

```cpp
// Command line usage
// universal_client <broker_address> <client_name> [--debug]

class UniversalClient {
public:
    UniversalClient(const std::string& broker_address, const std::string& client_name);
    ~UniversalClient();

    void Run();   // Starts interactive shell
    void Stop();  // Stops client

private:
    // Internal methods
    void Register();
    void Unregister();
    void SendMessage(const std::string& dest, const std::string& text,
                     bool needs_reply, bool needs_ack);
    void SendReply(const Message& original, const std::string& text);
    void SendManualReply(uint64_t correlation_id, const std::string& text);
    void SendAck(uint64_t correlation_id, const std::string& original_sender);
    void SendRawMessage(const Message& msg);
    void ReceiverLoop();
    void HandleMessage(const Message& msg);
    void ProcessCommand(const std::string& line);
    void PrintHelp();
    void ShowStatus();
    uint64_t GenerateCorrelationId();
};
```

## Usage Examples

### Starting the Broker

```cpp
#include "server.hpp"
#include "config.hpp"

int main() {
    broker::Config config;
    config.Port = 5555;
    config.DbPath = "/var/lib/broker/broker.db";
    config.Threads = 4;
    config.SessionTimeout = 60;
    config.AckTimeout = 30;

    broker::Server server(config);
    server.Run();  // Blocks until Stop() is called

    return 0;
}
```

### Using Command Line

```bash
# Start broker with default settings
./broker

# Start broker with custom port and database
./broker --port 5556 --db-path /data/broker.db

# Start broker with 8 threads and debug logging
./broker --threads 8 --log-level debug

# Start broker with metrics disabled
./broker --disable-metrics

# Show help
./broker --help
```

### Universal Client Examples

```bash
# Start client as 'alice'
./universal_client tcp://localhost:5555 alice

# Start client with debug mode
./universal_client tcp://localhost:5555 bob --debug

# Interactive commands (once client is running)
[alice] > send bob Hello Bob!
[alice] > send_ack bob Important message requiring ACK
[alice] > request bob Please reply to this
[alice] > reply 12345 This is my response
[alice] > status
[alice] > quit
```

### Sending a Message Programmatically

```cpp
#include "message.hpp"

// Create a message
broker::Message msg;
msg.SetType(broker::MessageType::Message);
msg.SetNeedsAck(true);
msg.SetCorrelationId(12345);
msg.SetSender("alice");
msg.SetDestination("bob");
msg.SetPayload({0x48, 0x65, 0x6c, 0x6c, 0x6f});  // "Hello"

// Serialize and send via ZeroMQ
auto serialized = msg.Serialize();
zmq::message_t zmq_msg(serialized.data(), serialized.size());
socket.send(zmq_msg);
```

### Receiving a Message

```cpp
// Receive message
zmq::message_t zmq_msg;
socket.recv(zmq_msg);

// Deserialize
std::vector<uint8_t> data(
    static_cast<uint8_t*>(zmq_msg.data()),
    static_cast<uint8_t*>(zmq_msg.data()) + zmq_msg.size()
);

auto msg = broker::Message::Deserialize(data);

// Process based on type
switch (msg.GetType()) {
    case broker::MessageType::Message:
        std::cout << "Received: " << msg.PayloadToString() << std::endl;
        if (msg.NeedsAck()) {
            // Send ACK
        }
        break;
    case broker::MessageType::Reply:
        std::cout << "Reply: " << msg.PayloadToString() << std::endl;
        break;
    case broker::MessageType::Ack:
        std::cout << "ACK received for ID: " << msg.GetCorrelationId() << std::endl;
        break;
    default:
        break;
}
```

### Metrics Endpoint

```bash
# Access Prometheus metrics
curl http://localhost:8080/metrics

# Example output:
# broker_messages_total{type="received"} 1234
# broker_messages_total{type="sent"} 1200
# broker_messages_total{type="failed"} 34
# broker_state{state="active_sessions"} 5
# broker_message_processing_duration_seconds_bucket{le="0.001"} 100
```

## Error Handling

```cpp
#include <stdexcept>

try {
    broker::Server server(config);
    server.Run();
} catch (const broker::ConfigError& e) {
    std::cerr << "Configuration error: " << e.what() << std::endl;
} catch (const broker::HelpRequested&) {
    // Help was printed, exit gracefully
} catch (const zmq::error_t& e) {
    std::cerr << "ZeroMQ error: " << e.what() << std::endl;
} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

## Build Configuration

### CMake Options

```cmake
# Enable/disable tests
option(WITH_GTEST "Build tests" ON)

# Enable/disable examples
option(BUILD_EXAMPLES "Build example clients" ON)

# Enable/disable metrics (auto-detected)
# BROKER_ENABLE_METRICS - defined when prometheus-cpp is found
```

### Required Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install libzmq3-dev libboost-system-dev libsqlite3-dev libspdlog-dev

# For metrics (optional)
# Build prometheus-cpp from source
```


