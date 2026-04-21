# Документация API асинхронного брокера сообщений

## Обзор

Async Message Broker — это высокопроизводительный брокер сообщений с гарантированной доставкой, использующий ZeroMQ для транспорта, SQLite для персистентности и Prometheus для метрик.

---

## Спецификация протокола

Формат бинарного сообщения (15 байт заголовка + переменная длина):

| Смещение | Размер | Поле | Порядок байт |
|----------|--------|------|--------------|
| 0 | 1 | Версия (всегда 1) | - |
| 1 | 1 | Тип (1-5) | - |
| 2 | 1 | Флаги | - |
| 3 | 8 | Correlation ID | network (big-endian) |
| 11 | 1 | Длина имени отправителя (0-255) | - |
| 12 | 1 | Длина имени получателя (0-255) | - |
| 13 | 2 | Длина payload (0-65535) | network (big-endian) |
| 15 | N | Имя отправителя (UTF-8) | - |
| 15+N | M | Имя получателя (UTF-8) | - |
| 15+N+M | L | Payload (произвольные данные) | - |

---

## Основные компоненты

### Message (Сообщение)

Базовый класс сообщения, используемый для всех типов коммуникации.

#### Конструктор

    Message(
        MessageType type,
        uint8_t flags,
        uint64_t correlation_id,
        const std::string& sender,
        const std::string& destination,
        const std::vector<uint8_t>& payload
    );

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| MessageType GetType() const | Возвращает тип сообщения |
| uint8_t GetFlags() const | Возвращает флаги сообщения |
| uint64_t GetCorrelationId() const | Возвращает идентификатор корреляции |
| const std::string& GetSender() const | Возвращает имя отправителя |
| const std::string& GetDestination() const | Возвращает имя получателя |
| const std::vector<uint8_t>& GetPayload() const | Возвращает полезную нагрузку сообщения |
| void SetType(MessageType type) | Устанавливает тип сообщения |
| void SetFlags(uint8_t flags) | Устанавливает флаги сообщения |
| void SetCorrelationId(uint64_t id) | Устанавливает идентификатор корреляции |
| void SetSender(const std::string& sender) | Устанавливает имя отправителя |
| void SetDestination(const std::string& dest) | Устанавливает имя получателя |
| void SetPayload(const std::vector<uint8_t>& payload) | Устанавливает полезную нагрузку сообщения |
| bool NeedsReply() const | Возвращает true, если сообщение ожидает ответ |
| bool NeedsAck() const | Возвращает true, если сообщение требует подтверждения |
| std::vector<uint8_t> Serialize() const | Сериализует сообщение в массив байтов |
| static Message Deserialize(const std::vector<uint8_t>& data) | Десериализует сообщение из массива байтов |
| std::string ToString() const | Возвращает человекочитаемое представление |

#### Типы сообщений

| Значение | Имя | Описание |
| :--- | :--- | :--- |
| 1 | Register | Регистрация клиента |
| 2 | Message | Обычное сообщение |
| 3 | Reply | Ответ на сообщение |
| 4 | Ack | Подтверждение |
| 5 | Unregister | Отмена регистрации клиента |

#### Флаги сообщений

| Флаг | Значение | Описание |
| :--- | :--- | :--- |
| FlagNone | 0 | Нет флагов |
| FlagNeedsReply | 1 << 0 | Ожидает ответ |
| FlagNeedsAck | 1 << 1 | Ожидает подтверждения |

#### Пример использования

    Message msg(MessageType::Message, FlagNeedsAck, 12345, "alice", "bob", {0x01, 0x02});
    auto serialized = msg.Serialize();
    auto deserialized = Message::Deserialize(serialized);

---

### Server (Сервер)

Основной класс сервера брокера.

#### Конструктор

    explicit Server(const Config& config);

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| void Run() | Запускает сервер (блокирующий вызов) |
| void Stop() | Корректно останавливает сервер |
| bool IsRunning() const | Возвращает статус сервера |
| void SendToClient(zmq::message_t identity, zmq::message_t data, std::function<void(bool)> callback = nullptr) | Отправляет сообщение клиенту |
| const Config& GetConfig() const | Возвращает конфигурацию сервера |
| std::shared_ptr<IMetrics> GetMetrics() const | Возвращает менеджер метрик |

