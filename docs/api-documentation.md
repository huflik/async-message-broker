# Документация публичного API

## Обзор

Асинхронный брокер сообщений — высокопроизводительный брокер с гарантированной доставкой. Для транспорта используется ZeroMQ, для персистентности — SQLite, для сбора метрик — Prometheus.

---

## Компоненты ядра

### Класс Message

Базовый класс сообщения, используемый для всех типов коммуникации.

**Методы:**

| Метод | Описание |
|-------|----------|
| `GetType()` | Возвращает тип сообщения |
| `GetFlags()` | Возвращает флаги сообщения |
| `GetCorrelationId()` | Возвращает идентификатор корреляции |
| `GetSender()` | Возвращает имя отправителя |
| `GetDestination()` | Возвращает имя получателя |
| `GetPayload()` | Возвращает полезную нагрузку |
| `SetType()` | Устанавливает тип сообщения |
| `SetFlags()` | Устанавливает флаги |
| `SetCorrelationId()` | Устанавливает идентификатор корреляции |
| `SetSender()` | Устанавливает отправителя |
| `SetDestination()` | Устанавливает получателя |
| `SetPayload()` | Устанавливает полезную нагрузку |
| `NeedsReply()` | Проверяет, требуется ли ответ |
| `NeedsAck()` | Проверяет, требуется ли подтверждение |
| `Serialize()` | Сериализует сообщение в массив байт |
| `Deserialize()` | Десериализует сообщение из массива байт |
| `ToString()` | Возвращает строковое представление |

**Типы сообщений:**

| Значение | Имя | Описание |
|----------|-----|----------|
| 1 | `Register` | Регистрация клиента |
| 2 | `Message` | Обычное сообщение |
| 3 | `Reply` | Ответ на сообщение |
| 4 | `Ack` | Подтверждение |
| 5 | `Unregister` | Отмена регистрации клиента |

**Флаги сообщений:**

| Флаг | Значение | Описание |
|------|----------|----------|
| `FlagNone` | 0 | Нет флагов |
| `FlagNeedsReply` | 1 << 0 | Ожидается ответ |
| `FlagNeedsAck` | 1 << 1 | Требуется подтверждение |

**Пример использования**

```cpp
Message msg(MessageType::Message, FlagNeedsAck, 12345, "alice", "bob", {0x01, 0x02});
auto serialized = msg.Serialize();
auto deserialized = Message::Deserialize(serialized);
```

### Класс Server

Основной класс сервера брокера

**Методы**

| Метод | Описание |
| :--- | :--- |
| `Server(Config)` | Конструктор |
| `Run()` | Запускает сервер |
| `Stop()` | Останавливает сервер |
| `IsRunning()` | Проверяет, работает ли сервер |
| `SendToClient()` | Отправляет сообщение клиенту |
| `GetConfig()` | Возвращает конфигурацию |
| `GetMetrics()` | Возвращает менеджер метрик |

## Пример использования

```cpp
Config config;
config.Port = 5555;
config.DbPath = "./broker.db";

Server server(config);
std::thread server_thread([&server]() { server.Run(); });

// ... выполнение полезной работы ...

server.Stop();
server_thread.join();
```
### Структура Config

Конфигурация сервера

**Поля**

| Поле | Тип | По умолчанию | Описание |
| :--- | :--- | :--- | :--- |
| `Port` | `int` | `5555` | Порт для ZeroMQ |
| `DbPath` | `string` | `"./broker.db"` | Путь к базе SQLite |
| `Threads` | `int` | кол-во ядер CPU | Количество рабочих потоков |
| `LogLevel` | `string` | `"info"` | Уровень логирования |
| `SessionTimeout` | `int` | `60` | Таймаут сессии в секундах |
| `AckTimeout` | `int` | `30` | Таймаут подтверждения в секундах |
| `EnableMetrics` | `bool` | `true` | Включить метрики Prometheus |
| `MetricsBindAddress` | `string` | `"0.0.0.0:8080"` | Адрес для метрик |
| `MetricsUpdateInterval` | `int` | `2` | Интервал обновления метрик |

**Статические методы**

| Метод | Описание |
| :--- | :--- |
| `ParseArgs(argc, argv)` | Разбирает аргументы командной строки |
| `PrintHelp(program_name)` | Выводит справку |

**Аргументы командной строки**

