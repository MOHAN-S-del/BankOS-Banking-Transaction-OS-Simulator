CXX = clang++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -pthread -Iinclude

SRCS = src/main.cpp src/server/http_server.cpp
TARGET = adaptive_ledger_os

TEST_SRCS = tests/test_mlfq_scheduler.cpp \
            tests/test_telemetry_mdacc.cpp \
            tests/test_lock_manager.cpp \
            tests/test_acid_storage.cpp \
            tests/test_stress_concurrency.cpp

TEST_BINS = build/test_mlfq_scheduler \
            build/test_telemetry_mdacc \
            build/test_lock_manager \
            build/test_acid_storage \
            build/test_stress_concurrency

.PHONY: all clean test benchmark run

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

build/test_mlfq_scheduler: tests/test_mlfq_scheduler.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_mlfq_scheduler.cpp -o build/test_mlfq_scheduler

build/test_telemetry_mdacc: tests/test_telemetry_mdacc.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_telemetry_mdacc.cpp -o build/test_telemetry_mdacc

build/test_lock_manager: tests/test_lock_manager.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_lock_manager.cpp -o build/test_lock_manager

build/test_acid_storage: tests/test_acid_storage.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_acid_storage.cpp -o build/test_acid_storage

build/test_stress_concurrency: tests/test_stress_concurrency.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_stress_concurrency.cpp -o build/test_stress_concurrency

test: $(TEST_BINS)
	@echo "=== RUNNING ALL UNIT & INTEGRATION TESTS ==="
	@./build/test_mlfq_scheduler
	@./build/test_telemetry_mdacc
	@./build/test_lock_manager
	@./build/test_acid_storage
	@./build/test_stress_concurrency
	@echo "=== ALL TESTS COMPLETED SUCCESSFULLY ==="

benchmark: $(TARGET)
	./$(TARGET) --benchmark

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build $(TARGET)