#### Пример использования

    Config config;
    config.Port = 5555;
    config.DbPath = "./broker.db";

    Server server(config);
    std::thread server_thread([&server]() { server.Run(); });

    // ... логика приложения ...

    server.Stop();
    server_thread.join();

---

### Config (Конфигурация)

Конфигурация сервера.

#### Публичные поля

| Поле | Тип | По умолчанию | Описание |
| :--- | :--- | :--- | :--- |
| Port | int | 5555 | Порт прослушивания ZeroMQ |
| DbPath | std::string | "./broker.db" | Путь к базе данных SQLite |
| Threads | int | CPU cores | Количество рабочих потоков |
| LogLevel | std::string | "info" | Уровень логирования (trace/debug/info/warn/error) |
| SessionTimeout | int | 60 | Таймаут сессии в секундах |
| AckTimeout | int | 30 | Таймаут подтверждения в секундах |
| EnableMetrics | bool | true | Включить метрики Prometheus |
| MetricsBindAddress | std::string | "0.0.0.0:8080" | Адрес конечной точки метрик |
| MetricsUpdateInterval | int | 2 | Интервал обновления метрик в секундах |

#### Статические методы

| Метод | Описание |
| :--- | :--- |
| static Config ParseArgs(int argc, char* argv[]) | Разбирает аргументы командной строки |
| static void PrintHelp(const char* program_name) | Выводит справочное сообщение |

#### Аргументы командной строки

| Аргумент | Описание |
| :--- | :--- |
| --port PORT | Порт прослушивания ZeroMQ |
| --db-path PATH | Путь к базе данных SQLite |
| --threads N | Количество рабочих потоков |
| --log-level LEVEL | Уровень логирования |
| --session-timeout N | Таймаут сессии в секундах |
| --ack-timeout N | Таймаут подтверждения в секундах |
| --disable-metrics | Отключить метрики Prometheus |
| --metrics-address ADDR | Адрес привязки метрик |
| --help | Показать справку |

---

### PendingMessage

Структура для хранения сообщения в очереди.

| Поле | Тип | Описание |
| :--- | :--- | :--- |
| id | uint64_t | Уникальный идентификатор сообщения в БД |
| msg | Message | Само сообщение |

---

### Storage (Интерфейс IStorage)

Интерфейс для хранения сообщений.

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| uint64_t SaveMessage(const Message& msg) | Сохраняет сообщение, возвращает ID |
| void MarkDelivered(uint64_t message_id) | Помечает сообщение как доставленное |
| void MarkSent(uint64_t message_id) | Помечает сообщение как отправленное (ожидает ACK) |
| void MarkPending(uint64_t message_id) | Помечает сообщение как ожидающее (повторная попытка) |
| bool NeedsAck(uint64_t message_id) | Проверяет, требует ли сообщение ACK |
| void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender) | Сохраняет сопоставление корреляции |
| std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) | Находит исходного отправителя по ID корреляции |
| uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) | Находит ID сообщения по ID корреляции |
| uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, const std::string& destination) | Находит ID сообщения по ID корреляции и получателю |
| void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) | Помечает сообщение как подтверждённое |
| std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) | Загружает просроченные отправленные сообщения |
| std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name) | Загружает ожидающие ответы для отправителя |
| std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name) | Загружает ожидающие сообщения для клиента |

---

### Router (Реализация ISessionManager)

Класс, реализующий маршрутизацию сообщений и управление сессиями клиентов.

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| void RouteMessage(const Message& msg, const zmq::message_t& identity) | Маршрутизирует сообщение получателю |
| std::shared_ptr<Session> FindSession(const std::string& name) | Находит сессию по имени клиента |
| bool RegisterClient(const std::string& name, std::shared_ptr<Session> session) | Регистрирует клиента |
| void UnregisterClient(const std::string& name) | Отменяет регистрацию клиента |
| void CleanupInactiveSessions() | Удаляет неактивные сессии |
| void CheckExpiredAcks() | Проверяет и повторяет просроченные ACK |
| void DeliverOfflineMessages(const std::string& name) | Доставляет сообщения из очереди офлайн |
| void PrintActiveClients() | Выводит активных клиентов в лог |

