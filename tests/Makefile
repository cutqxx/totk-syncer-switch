CC := gcc

CORE_DIR := ../source/core
CJSON_DIR := ../third_party/cjson
BUILD_DIR := build

CFLAGS := -std=c11 -Wall -Wextra -Werror -g -MMD -MP -I$(CORE_DIR) -I$(CJSON_DIR) -I. -DTS_CORE_DIR=\"$(CORE_DIR)\"

CORE_SOURCES := $(wildcard $(CORE_DIR)/*.c)
TEST_SOURCES := $(wildcard *.c)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/core_%.o,$(notdir $(CORE_SOURCES))) \
           $(patsubst %.c,$(BUILD_DIR)/test_%.o,$(notdir $(TEST_SOURCES))) \
           $(BUILD_DIR)/cJSON.o

DEPS := $(OBJECTS:.o=.d)

RUNNER := $(BUILD_DIR)/run_tests.exe

.PHONY: all clean
all: $(RUNNER)
	rm -rf $(BUILD_DIR)/tmp
	mkdir -p $(BUILD_DIR)/tmp
	./$(RUNNER)

$(RUNNER): $(OBJECTS)
	$(CC) -o $@ $^

$(BUILD_DIR)/core_%.o: $(CORE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/cJSON.o: $(CJSON_DIR)/cJSON.c | $(BUILD_DIR)
	$(CC) -std=c11 -g -I$(CJSON_DIR) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)
