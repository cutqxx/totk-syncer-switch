CC := gcc

CORE_DIR := ../source/core
CJSON_DIR := ../third_party/cjson
MINIZ_DIR := ../third_party/miniz
MINIZ_DEFINES := -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_STDIO -DMINIZ_NO_ZLIB_APIS
BUILD_DIR := build

CFLAGS := -std=c11 -Wall -Wextra -Werror -g -MMD -MP -I$(CORE_DIR) -I$(CJSON_DIR) -I$(MINIZ_DIR) $(MINIZ_DEFINES) -I. -DTS_CORE_DIR=\"$(CORE_DIR)\"

CORE_SOURCES := $(wildcard $(CORE_DIR)/*.c)
TEST_SOURCES := $(wildcard *.c)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/core_%.o,$(notdir $(CORE_SOURCES))) \
           $(patsubst %.c,$(BUILD_DIR)/test_%.o,$(notdir $(TEST_SOURCES))) \
           $(BUILD_DIR)/cJSON.o $(BUILD_DIR)/miniz.o

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

$(BUILD_DIR)/miniz.o: $(MINIZ_DIR)/miniz.c | $(BUILD_DIR)
	$(CC) -std=c11 -g -w -I$(MINIZ_DIR) $(MINIZ_DEFINES) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)