---

### Session (Сессия)

Управление соединением с клиентом.

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| bool SendMessage(const Message& msg) | Отправляет сообщение клиенту |
| std::string GetName() const | Возвращает имя клиента |
| bool IsOnline() const | Возвращает статус онлайн |
| const zmq::message_t& GetIdentity() const | Возвращает идентификатор ZMQ |
| void SetName(const std::string& name) | Устанавливает имя клиента |
| void FlushQueue() | Отправляет все сообщения из очереди |
| void MarkOffline() | Помечает клиента как офлайн |
| void MarkOnline() | Помечает клиента как онлайн |
| void UpdateLastReceive() | Обновляет временную метку последнего получения |
| void UpdateLastActivity() | Обновляет временную метку последней активности |
| bool IsExpired(int timeout_seconds) const | Проверяет, истекла ли сессия |
| void PersistQueueToDatabase() | Сохраняет сообщения из очереди в БД |
| size_t GetQueueSize() const | Возвращает размер исходящей очереди |

---

### IMetrics (Интерфейс метрик)

Абстрактный интерфейс для сбора метрик брокера.

| Метод | Описание |
| :--- | :--- |
| void IncrementMessagesReceived() | Счётчик полученных сообщений |
| void IncrementMessagesSent() | Счётчик отправленных сообщений |
| void IncrementMessagesFailed() | Счётчик сообщений с ошибкой доставки |
| void ObservePayloadSize(size_t bytes) | Гистограмма размера полезной нагрузки |
| void AddMessageProcessingTime(double seconds) | Гистограмма времени обработки сообщений |
| void IncrementClientsRegistered() | Счётчик зарегистрированных клиентов |
| void IncrementClientsUnregistered() | Счётчик отключившихся клиентов |
| void IncrementClientsTimeout() | Счётчик клиентов, отключённых по таймауту |
| void SetActiveSessions(int count) | Текущее количество активных сессий |
| void IncrementOfflineDelivered() | Счётчик сообщений, доставленных офлайн-клиентам |
| void IncrementMessagesExpired() | Счётчик просроченных сообщений |
| void IncrementAcksReceived() | Счётчик полученных подтверждений (ACK) |
| void SetPendingSendQueueSize(size_t size) | Текущий размер очереди ожидающих отправки |

---

### MetricsManager

Реализация `IMetrics` с экспортом метрик в Prometheus.

#### Конструктор

    MetricsManager()

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| void InitExposer(const std::string& bind_address = "0.0.0.0:8080") | Запускает HTTP-сервер метрик на указанном адресе |
| void StartUpdater(std::chrono::seconds interval = std::chrono::seconds(2)) | Запускает фоновое обновление метрик |
| void StopUpdater() | Останавливает фоновое обновление |
| *методы IMetrics* | См. таблицу выше |

#### Пример использования

    MetricsManager metrics;
    metrics.InitExposer("0.0.0.0:8080");
    metrics.StartUpdater();
    
    metrics.IncrementMessagesReceived();
    metrics.ObservePayloadSize(1024);
    metrics.AddMessageProcessingTime(0.005);
    
    metrics.StopUpdater();

#### Примечание

Если брокер скомпилирован без `BROKER_ENABLE_METRICS`, все методы становятся пустыми заглушками.

---

### ScopedMetricsTimer

RAII-таймер для автоматического измерения времени выполнения. При создании начинает отсчёт, при разрушении автоматически записывает время в метрики.

#### Конструктор

    explicit ScopedMetricsTimer(std::shared_ptr<IMetrics> metrics)

#### Пример использования

    void ProcessMessage(const Message& msg) {
        ScopedMetricsTimer timer(metrics_);
        // ... обработка сообщения ...
    } // время автоматически записывается при выходе из функции

