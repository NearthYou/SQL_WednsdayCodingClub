CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g

TARGET := sql_processor
CORE_OBJS := src/parser.o src/statement.o src/sql_error.o
PHASE2_OBJS := src/execute.o src/storage.o
APP_OBJS := src/main.o $(CORE_OBJS) $(PHASE2_OBJS)
TEST_OBJS := tests/test_parser.o $(CORE_OBJS) $(PHASE2_OBJS)
TEST_TARGET := test_parser

.PHONY: all test clean

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(APP_OBJS)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) src/*.o tests/*.o
