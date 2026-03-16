[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Boost](https://img.shields.io/badge/Boost-1.82+-green.svg)](https://www.boost.org/)
[![ZeroMQ](https://img.shields.io/badge/ZeroMQ-4.3+-red.svg)](https://zeromq.org/)
[![SQLite](https://img.shields.io/badge/SQLite-3-blue.svg)](https://www.sqlite.org/)

Асинхронный брокер сообщений с гарантированной доставкой, поддержкой двухсторонней связи (request-reply) и персистентным хранением на диске. Реализован на C++ с использованием библиотек Boost.Asio и ZeroMQ.

## 📋 Содержание

- [О проекте](#о-проекте)
- [Функциональные возможности](#функциональные-возможности)
- [Архитектура](#архитектура)
- [Диаграмма классов](#диаграмма-классов)
- [Сценарии работы](#сценарии-работы)
- [Технологический стек](#технологический-стек)
- [Структура проекта](#структура проекта)
- [Сборка и установка](#сборка-и-установка)

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
    class Server {
        -io_context io_context_
        -zmq::socket_t router_socket_
        -Router router_
        -Storage storage_
        +run()
        -handle_zmq_events()
    }
    
    class Router {
        -map~string, weak_ptr~Session~~ active_clients_
        -CorrelationEngine correlation_engine_
        -OfflineQueueManager offline_manager_
        +register_client(string name, Session session)
        +unregister_client(string name)
        +route_message(Message msg)
        +deliver_offline_messages(string name)
    }
    
    class Session {
        <<interface>>
        +send_message(Message msg)
        +get_name() string
        +is_online() bool
    }
    
    class ZmqSession {
        -zmq::socket_t socket_
        -string client_name_
        -vector~Message~ outgoing_queue_
        +send_message(Message msg)
        +process_incoming()
    }
    
    class Message {
        +uint8_t type
        +uint8_t flags
        +uint64_t correlation_id
        +string sender
        +string destination
        +vector~uint8_t~ payload
        +serialize() vector~uint8_t~
        +deserialize(data) Message
    }
    
    class Storage {
        +save_message(Message msg) uint64_t
        +load_pending(string client) vector~Message~
        +mark_delivered(uint64_t msg_id)
        +save_correlation(uint64_t msg_id, uint64_t correlation_id)
        +find_by_correlation(uint64_t correlation_id) Message
    }
    
    class CorrelationEngine {
        -map~uint64_t, PendingRequest~ requests_
        +register_request(uint64_t correlation_id, string original_sender)
        +handle_response(Message response)
    }
    
    class OfflineQueueManager {
        -Storage& storage_
        +enqueue_offline(string client, Message msg)
        +deliver_queued(string client, Session session)
    }
    
    Server *-- Router
    Server *-- Storage
    Router --> CorrelationEngine
    Router --> OfflineQueueManager
    OfflineQueueManager --> Storage
    CorrelationEngine --> Storage
    Router --> Session : uses
    Session <|-- ZmqSession
    ZmqSession --> Message
    Storage --> Message
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
Для объединения двух библиотек в единый цикл событий используется следующий подход:

ZeroMQ сокеты работают в неблокирующем режиме

В основном потоке используется zmq_poll с небольшим таймаутом

После каждого опроса все готовые дескрипторы обрабатываются, а управление передаётся io_context::poll() или io_context::run_for()

Альтернативно на Unix-системах можно получить файловый дескриптор сокета ZeroMQ через getsockopt(ZMQ_FD) и использовать boost::asio::posix::stream_descriptor для полноценной асинхронной интеграции.

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
- Требуется максимальная производительность, низкоуровневый контроль над ресурсами и богатая экосистема библиотек.
- C++ обеспечивает эффективную работу с сетью, памятью и многопоточностью, что критически важно для брокера сообщений.

### Сетевая библиотека: Boost.Asio
**Обоснование:**
- Стандарт де-факто для асинхронного сетевого программирования на C++
- Асинхронная модель (Proactor) идеально подходит для высоконагруженных I/O-приложений
- Богатые возможности (таймеры, сигналы, интеграция с другими библиотеками)
- Часть Boost (широко используется, хорошая документация)
- Кроссплатформенность
- Единый цикл событий (io_context) для всех асинхронных операций

### Транспорт и маршрутизация: ZeroMQ (libzmq + cppzmq)
**Обоснование:**
- Готовые паттерны ROUTER/DEALER для асинхронной маршрутизации
- Автоматическое управление соединениями и переподключением
- Фреймовая структура сообщений (удобно для заголовков и метаданных)
- Высокая производительность (ядро на C)
- Проверенная временем библиотека, используемая в высоконагруженных системах

### Хранение данных: SQLite
**Обоснование:**
- Встраиваемая БД (не требует отдельного сервиса)
- Транзакционность (гарантия целостности при записи на диск)
- SQL для удобной выборки (pending сообщения для клиента, поиск по корреляции)
- Достаточная производительность для заявленных нагрузок
- Надёжность (атомарная запись на диск)
- Минимальные накладные расходы

### Логирование: spdlog
Обоснование:

Высокая производительность (асинхронные режимы)

Простой и удобный API

Гибкое форматирование

Поддержка ротации файлов

### Сборка: CMake
Обоснование:

Стандарт для C++ проектов

Удобное управление зависимостями (FetchContent, find_package)

Кроссплатформенность

Хорошая интеграция с IDE

### Тестирование: Google Test
Обоснование: Фреймворк обеспечивает удобное модульное и интеграционное тестирование ключевых компонентов (маршрутизация, корреляция, хранение).

## Структура проекта
```
broker/
├── cmake/                 # CMake модули
├── include/               
│   └── broker/            # Публичные заголовки
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
├── libs/                   # Внешние зависимости (через CMake)
├── tests/
│   ├── unit/
│   └── integration/
├── examples/               # Примеры клиентов
│   ├── cpp_client/
│   └── python_client/
├── docs/                   # Документация
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