---

### Метрики Prometheus

Доступны по адресу `http://<bind_address>:<port>/metrics` (по умолчанию `0.0.0.0:8080`).

| Метрика | Тип | Лейблы | Описание |
| :--- | :--- | :--- | :--- |
| `broker_messages_total` | Counter | `type="received"` | Получено сообщений |
| `broker_messages_total` | Counter | `type="sent"` | Отправлено сообщений |
| `broker_messages_total` | Counter | `type="failed"` | Ошибок доставки |
| `broker_messages_total` | Counter | `type="ack"` | Получено ACK |
| `broker_messages_total` | Counter | `type="expired"` | Просрочено сообщений |
| `broker_messages_total` | Counter | `type="offline"` | Доставлено офлайн |
| `broker_clients_total` | Counter | `type="register"` | Регистраций клиентов |
| `broker_clients_total` | Counter | `type="unregister"` | Отключений клиентов |
| `broker_clients_total` | Counter | `type="timeout"` | Отключений по таймауту |
| `broker_state` | Gauge | `state="active_sessions"` | Активных сессий |
| `broker_state` | Gauge | `state="pending_send_queue"` | Размер очереди отправки |
| `broker_message_processing_duration_seconds` | Histogram | `operation="message_processing"` | Время обработки |
| `broker_payload_size_bytes` | Histogram | `type="payload"` | Размер сообщений |

---

### Universal Client (Универсальный клиент)

Клиентский класс для взаимодействия с брокером.

#### Конструктор

    UniversalClient(const std::string& broker_address, const std::string& client_name);

#### Публичные методы

| Метод | Описание |
| :--- | :--- |
| void Run() | Запускает интерактивную оболочку клиента |
| void Stop() | Останавливает клиент |

#### Интерактивные команды

| Команда | Описание |
| :--- | :--- |
| send <dest> <message> | Отправить сообщение получателю |
| send_ack <dest> <msg> | Отправить сообщение, требующее ACK |
| request <dest> <message> | Отправить запрос и ждать ответ |
| reply <corr_id> <message> | Вручную ответить на запрос |
| status | Показать статус клиента |
| help | Показать справку |
| quit | Выйти из клиента |

#### Использование из командной строки

    universal_client <broker_address> <client_name> [--debug]

    # Примеры
    universal_client tcp://localhost:5555 alice
    universal_client tcp://localhost:5555 bob --debug

---

## Интерфейсы

### IStorage

Интерфейс для абстракции хранилища сообщений. Определяет контракт для сохранения, обновления состояния и извлечения сообщений из персистентного хранилища.

#### Определение интерфейса

    class IStorage {
    public:
        virtual ~IStorage() = default;
        virtual uint64_t SaveMessage(const Message& msg) = 0;
        virtual void MarkDelivered(uint64_t message_id) = 0;
        virtual void MarkSent(uint64_t message_id) = 0;
        virtual bool NeedsAck(uint64_t message_id) = 0;
        virtual void MarkPending(uint64_t message_id) = 0;
        virtual void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender) = 0;
        virtual std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) = 0;
        virtual uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) = 0;
        virtual uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, const std::string& destination) = 0;
        virtual void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) = 0;
        virtual std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) = 0;
        virtual std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name) = 0;
        virtual std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name) = 0;
    };

#### Описание методов