| Аргумент | Описание |
| :--- | :--- |
| `--port PORT` | Порт для ZeroMQ |
| `--db-path PATH` | Путь к базе данных |
| `--threads N` | Количество рабочих потоков |
| `--log-level LEVEL` | Уровень логирования |
| `--session-timeout N` | Таймаут сессии |
| `--ack-timeout N` | Таймаут подтверждения |
| `--disable-metrics` | Отключить метрики |
| `--metrics-address ADDR` | Адрес для метрик |
| `--help` | Показать справку |

### Интерфейс IStorage

Интерфейс для хранения сообщений

**Методы**

| Метод | Описание |
| :--- | :--- |
| `SaveMessage(msg)` | Сохраняет сообщение, возвращает ID |
| `MarkDelivered(message_id)` | Отмечает сообщение как доставленное |
| `MarkSent(message_id)` | Отмечает сообщение как отправленное |
| `MarkPending(message_id)` | Отмечает сообщение как ожидающее |
| `NeedsAck(message_id)` | Проверяет, требуется ли подтверждение |
| `SaveCorrelation(msg_id, corr_id, sender)` | Сохраняет связь корреляции |
| `FindOriginalSenderByCorrelation(corr_id)` | Находит отправителя по ID корреляции |
| `FindMessageIdByCorrelation(corr_id)` | Находит ID сообщения по ID корреляции |
| `LoadPendingMessagesOnly(client_name)` | Загружает ожидающие сообщения для клиента |
| `LoadPendingRepliesForSenderOnly(sender)` | Загружает ожидающие ответы для отправителя |
| `LoadExpiredSent(timeout)` | Загружает просроченные отправленные сообщения |

### Класс Session

Управление соединением с клиентом

**Методы**

| Метод | Описание |
| :--- | :--- |
| `SendMessage(msg)` | Отправляет сообщение клиенту |
| `GetName()` | Возвращает имя клиента |
| `IsOnline()` | Проверяет, онлайн ли клиент |
| `GetIdentity()` | Возвращает ZMQ идентификатор |
| `SetName(name)` | Устанавливает имя клиента |
| `FlushQueue()` | Отправляет все накопленные сообщения |
| `MarkOffline()` | Отмечает клиента как офлайн |
| `MarkOnline()` | Отмечает клиента как онлайн |
| `UpdateLastReceive()` | Обновляет время последнего получения |
| `UpdateLastActivity()` | Обновляет время последней активности |
| `IsExpired(timeout)` | Проверяет, истекла ли сессия |
| `PersistQueueToDatabase()` | Сохраняет очередь в базу данных |
| `GetQueueSize()` | Возвращает размер очереди |

### Интерфейс IMetrics

Интерфейс для сбора метрик

**Методы**

| Метод | Описание |
| :--- | :--- |
| `IncrementMessagesReceived()` | Увеличивает счетчик полученных сообщений |
| `IncrementMessagesSent()` | Увеличивает счетчик отправленных сообщений |
| `IncrementMessagesFailed()` | Увеличивает счетчик неудачных сообщений |
| `IncrementAcksReceived()` | Увеличивает счетчик полученных подтверждений |
| `IncrementMessagesExpired()` | Увеличивает счетчик просроченных сообщений |
| `IncrementOfflineDelivered()` | Увеличивает счетчик офлайн-доставок |
| `IncrementClientsRegistered()` | Увеличивает счетчик регистраций клиентов |
| `IncrementClientsUnregistered()` | Увеличивает счетчик отмен регистрации |
| `IncrementClientsTimeout()` | Увеличивает счетчик таймаутов клиентов |
| `SetActiveSessions(count)` | Устанавливает количество активных сессий |
| `SetPendingSendQueueSize(size)` | Устанавливает размер очереди отправки |
| `ObservePayloadSize(bytes)` | Записывает размер полезной нагрузки |
| `AddMessageProcessingTime(seconds)` | Записывает время обработки сообщения |

### Точка метрик Prometheus

Метрики доступны по HTTP-адресу, указанному в MetricsBindAddress (по умолчанию 0.0.0.0:8080). Полный список метрик:

**Счетчики сообщений (broker_messages_total)**

| Тип | Описание |
| :--- | :--- |
| received | Количество полученных сообщений |
| sent | Количество отправленных сообщений |
| failed | Количество неудачных отправок |
| ack | Количество полученных подтверждений |
| expired | Количество просроченных сообщений |
| offline | Количество доставленных офлайн-сообщений |

**Счетчики клиентов (broker_clients_total)**

| Тип | Описание |
| :--- | :--- |
| register | Количество регистраций клиентов |
| unregister | Количество отмен регистрации |
| timeout | Количество клиентов по таймауту |

