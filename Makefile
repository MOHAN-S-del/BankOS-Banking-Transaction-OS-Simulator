CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude -pthread

# Detect MySQL installation on macOS / Linux
MYSQL_INCLUDE = /usr/local/mysql-8.4.10-macos15-arm64/include
MYSQL_LIB = /usr/local/mysql-8.4.10-macos15-arm64/lib

# Check if MySQL directory exists
ifneq ($(wildcard $(MYSQL_INCLUDE)/mysql.h),)
    CXXFLAGS += -DHAVE_MYSQL -I$(MYSQL_INCLUDE)
    LDFLAGS += -L$(MYSQL_LIB) -Wl,-rpath,$(MYSQL_LIB) -lmysqlclient
else
    # Check alternate Homebrew MySQL path
    ifneq ($(wildcard /opt/homebrew/include/mysql/mysql.h),)
        CXXFLAGS += -DHAVE_MYSQL -I/opt/homebrew/include/mysql
        LDFLAGS += -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lmysqlclient
    endif
endif


SRC = src/main.cpp \
      src/db_manager.cpp \
      src/process_manager.cpp \
      src/lock_manager.cpp \
      src/scheduler.cpp \
      src/http_server.cpp \
      src/bank_os_api.cpp

OBJ = $(SRC:.cpp=.o)
TARGET = bank_os_simulator

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) test_scheduler test_deadlock test_atomicity

test: test_scheduler test_deadlock test_atomicity
	@echo "================ RUNNING AUTOMATED UNIT TESTS ================"
	./test_scheduler
	./test_deadlock
	./test_atomicity
	@echo "================ ALL UNIT TESTS PASSED ======================="

test_scheduler: tests/test_scheduler.cpp src/process_manager.cpp src/scheduler.cpp src/lock_manager.cpp src/db_manager.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_deadlock: tests/test_deadlock.cpp src/lock_manager.cpp src/db_manager.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_atomicity: tests/test_atomicity.cpp src/db_manager.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run test