| Метод | Описание |
| :--- | :--- |
| virtual uint64_t SaveMessage(const Message& msg) | Сохраняет сообщение в БД. Возвращает уникальный идентификатор сообщения |
| virtual void MarkDelivered(uint64_t message_id) | Помечает сообщение как успешно доставленное получателю |
| virtual void MarkSent(uint64_t message_id) | Помечает сообщение как отправленное, но ещё не подтверждённое (ожидает ACK) |
| virtual bool NeedsAck(uint64_t message_id) | Проверяет флаги сообщения и возвращает true, если требуется подтверждение |
| virtual void MarkPending(uint64_t message_id) | Помечает сообщение как ожидающее повторной отправки |
| virtual void SaveCorrelation(uint64_t message_id, uint64_t correlation_id, const std::string& original_sender) | Сохраняет связь между ID сообщения, ID корреляции и отправителем для отслеживания цепочек запрос-ответ |
| virtual std::string FindOriginalSenderByCorrelation(uint64_t correlation_id) | Находит имя отправителя по ID корреляции |
| virtual uint64_t FindMessageIdByCorrelation(uint64_t correlation_id) | Находит ID сообщения по ID корреляции |
| virtual uint64_t FindMessageIdByCorrelationAndDestination(uint64_t correlation_id, const std::string& destination) | Находит ID сообщения по ID корреляции и получателю |
| virtual void MarkAckReceived(uint64_t message_id, const std::string& ack_sender) | Помечает сообщение как подтверждённое указанным получателем |
| virtual std::vector<PendingMessage> LoadExpiredSent(int timeout_seconds) | Загружает сообщения в статусе "отправлено", которые не получили ACK за указанный таймаут |
| virtual std::vector<PendingMessage> LoadPendingRepliesForSenderOnly(const std::string& sender_name) | Загружает ожидающие ответы для конкретного отправителя |
| virtual std::vector<PendingMessage> LoadPendingMessagesOnly(const std::string& client_name) | Загружает все ожидающие сообщения для конкретного клиента |

### IMessageSender

Интерфейс для отправки сообщений клиентам через ZeroMQ. Абстрагирует транспортный уровень от бизнес-логики.

#### Определение интерфейса

    class IMessageSender {
    public:
        virtual ~IMessageSender() = default;
        virtual void SendToClient(zmq::message_t identity, zmq::message_t data, std::function<void(bool)> callback = nullptr) = 0;
    };

#### Описание методов

| Метод | Описание |
| :--- | :--- |
| virtual void SendToClient(zmq::message_t identity, zmq::message_t data, std::function<void(bool)> callback) | Отправляет данные клиенту по его ZMQ-идентификатору. Опциональный callback вызывается с результатом отправки (true - успех, false - ошибка) |

### IConfigProvider

Интерфейс для доступа к конфигурации брокера. Позволяет компонентам получать настройки без прямого доступа к объекту Config.

#### Определение интерфейса

    class IConfigProvider {
    public:
        virtual ~IConfigProvider() = default;
        virtual const Config& GetConfig() const = 0;
    };

#### Описание методов

| Метод | Описание |
| :--- | :--- |
| virtual const Config& GetConfig() const | Возвращает константную ссылку на объект конфигурации |

### ISessionManager

Интерфейс для управления сессиями клиентов и маршрутизации сообщений. Центральный компонент для отслеживания подключённых клиентов и доставки сообщений.

#### Определение интерфейса

    class ISessionManager {
    public:
        virtual ~ISessionManager() = default;
        virtual std::shared_ptr<Session> FindSession(const std::string& name) = 0;
        virtual bool RegisterClient(const std::string& name, std::shared_ptr<Session> session) = 0;
        virtual void UnregisterClient(const std::string& name) = 0;
        virtual void PrintActiveClients() = 0;
        virtual void CleanupInactiveSessions() = 0;
        virtual void DeliverOfflineMessages(const std::string& name) = 0;
        virtual void DeliverPendingReplies(const std::string& name) = 0;
        virtual void PersistMessageForClient(const std::string& client_name, const Message& msg) = 0;
        virtual void CheckExpiredAcks() = 0;
    };

#### Описание методов

