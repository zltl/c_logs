.PHONY: clean all example

.ONESHELL:

ROOT_DIR:=$(shell dirname $(realpath $(firstword) $(MAKEFILE_LIST)))
ifeq ($(TARGET_DIR),)
    TARGET_DIR = $(ROOT_DIR)/target
endif

$(shell mkdir -p $(TARGET_DIR))

ifneq ($(LOGS_DEBUG),)
	DEBUG_FLAGS = -DLOGS_DEBUG
	SANITIZER_FLAGS = -fsanitize=address -lasan
endif

COMMON_FLAGS += -Wall -Wextra -Werror -ggdb -Wno-unused-result \
	-I$(ROOT_DIR)/src $(DEBUG_FLAGS) $(SANITIZER_FLAGS)

CFLAGS += -std=c11 $(COMMON_FLAGS)
CXXFLAGS += -std=c++17 $(COMMON_FLAGS)
LDFLAGS ?=

export

libclogs:
	$(MAKE) -C $(ROOT_DIR)/src

example: libclogs $(ROOT_DIR)/example/example.c
	$(CC) $(CFLAGS) -o $(TARGET_DIR)/example $(ROOT_DIR)/example/example.c $(TARGET_DIR)/libclogs.a

clean:
	rm -rf $(ROOT_DIR)/target
