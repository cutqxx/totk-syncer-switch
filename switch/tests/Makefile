# Host-side tests (Linux/macOS, needs gcc/clang, libcurl dev headers, python3).
#   make -C switch/tests        -> builds and runs everything
CC      ?= cc
CFLAGS  ?= -O1 -g -Wall -Wextra -Werror=implicit-function-declaration -fsanitize=address,undefined
CFLAGS  += -std=gnu11 -I../third_party/miniz -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
SRCS    := test_host.c ../source/util.c ../source/log.c ../source/config.c ../source/propfind.c \
           ../source/savemeta.c ../source/backups.c ../source/savezip.c ../source/webdav.c ../third_party/miniz/miniz.c
OUT     := build

all: run

$(OUT)/test_host: $(SRCS) $(wildcard ../source/*.h)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lcurl

run: $(OUT)/test_host
	./run.sh $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
