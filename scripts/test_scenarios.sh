#!/bin/bash

# test_scenarios.sh - Скрипт для тестирования работы брокера с клиентами

set -e

# Исправленные пути
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
BROKER_BIN="$BUILD_DIR/src/broker"
CLIENT_BIN="$BUILD_DIR/src/universal_client"
BROKER_ADDRESS="tcp://localhost:5555"
BROKER_DB="$PROJECT_DIR/broker_test.db"
BROKER_LOG="$PROJECT_DIR/broker.log"

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo "=========================================="
echo "  Async Message Broker - Test Scenario"
echo "=========================================="
echo ""
echo "Project directory: $PROJECT_DIR"
echo "Build directory: $BUILD_DIR"
echo "Broker binary: $BROKER_BIN"
echo "Client binary: $CLIENT_BIN"
echo ""

# Функция для проверки наличия исполняемых файлов
check_binaries() {
    echo -e "${YELLOW}Checking binaries...${NC}"
    
    if [ ! -f "$BROKER_BIN" ]; then
        echo -e "${RED}Error: Broker binary not found at $BROKER_BIN${NC}"
        echo ""
        echo "Please build the project first:"
        echo "  cd $PROJECT_DIR"
        echo "  mkdir -p build && cd build"
        echo "  cmake .. -DBUILD_EXAMPLES=ON"
        echo "  make -j\$(nproc)"
        exit 1
    fi
    
    if [ ! -f "$CLIENT_BIN" ]; then
        echo -e "${RED}Error: Universal client binary not found at $CLIENT_BIN${NC}"
        echo "Make sure to add universal_client to CMakeLists.txt and rebuild"
        exit 1
    fi
    
    echo -e "${GREEN}All binaries found${NC}"
}

# Функция для очистки старых файлов
cleanup_old_files() {
    echo -e "${YELLOW}Cleaning up old files...${NC}"
    rm -f "$BROKER_DB" "$BROKER_DB"-shm "$BROKER_DB"-wal
    rm -f "$BROKER_LOG"
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Функция для запуска брокера
start_broker() {
    echo -e "${YELLOW}Starting broker...${NC}"
    
    $BROKER_BIN \
        --port 5555 \
        --db-path "$BROKER_DB" \
        --log-level debug \
        --threads 4 \
        --session-timeout 60 \
        --ack-timeout 10 \
        --disable-metrics &
    
    BROKER_PID=$!
    echo -e "${GREEN}Broker started with PID: $BROKER_PID${NC}"
    
    # Ждем инициализации брокера
    sleep 3
    
    # Проверяем что брокер запущен
    if ! kill -0 $BROKER_PID 2>/dev/null; then
        echo -e "${RED}Broker failed to start${NC}"
        echo "Check log: $BROKER_LOG"
        if [ -f "$BROKER_LOG" ]; then
            tail -20 "$BROKER_LOG"
        fi
        exit 1
    fi
}

# Функция для остановки брокера
stop_broker() {
    if [ -n "$BROKER_PID" ] && kill -0 $BROKER_PID 2>/dev/null; then
        echo -e "${YELLOW}Stopping broker (PID: $BROKER_PID)...${NC}"
        kill -SIGTERM $BROKER_PID
        wait $BROKER_PID 2>/dev/null || true
        echo -e "${GREEN}Broker stopped${NC}"
    fi
}

# Функция для запуска клиента и отправки команд через expect
run_client_with_commands() {
    local client_name=$1
    local commands=$2
    local log_file=$3
    
    echo -e "${CYAN}Starting client: $client_name${NC}"
    
    # Запускаем клиента и передаем команды через pipe
    echo "$commands" | timeout 10 $CLIENT_BIN "$BROKER_ADDRESS" "$client_name" > "$log_file" 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq 124 ]; then
        echo -e "${YELLOW}Client $client_name timed out${NC}"
    fi
    
    return $exit_code
}

# ==========================================
# Тестовые сценарии
# ==========================================

test_scenario_1() {
    echo ""
    echo -e "${MAGENTA}=========================================="
    echo "  Scenario 1: Simple Send/Receive"
    echo "==========================================${NC}"
    echo ""
    
    # Bob отправляет сообщение alice
    echo -e "${BLUE}Sender 'bob' sending message to 'alice'...${NC}"
    local bob_commands="send alice Hello from Bob!
status
quit"
    
    # Alice ждет сообщения
    local alice_commands="status
quit"
    
    # Запускаем обоих клиентов (alice сначала, чтобы получить сообщение)
    run_client_with_commands "alice" "$alice_commands" "/tmp/alice_scenario1.log" &
    local alice_pid=$!
    
    sleep 2
    
    run_client_with_commands "bob" "$bob_commands" "/tmp/bob_scenario1.log"
    
    wait $alice_pid 2>/dev/null || true
    
    echo -e "${GREEN}Scenario 1 completed${NC}"
    echo ""
    echo -e "${CYAN}--- Bob's output ---${NC}"
    cat /tmp/bob_scenario1.log
    echo ""
    echo -e "${CYAN}--- Alice's output ---${NC}"
    cat /tmp/alice_scenario1.log
}