| Метод | Описание |
| :--- | :--- |
| virtual std::shared_ptr<Session> FindSession(const std::string& name) | Ищет активную сессию по имени клиента. Возвращает nullptr, если клиент не найден или офлайн |
| virtual bool RegisterClient(const std::string& name, std::shared_ptr<Session> session) | Регистрирует новую сессию клиента. Возвращает false, если клиент с таким именем уже зарегистрирован |
| virtual void UnregisterClient(const std::string& name) | Удаляет регистрацию клиента и освобождает ресурсы сессии |
| virtual void PrintActiveClients() | Выводит в лог список всех активных клиентов (для отладки) |
| virtual void CleanupInactiveSessions() | Проверяет все сессии и удаляет те, которые превысили таймаут SessionTimeout |
| virtual void DeliverOfflineMessages(const std::string& name) | Доставляет клиенту все сообщения, накопленные за время его отсутствия |
| virtual void DeliverPendingReplies(const std::string& name) | Доставляет клиенту все ожидающие ответы на его запросы |
| virtual void PersistMessageForClient(const std::string& client_name, const Message& msg) | Сохраняет сообщение в БД для офлайн-клиента и помещает в очередь ожидания |
| virtual void CheckExpiredAcks() | Проверяет сообщения, ожидающие ACK, и инициирует повторную отправку для просроченных |

### IMetrics

Интерфейс для сбора и предоставления метрик брокера. Абстрагирует систему мониторинга от остальных компонентов.

#### Определение интерфейса

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

#### Описание методов

| Метод | Описание |
| :--- | :--- |
| virtual void IncrementMessagesReceived() | Увеличивает счётчик входящих сообщений, полученных брокером |
| virtual void IncrementMessagesSent() | Увеличивает счётчик сообщений, успешно отправленных получателям |
| virtual void IncrementMessagesFailed() | Увеличивает счётчик сообщений, которые не удалось доставить |
| virtual void ObservePayloadSize(size_t bytes) | Записывает в гистограмму размер полезной нагрузки сообщения |
| virtual void AddMessageProcessingTime(double seconds) | Записывает в гистограмму время обработки сообщения |
| virtual void IncrementClientsRegistered() | Увеличивает счётчик успешных регистраций клиентов |
| virtual void IncrementClientsUnregistered() | Увеличивает счётчик отмены регистраций клиентов |
| virtual void IncrementClientsTimeout() | Увеличивает счётчик клиентов, отключённых по таймауту |
| virtual void SetActiveSessions(int count) | Устанавливает текущее значение активных сессий (Gauge) |
| virtual void IncrementOfflineDelivered() | Увеличивает счётчик сообщений, доставленных клиенту после выхода из офлайна |
| virtual void IncrementMessagesExpired() | Увеличивает счётчик сообщений, превысивших TTL или таймаут доставки |
| virtual void IncrementAcksReceived() | Увеличивает счётчик полученных подтверждений (ACK) |
| virtual void SetPendingSendQueueSize(size_t size) | Устанавливает текущий размер очереди сообщений, ожидающих отправки (Gauge) |

---

## Диаграмма потока сообщений

    Клиент A → Брокер → Клиент B
              ← ACK ←
              ← Reply ←

**Описание потока:**

1. Клиент A отправляет сообщение в брокер
2. Брокер сохраняет сообщение в БД и маршрутизирует клиенту B
3. Клиент B получает сообщение и отправляет ACK
4. Брокер получает ACK и помечает сообщение как доставленное
5. (Опционально) Клиент B отправляет Reply
6. Брокер маршрутизирует Reply клиенту A

---

## Обработка ошибок

Брокер использует стандартные исключения C++ для обработки ошибок:

| Исключение | Описание |
| :--- | :--- |
| std::runtime_error | Общие ошибки времени выполнения (сеть, сериализация, база данных) |
| broker::ConfigError | Ошибки разбора конфигурации |
| broker::HelpRequested | Обнаружен флаг справки (не ошибка) |
| zmq::error_t | Ошибки транспорта ZeroMQ |

---

## Сборка и установка

### Требования для сборки

- CMake 3.15+
- Компилятор с поддержкой C++17
- ZeroMQ (libzmq3-dev)
- Boost.Asio (libboost-system-dev)
- SQLite3 (libsqlite3-dev)
- spdlog (libspdlog-dev)
- prometheus-cpp (опционально, для метрик)

### Команды сборки

    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    sudo make install

### Установка DEB пакета

    sudo dpkg -i async-message-broker.deb
    broker --help
    universal_client --help
