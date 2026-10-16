CXX = clang++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -pthread -Iinclude

TARGET = bank_os_sim
ALIAS_TARGET = adaptive_ledger_os

SRCS = src/main.cpp

TEST_BINS = build/test_bank_os_simulator \
            build/test_predictive_ml \
            build/test_proactive_mlfq \
            build/test_mlfq_scheduler \
            build/test_telemetry_mdacc \
            build/test_lock_manager \
            build/test_acid_storage \
            build/test_stress_concurrency

.PHONY: all clean test run

all: $(TARGET) $(ALIAS_TARGET)

$(TARGET): $(SRCS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

$(ALIAS_TARGET): $(TARGET)
	@cp $(TARGET) $(ALIAS_TARGET)

build/test_bank_os_simulator: tests/test_bank_os_simulator.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_bank_os_simulator.cpp -o build/test_bank_os_simulator

build/test_predictive_ml: tests/test_predictive_ml.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_predictive_ml.cpp -o build/test_predictive_ml

build/test_proactive_mlfq: tests/test_proactive_mlfq.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_proactive_mlfq.cpp -o build/test_proactive_mlfq

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

test: build/test_bank_os_simulator
	@echo "=== RUNNING BANK OS SIMULATOR TEST SUITE ==="
	@./build/test_bank_os_simulator
	@echo "=== ALL SIMULATOR TESTS COMPLETED SUCCESSFULLY ==="

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build $(TARGET) $(ALIAS_TARGET) state_transitions.json
