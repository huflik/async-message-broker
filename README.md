[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Boost](https://img.shields.io/badge/Boost-1.82+-green.svg)](https://www.boost.org/)
[![ZeroMQ](https://img.shields.io/badge/ZeroMQ-4.3+-red.svg)](https://zeromq.org/)
[![SQLite](https://img.shields.io/badge/SQLite-3-blue.svg)](https://www.sqlite.org/)

Асинхронный брокер сообщений с гарантированной доставкой, поддержкой двухсторонней связи (request-reply) и персистентным хранением на диске. Реализован на C++ с использованием библиотек Boost.Asio и ZeroMQ.

## 📋 Техническое задание (сводка)

### Ключевые требования к проекту

| Категория | Требование |
|-----------|------------|
| **Архитектура** | Отдельный процесс (не библиотека) |
| **Транспорт** | ZeroMQ (ROUTER/DEALER) |
| **Сеть** | Boost.Asio (асинхронный I/O) |
| **Хранение** | SQLite (персистентность) |
| **Регистрация** | По логическим именам |
| **Маршрутизация** | По имени получателя |
| **Доставка** | Гарантированная (сохранение на диск) |
| **Офлайн-режим** | Отложенная доставка |
| **Двухсторонняя связь** | Request-reply с корреляцией |
| **Подтверждения** | Application-level ACK |
| **Протокол** | Бинарный (TLV) |
| **Клиенты** | C++ и Python примеры |
| **Тестирование** | Google Test |
| **Сборка** | CMake |

[📄 Полный текст технического задания](docs/technical-specification.md)

### Ключевые особенности:

- **Гарантированная доставка** — все сообщения сохраняются на диск и не теряются при отключении клиентов или самого брокера
- **Двухсторонняя связь** — поддержка паттерна request-reply с корреляцией сообщений
- **Отложенная доставка** — сообщения для офлайн-получателей хранятся и доставляются при их подключении
- **Асинхронность** — неблокирующая обработка всех операций
- **Масштабируемость** — пул потоков для обработки множества одновременных соединений

## ✨ Функциональные возможности

### Для клиентов

| Возможность | Описание |
|-------------|----------|
| **Регистрация** | Клиент подключается к брокеру и регистрируется под уникальным логическим именем |
| **Отправка сообщений** | Отправка сообщения любому зарегистрированному клиенту по имени |
| **Запрос-ответ** | Отправка сообщения с требованием ответа (флаг `NEEDS_REPLY`) |
| **Получение ответов** | Автоматическая маршрутизация ответов исходному отправителю |
| **Офлайн-режим** | Получение сообщений, отправленных во время отсутствия, при повторном подключении |

### Для брокера

| Возможность | Описание |
|-------------|----------|
| **Маршрутизация** | Доставка сообщений по логическим именам получателей |
| **Персистентность** | Сохранение всех сообщений в SQLite до подтверждения доставки |
| **Корреляция** | Связывание запросов и ответов через Correlation ID |
| **Управление сессиями** | Отслеживание подключённых клиентов и их статуса |
| **Восстановление** | Загрузка неотправленных сообщений при запуске |

## 🏗 Архитектура
### Контекст системы
<img width="466" height="496" alt="XP7FRjD04CRlUOebfoAbyOKJfr9VYHJ-SE5IEVOgjV2kezsre3TI2mgYIjy0NY4DH0sKEAymyqPyQrjAb0X8vDNiZERtpJTxObraNA6opeDKnrcHI77ktCG-3W8JlUsFKtTUZ0ehJPnh9xNhnxaA9bcaVS-nTKcMZeBmqV7GfruDdWOgiauQfCv2T4IfbgO_yVoEIrqcnOiBr9NmHVd" src="https://github.com/user-attachments/assets/76f55b1d-5a2d-408c-b5d7-ec4fec454ddb" />

### Контейнеры
<img width="480" height="1001" alt="hL9FRn9V5DttKxmVAnh-pEQtwgeo6cDIufIs6pVamJpXnVc3RzwaGMD2I_1FN13z01eNhaKi5g7WLxZl6tdVum6TshWmje7SkSmzvznpptuYgP1f6EpynwDcaFgCjANi97kE8-XfkSLbEssa2HFDE98iakLc73gTW4PwffH8F-0Dn_s_vEdMIVN02MaYcS1MtSK9oYCcoXsDlqCabm4" src="https://github.com/user-attachments/assets/81da3519-7775-4a30-9bee-3972efbafd93" />

### Основные компоненты
<img width="908" height="832" alt="XLJDRZCr5DttAIxPa4zVqsnOTLKrHGYf4VqH6pQHct4IIpDswl6e2WWf2IeLQaN52_0ZXDY6jA7_wIjuteXZPqXY64ggflQnxptdddlTdTGmRR8atlX0o7QSHPnwnlJJxNfTixEjhZ2zx2HBkMuhQRWqMssLrFinaowcbfee5YVrnaUr0mTztjol9omrN0DgDLJILzB5z5t-3ocJ24u" src="https://github.com/user-attachments/assets/3ce1b14d-2c67-4478-958c-49a8dc66ce94" />

## 📊 Диаграмма классов
```mermaid
classDiagram
    %% ========== ИНТЕРФЕЙСЫ ==========
    class IStorage {
        <<interface>>
        +SaveMessage(Message) uint64_t
        +MarkDelivered(uint64_t)
        +MarkSent(uint64_t)
        +NeedsAck(uint64_t) bool
        +MarkPending(uint64_t)
        +SaveCorrelation(uint64_t, uint64_t, string)
        +FindOriginalSenderByCorrelation(uint64_t) string
        +FindMessageIdByCorrelation(uint64_t) uint64_t
        +FindMessageIdByCorrelationAndDestination(uint64_t, string) uint64_t
        +MarkAckReceived(uint64_t, string)
        +LoadExpiredSent(int) vector~PendingMessage~
        +LoadPendingRepliesForSenderOnly(string) vector~PendingMessage~
        +LoadPendingMessagesOnly(string) vector~PendingMessage~
    }
    
    class IMessageSender {
        <<interface>>
        +SendToClient(zmq::message_t, zmq::message_t, callback)
    }
    
    class IConfigProvider {
        <<interface>>
        +GetConfig() Config
    }
    
    class ISessionManager {
        <<interface>>
        +FindSession(string) shared_ptr~Session~
        +RegisterClient(string, shared_ptr~Session~) bool
        +UnregisterClient(string)
        +PrintActiveClients()
        +CleanupInactiveSessions()
        +DeliverOfflineMessages(string)
        +DeliverPendingReplies(string)
        +PersistMessageForClient(string, Message)
        +CheckExpiredAcks()
    }
    
    class IMetrics {
        <<interface>>
        +IncrementMessagesReceived()
        +IncrementMessagesSent()
        +IncrementMessagesFailed()
        +IncrementAcksReceived()
        +IncrementMessagesExpired()
        +IncrementOfflineDelivered()
        +IncrementClientsRegistered()
        +IncrementClientsUnregistered()
        +IncrementClientsTimeout()
        +SetActiveSessions(int)
        +SetPendingSendQueueSize(size_t)
        +ObservePayloadSize(size_t)
        +AddMessageProcessingTime(double)
    }
    
    class IMessageHandler {
        <<interface>>
        +Handle(Message, HandlerContext)
    }
    
    %% ========== СТРУКТУРЫ ==========
    class Config {
        +int Port
        +string DbPath
        +int Threads
        +string LogLevel
        +int SessionTimeout
        +int AckTimeout
        +bool EnableMetrics
        +string MetricsBindAddress
        +int MetricsUpdateInterval
        +ParseArgs() Config$
        +PrintHelp()$
    }
    
    class PendingMessage {
        +uint64_t id
        +Message msg
    }
    
    class HandlerContext {
        +IStorage& storage
        +ISessionManager& session_manager
        +IMessageSender& message_sender
        +IConfigProvider& config_provider
        +zmq::message_t& identity
        +shared_ptr~IMetrics~ metrics
    }
    
    %% ========== ОСНОВНЫЕ КЛАССЫ ==========
    class Server {
        -Config config_
        -zmq::socket_t router_socket_
        -io_context io_context_
        -unique_ptr~Router~ router_
        -unique_ptr~Storage~ storage_
        -shared_ptr~MetricsManager~ metrics_manager_
        +Server(Config)
        +Run()
        +Stop()
        +SendToClient(zmq::message_t, zmq::message_t, callback)
        +GetConfig() Config
        +GetMetrics() shared_ptr~IMetrics~
    }
    
    class Router {
        -IStorage& storage_
        -IMessageSender& message_sender_
        -IConfigProvider& config_provider_
        -shared_ptr~IMetrics~ metrics_
        -unordered_map~string, shared_ptr~Session~~ active_clients_
        -mutex registry_mutex_
        +Router(IStorage&, IMessageSender&, IConfigProvider&, shared_ptr~IMetrics~)
        +RouteMessage(Message, zmq::message_t&)
        +FindSession(string) shared_ptr~Session~
        +RegisterClient(string, shared_ptr~Session~) bool
        +UnregisterClient(string)
        +PrintActiveClients()
        +CleanupInactiveSessions()
        +DeliverOfflineMessages(string)
        +DeliverPendingReplies(string)
        +PersistMessageForClient(string, Message)
        +CheckExpiredAcks()
    }
    
    class Storage {
        -sqlite3* db_
        -string db_path_
        -mutex db_mutex_
        +Storage(string)
        +~Storage()
        +SaveMessage(Message) uint64_t
        +MarkDelivered(uint64_t)
        +MarkSent(uint64_t)
        +NeedsAck(uint64_t) bool
        +MarkPending(uint64_t)
        +SaveCorrelation(uint64_t, uint64_t, string)
        +FindOriginalSenderByCorrelation(uint64_t) string
        +FindMessageIdByCorrelation(uint64_t) uint64_t
        +FindMessageIdByCorrelationAndDestination(uint64_t, string) uint64_t
        +MarkAckReceived(uint64_t, string)
        +LoadExpiredSent(int) vector~PendingMessage~
        +LoadPendingRepliesForSenderOnly(string) vector~PendingMessage~
        +LoadPendingMessagesOnly(string) vector~PendingMessage~
    }
    
    class Session {
        -zmq::message_t identity_
        -IMessageSender& message_sender_
        -string name_
        -bool is_online_
        -queue~Message~ outgoing_queue_
        -mutex queue_mutex_
        -time_point last_receive_
        -time_point last_activity_
        +Session(zmq::message_t, IMessageSender&, Config&)
        +~Session()
        +SendMessage(Message) bool
        +GetName() string
        +IsOnline() bool
        +GetIdentity() zmq::message_t&
        +SetName(string)
        +FlushQueue()
        +MarkOffline()
        +MarkOnline()
        +UpdateLastReceive()
        +UpdateLastActivity()
        +IsExpired(int) bool
        +PersistQueueToDatabase()
        +GetQueueSize() size_t
    }
    
    class Message {
        -MessageType type_
        -uint8_t flags_
        -uint64_t correlation_id_
        -string sender_
        -string destination_
        -vector~uint8_t~ payload_
        +Message()
        +Message(MessageType, uint8_t, uint64_t, string, string, vector~uint8_t~)
        +GetType() MessageType
        +GetFlags() uint8_t
        +GetCorrelationId() uint64_t
        +GetSender() string
        +GetDestination() string
        +GetPayload() vector~uint8_t~
        +SetType(MessageType)
        +SetFlags(uint8_t)
        +SetCorrelationId(uint64_t)
        +SetSender(string)
        +SetDestination(string)
        +SetPayload(vector~uint8_t~)
        +NeedsReply() bool
        +NeedsAck() bool
        +Serialize() vector~uint8_t~
        +Deserialize(vector~uint8_t~)$ Message
        +ToString() string
    }
    
    class MetricsManager {
        -atomic~uint64_t~ messages_received_
        -atomic~uint64_t~ messages_sent_
        -unique_ptr~Exposer~ exposer_
        +MetricsManager()
        +InitExposer(string)
        +StartUpdater(seconds)
        +StopUpdater()
        +IncrementMessagesReceived()
        +IncrementMessagesSent()
        +IncrementMessagesFailed()
        +IncrementAcksReceived()
        +IncrementMessagesExpired()
        +IncrementOfflineDelivered()
        +IncrementClientsRegistered()
        +IncrementClientsUnregistered()
        +IncrementClientsTimeout()
        +SetActiveSessions(int)
        +SetPendingSendQueueSize(size_t)
        +ObservePayloadSize(size_t)
        +AddMessageProcessingTime(double)
    }
    
    %% ========== ОБРАБОТЧИКИ ==========
    class RegisterHandler {
        +Handle(Message, HandlerContext)
    }
    
    class MessageHandler {
        +Handle(Message, HandlerContext)
    }
    
    class ReplyHandler {
        +Handle(Message, HandlerContext)
    }
    
    class AckHandler {
        +Handle(Message, HandlerContext)
    }
    
    class UnregisterHandler {
        +Handle(Message, HandlerContext)
    }
    
    class MessageHandlerFactory {
        <<static>>
        +Create(MessageType) unique_ptr~IMessageHandler~
    }
    
    %% ========== ПЕРЕЧИСЛЕНИЯ ==========
    class MessageType {
        <<enumeration>>
        Register = 1
        Message = 2
        Reply = 3
        Ack = 4
        Unregister = 5
    }
    
    class MessageFlag {
        <<enumeration>>
        FlagNone = 0
        FlagNeedsReply = 1
        FlagNeedsAck = 2
    }
    
    class MessageStatus {
        <<enumeration>>
        STATUS_PENDING = 0
        STATUS_DELIVERED = 1
        STATUS_SENT = 2
    }
    
    %% ========== ОТНОШЕНИЯ ==========
    Server ..|> IMessageSender
    Server ..|> IConfigProvider
    Server *-- Router
    Server *-- Storage
    Server *-- MetricsManager
    
    Storage ..|> IStorage
    Storage --> PendingMessage
    Storage --> Message
    
    Router ..|> ISessionManager
    Router --> IStorage
    Router --> IMessageSender
    Router --> IConfigProvider
    Router --> IMetrics
    Router *-- Session
    
    Session --> IMessageSender
    Session --> Config
    Session --> Message
    
    MetricsManager ..|> IMetrics
    
    IMessageHandler <|.. RegisterHandler
    IMessageHandler <|.. MessageHandler
    IMessageHandler <|.. ReplyHandler
    IMessageHandler <|.. AckHandler
    IMessageHandler <|.. UnregisterHandler
    
    MessageHandlerFactory --> IMessageHandler
    MessageHandlerFactory ..> MessageType
    
    HandlerContext --> IStorage
    HandlerContext --> ISessionManager
    HandlerContext --> IMessageSender
    HandlerContext --> IConfigProvider
    HandlerContext --> IMetrics
    
    Message --> MessageType
    Message --> MessageFlag
    Storage --> MessageStatus
```

## 🔄 Сценарии работы

### Сценарий 1: Отправка сообщения онлайн-получателю
```mermaid
sequenceDiagram
    participant Producer
    participant Broker
    participant Consumer
    participant Storage
    
    Producer->>Broker: Сообщение для Consumer (NEEDS_REPLY)
    
    Broker->>Storage: Сохранить сообщение
    Storage-->>Broker: OK, message_id
    
    Broker->>Broker: Проверка статуса Consumer
    Note over Broker: Consumer онлайн
    
    Broker->>Consumer: Пересылка сообщения
    
    Consumer->>Broker: Подтверждение получения (ACK)
    Broker->>Storage: Пометить как delivered
    
    Consumer->>Broker: Ответ (с Correlation ID)
    Broker->>Storage: Сохранить ответ
    
    Broker->>Producer: Пересылка ответа
    Producer->>Broker: ACK на ответ
    Broker->>Storage: Пометить ответ как delivered
```

### Сценарий 2: Отправка сообщения офлайн-получателю
```mermaid
sequenceDiagram
    participant Producer
    participant Broker
    participant Consumer
    participant Storage
    
    Producer->>Broker: Сообщение для Consumer
    
    Broker->>Storage: Сохранить сообщение (status=pending)
    Storage-->>Broker: OK
    
    Broker->>Broker: Проверка статуса Consumer
    Note over Broker: Consumer офлайн
    
    Broker->>Producer: Подтверждение приёма (сохранено)
    
    Note over Broker: Проходит время...
    
    Consumer->>Broker: Подключение / регистрация
    
    Broker->>Storage: Загрузить pending для Consumer
    Storage-->>Broker: [сообщение]
    
    Broker->>Consumer: Доставка сохранённого сообщения
    
    Consumer->>Broker: Подтверждение получения
    Broker->>Storage: Пометить как delivered
```

### Сценарий 3: Запрос-ответ с офлайн-отправителем
```mermaid
sequenceDiagram
    participant Producer
    participant Broker
    participant Consumer
    participant Storage
    
    Producer->>Broker: Запрос к Consumer (NEEDS_REPLY)
    
    Broker->>Storage: Сохранить запрос
    Broker->>Consumer: Доставка запроса
    
    Note over Producer: Producer отключается
    
    Consumer->>Broker: Ответ (с Correlation ID)
    
    Broker->>Broker: Producer офлайн
    Broker->>Storage: Сохранить ответ (status=pending)
    
    Note over Broker: Проходит время...
    
    Producer->>Broker: Переподключение / регистрация
    
    Broker->>Storage: Загрузить ответы для Producer
    Broker->>Producer: Доставка сохранённого ответа
```
### Интеграция ZeroMQ и Boost.Asio
Для объединения двух асинхронных библиотек в единый цикл событий используется следующий подход:

1. **Неблокирующий режим ZeroMQ**
   - ZMQ сокет переводится в неблокирующий режим
     
2. **Интеграция через файловый дескриптор (FD)**
   - ZMQ сокет предоставляет файловый дескриптор
   - Дескриптор оборачивается в `boost::asio::posix::stream_descriptor`
   - Асинхронное ожидание событий

3. **Цикл обработки событий**
   - При срабатывании триггера `ZMQ_POLLIN` вызывается колбэк
   - В колбэке сообщения вычитываются в цикле до тех пор, пока сокет не опустеет

4. **Многопоточная обработка**
   - Используется пул потоков, каждый из которых вызывает `io_context_.run()`
   - Все асинхронные операции (ZMQ, таймеры, callback'и) выполняются в контексте этих потоков
   - Синхронизация доступа к разделяемым данным осуществляется через мьютексы

## 🛠 Технологический стек

| Компонент | Технология |
|-----------|------------|
| **Язык** | C++17/20 |
| **Сеть** | Boost.Asio |
| **Транспорт** | ZeroMQ (libzmq + cppzmq) |
| **Хранение** | SQLite |
| **Логирование** | spdlog |
| **Сборка** | CMake |
| **Тестирование** | Google Test |

### Язык программирования: C++17/20
**Обоснование:**
- Требуется максимальная производительность
- C++ обеспечивает эффективную работу с сетью, памятью и многопоточностью, что критически важно для брокера сообщений.

### Сетевая библиотека: Boost.Asio
**Обоснование:**
- Стандарт де-факто для асинхронного сетевого программирования на C++
- Асинхронная модель (Proactor) идеально подходит для высоконагруженных I/O-приложений
- Кроссплатформенность
- Единый цикл событий (io_context) для всех асинхронных операций

### Транспорт и маршрутизация: ZeroMQ (libzmq + cppzmq)
**Обоснование:**
- Готовые паттерны ROUTER/DEALER для асинхронной маршрутизации
- Автоматическое управление соединениями и переподключением
- Фреймовая структура сообщений (удобно для заголовков и метаданных)
- Высокая производительность (ядро на C)

### Хранение данных: SQLite
**Обоснование:**
- Встраиваемая БД (не требует отдельного сервиса)
- Транзакционность (гарантия целостности при записи на диск)
- SQL для удобной выборки (поиск по корреляции)
- Надёжность (атомарная запись на диск)
- Минимальные накладные расходы

### Логирование: spdlog
**Обоснование:**
- Высокая производительность (асинхронные режимы)
- Простой и удобный API
- Гибкое форматирование

### Сборка: CMake
**Обоснование:**
- Стандарт для C++ проектов
- Удобное управление зависимостями (FetchContent, find_package)
- Кроссплатформенность

### Тестирование: Google Test
**Обоснование:**
- Фреймворк обеспечивает удобное тестирование ключевых компонентов (маршрутизация, корреляция, хранение).

## Структура проекта
```
broker/
├── cmake/                
├── include/               
│   └── broker/            
│       ├── server.hpp
│       ├── router.hpp
│       ├── message.hpp
│       └── storage.hpp
├── src/
│   ├── server.cpp
│   ├── router.cpp
│   ├── message.cpp
│   ├── storage.cpp
│   ├── zmq_gateway.cpp
│   └── main.cpp
├── libs/                   
├── tests/
├── examples/               
│   ├── cpp_client/
│   └── python_client/
├── docs/                   
├── CMakeLists.txt
└── README.md
```

## Сборка и установка

### Требования

- CMake 3.15+
- Компилятор с поддержкой C++17 (GCC 9+)
- Boost 1.82+ (Asio, Beast)
- ZeroMQ 4.3+ (libzmq, cppzmq)
- SQLite3
- spdlog

## [Документация API](docs/api-documentation.md)
