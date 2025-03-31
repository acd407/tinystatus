CC = gcc
CFLAGS = -Wall -Wextra -MMD -MP
LDFLAGS = -ljson-c

SRC_DIR = src
OBJ_DIR = obj

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPENDS = $(OBJECTS:.o=.d)

CFLAGS += -Iinclude $(shell pkg-config --cflags libcjson)
LDFLAGS += $(shell pkg-config --libs libcjson) -lm

EXEC = i3bar-oop

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

-include $(DEPS)
