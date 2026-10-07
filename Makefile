
# Makefile for High-Performance Multi-Threaded Parallel K-Means
# Compiler configuration
CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=c11 -Iinclude
PTHREAD_FLAGS ?= -pthread

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests

# Sources & Objects for Person 1
PERSON1_SRCS = $(SRC_DIR)/task_queue.c $(SRC_DIR)/thread_pool.c
PERSON1_OBJS = $(OBJ_DIR)/task_queue.o $(OBJ_DIR)/thread_pool.o

all: test_person1

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

test_person1: $(PERSON1_OBJS) $(TEST_DIR)/test_thread_pool.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) $(TEST_DIR)/test_thread_pool.c $(PERSON1_OBJS) -o $(BIN_DIR)/test_thread_pool.exe

benchmark_scheduler: $(PERSON1_OBJS) benchmarks/benchmark_scheduler.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) benchmarks/benchmark_scheduler.c $(PERSON1_OBJS) -o $(BIN_DIR)/benchmark_scheduler.exe

hard_comparison_benchmark: $(PERSON1_OBJS) benchmarks/hard_comparison_benchmark.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) benchmarks/hard_comparison_benchmark.c $(PERSON1_OBJS) -o $(BIN_DIR)/hard_comparison_benchmark.exe -lm

test: test_person1
	./$(BIN_DIR)/test_thread_pool.exe

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean test test_person1
