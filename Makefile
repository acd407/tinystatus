CC = gcc
EXEC = tinystatus
CFLAGS = -Wall -Wextra -MMD -MP -Iinclude
LDFLAGS = -lm -s

CFLAGS += $(shell pkg-config --cflags libcjson)
LDFLAGS += $(shell pkg-config --libs libcjson)
LDFLAGS += $(shell pkg-config --libs alsa)

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
