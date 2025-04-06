CC = gcc
EXEC = tinystatus
CFLAGS = -Wall -Wextra -MMD -MP -Isrc/include -O2 -std=gnu23
LDFLAGS = -lm -s

CFLAGS += $(shell pkg-config --cflags libcjson)
CFLAGS += $(shell pkg-config --cflags dbus-1)
LDFLAGS += $(shell pkg-config --libs libcjson)
LDFLAGS += $(shell pkg-config --libs alsa)
LDFLAGS += $(shell pkg-config --libs dbus-1)

SRC_DIR = src
OBJ_DIR = obj

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPENDS = $(OBJECTS:.o=.d)

all: $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(EXEC)

.PHONY: all clean

-include $(DEPENDS)