**Текущие состояния (broker_state)**

| Состояние | Описание |
| :--- | :--- |
| active_sessions | Текущее количество активных сессий |
| pending_send_queue | Текущий размер очереди отправки |

**Гистограммы**

| Метрика | Описание |
| :--- | :--- |
| broker_message_processing_duration_seconds | Время обработки сообщения |
| broker_payload_size_bytes | Размер полезной нагрузки сообщения |

### Универсальный клиент

Клиент для тестирования и отладки

**Команды интерактивного режима**

| Команда | Описание |
| :--- | :--- |
| `send <получатель> <сообщение>` | Отправить сообщение |
| `send_ack <получатель> <сообщение>` | Отправить с запросом подтверждения |
| `request <получатель> <сообщение>` | Отправить с запросом ответа |
| `reply <id> <сообщение>` | Отправить ответ на запрос |
| `status` | Показать статус клиента |
| `help` | Показать справку |
| `quit` | Выйти из клиента |

## Примеры запуска

```bash
universal_client tcp://localhost:5555 alice
universal_client tcp://localhost:5555 bob --debug
```
### Спецификация протокола

Формат сообщения (15 байт заголовка + переменная длина)

| Смещение | Размер | Поле | Описание | Порядок байт |
| :--- | :--- | :--- | :--- | :--- |
| 0 | 1 | Версия | Всегда 1 | - |
| 1 | 1 | Тип | 1-5 | - |
| 2 | 1 | Флаги | Битовая маска | - |
| 3 | 8 | Correlation ID | 64-битный идентификатор | network |
| 11 | 1 | Длина отправителя | 0-255 | - |
| 12 | 1 | Длина получателя | 0-255 | - |
| 13 | 2 | Длина полезной нагрузки | 0-65535 | network |
| 15 | N | Имя отправителя | UTF-8 строка | - |
| 15+N | M | Имя получателя | UTF-8 строка | - |
| 15+N+M | L | Полезная нагрузка | Произвольные данные | - |

**Пример шестнадцатеричного дампа**
```
01 02 03 00 00 00 00 00 00 00 30 39 04 05 00 04 61 6c 69 63 65 03 62 6f 62 48 65 6c 6c 6f
```
Где:

- 01 — версия протокола
- 02 — тип "Сообщение"
- 03 — флаги (NeedsReply + NeedsAck)
- 00 00 00 00 00 00 30 39 — Correlation ID = 12345
- 04 — длина отправителя = 4
- 05 — длина получателя = 5
- 00 04 — длина полезной нагрузки = 4
- 61 6c 69 63 65 — "alice"
- 62 6f 62 — "bob"
- 48 65 6c 6c 6f — "Hello"

## Примеры использования

### Запуск брокера

```bash
./broker --port 5555 --db-path /data/broker.db --threads 4
```
### Отправка сообщения через ZeroMQ

```cpp
Message msg;
msg.SetType(MessageType::Message);
msg.SetNeedsAck(true);
msg.SetCorrelationId(12345);
msg.SetSender("alice");
msg.SetDestination("bob");
msg.SetPayload({0x48, 0x65, 0x6c, 0x6c, 0x6f});

auto serialized = msg.Serialize();
zmq::message_t zmq_msg(serialized.data(), serialized.size());
socket.send(zmq_msg);
```
### Получение сообщения

```cpp
zmq::message_t zmq_msg;
socket.recv(zmq_msg);

std::vector<uint8_t> data(
    static_cast<uint8_t*>(zmq_msg.data()),
    static_cast<uint8_t*>(zmq_msg.data()) + zmq_msg.size()
);

auto msg = Message::Deserialize(data);

if (msg.NeedsAck()) {
    // отправить подтверждение
}
```
### Получение метрик

```bash
curl http://localhost:8080/metrics
```
### Обработка ошибок

Брокер использует стандартные исключения C++:

| Исключение | Описание |
| :--- | :--- |
| std::runtime_error | Ошибки времени выполнения |
| broker::ConfigError | Ошибки парсинга конфигурации |
| broker::HelpRequested | Запрошена справка |
| zmq::error_t | Ошибки ZeroMQ |
### Сборка и установка

**Требования**

- CMake 3.15+
- C++17
- ZeroMQ
- Boost.Asio
- SQLite3
- spdlog
- prometheus-cpp (опционально)

**Команды сборки**

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```
**Установка DEB пакета**

```bash
sudo dpkg -i async-message-broker.deb
broker --help
```