test_scenario_2() {
    echo ""
    echo -e "${MAGENTA}=========================================="
    echo "  Scenario 2: Offline Message Delivery"
    echo "==========================================${NC}"
    echo ""
    
    # Bob отправляет сообщение alice, когда она офлайн
    echo -e "${BLUE}Bob sending message to offline Alice...${NC}"
    local bob_commands="send alice This message will be stored!
status
quit"
    
    run_client_with_commands "bob" "$bob_commands" "/tmp/bob_offline.log"
    
    sleep 2
    
    # Alice подключается позже и получает сообщение
    echo -e "${BLUE}Alice connecting and receiving stored messages...${NC}"
    local alice_commands="status
quit"
    
    run_client_with_commands "alice" "$alice_commands" "/tmp/alice_offline.log"
    
    echo -e "${GREEN}Scenario 2 completed${NC}"
    echo ""
    echo -e "${CYAN}--- Bob's output ---${NC}"
    cat /tmp/bob_offline.log
    echo ""
    echo -e "${CYAN}--- Alice's output ---${NC}"
    cat /tmp/alice_offline.log
}

test_scenario_3() {
    echo ""
    echo -e "${MAGENTA}=========================================="
    echo "  Scenario 3: Request-Reply Pattern"
    echo "==========================================${NC}"
    echo ""
    
    # Bob запускается и ждет запросы
    local bob_commands="status
quit"
    
    run_client_with_commands "bob" "$bob_commands" "/tmp/bob_reply.log" &
    local bob_pid=$!
    
    sleep 2
    
    # Alice делает запрос к Bob
    echo -e "${BLUE}Alice sending request to Bob...${NC}"
    local alice_commands="request bob What is the status?
status
quit"
    
    run_client_with_commands "alice" "$alice_commands" "/tmp/alice_reply.log"
    
    wait $bob_pid 2>/dev/null || true
    
    echo -e "${GREEN}Scenario 3 completed${NC}"
    echo ""
    echo -e "${CYAN}--- Alice's output ---${NC}"
    cat /tmp/alice_reply.log
    echo ""
    echo -e "${CYAN}--- Bob's output ---${NC}"
    cat /tmp/bob_reply.log
}

test_scenario_4() {
    echo ""
    echo -e "${MAGENTA}=========================================="
    echo "  Scenario 4: ACK and Reliability"
    echo "==========================================${NC}"
    echo ""
    
    # Alice запускается
    run_client_with_commands "alice" "status\nquit" "/tmp/alice_ack.log" &
    local alice_pid=$!
    
    sleep 2
    
    # Bob отправляет сообщение с требованием ACK
    echo -e "${BLUE}Bob sending message requiring ACK...${NC}"
    local bob_commands="send_ack alice This is important!
status
quit"
    
    run_client_with_commands "bob" "$bob_commands" "/tmp/bob_ack.log"
    
    wait $alice_pid 2>/dev/null || true
    
    echo -e "${GREEN}Scenario 4 completed${NC}"
    echo ""
    echo -e "${CYAN}--- Bob's output ---${NC}"
    cat /tmp/bob_ack.log
    echo ""
    echo -e "${CYAN}--- Alice's output ---${NC}"
    cat /tmp/alice_ack.log
}

test_scenario_5() {
    echo ""
    echo -e "${MAGENTA}=========================================="
    echo "  Scenario 5: Multiple Clients"
    echo "==========================================${NC}"
    echo ""
    
    # Запускаем alice
    run_client_with_commands "alice" "status\nquit" "/tmp/alice_multi.log" &
    local alice_pid=$!
    
    sleep 1
    
    # Bob отправляет charlie
    run_client_with_commands "bob" "send charlie Hi Charlie!\nstatus\nquit" "/tmp/bob_multi.log" &
    local bob_pid=$!
    
    sleep 1
    
    # Charlie отправляет alice и bob
    echo -e "${BLUE}Charlie sending messages to multiple clients...${NC}"
    local charlie_commands="send alice Hello from Charlie!
send bob Hey Bob!
status
quit"
    
    run_client_with_commands "charlie" "$charlie_commands" "/tmp/charlie_multi.log"
    
    wait $alice_pid 2>/dev/null || true
    wait $bob_pid 2>/dev/null || true
    
    echo -e "${GREEN}Scenario 5 completed${NC}"
    echo ""
    echo -e "${CYAN}--- Alice's output ---${NC}"
    cat /tmp/alice_multi.log
    echo ""
    echo -e "${CYAN}--- Bob's output ---${NC}"
    cat /tmp/bob_multi.log
    echo ""
    echo -e "${CYAN}--- Charlie's output ---${NC}"
    cat /tmp/charlie_multi.log
}

# ==========================================
# Main
# ==========================================

main() {
    check_binaries
    cleanup_old_files
    
    # Устанавливаем обработчик для очистки при выходе
    trap stop_broker EXIT INT TERM
    
    # Запускаем брокер
    start_broker
    
    # Запускаем сценарии
    test_scenario_1
    sleep 2
    
    test_scenario_2
    sleep 2
    
    test_scenario_3
    sleep 2
    
    test_scenario_4
    sleep 2
    
    test_scenario_5
    
    echo ""
    echo -e "${GREEN}=========================================="
    echo "  All test scenarios completed!"
    echo "==========================================${NC}"
    echo ""
    echo -e "${YELLOW}Broker log (last 30 lines):${NC}"
    echo "---"
    if [ -f "$BROKER_LOG" ]; then
        tail -30 "$BROKER_LOG"
    else
        echo "No log file found"
    fi
    echo "---"
}

# Запуск
main