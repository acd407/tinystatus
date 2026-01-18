CC = gcc
EXEC = tinystatus
CFLAGS = -Wall -Wextra -MMD -MP -Isrc/include -std=gnu23
LDFLAGS = -lm

ifeq ($(DEBUG),1)
CFLAGS += -g -DDEBUG -Og -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address -fno-omit-frame-pointer
else
CFLAGS += -O2
LDFLAGS += -s
endif

CFLAGS += $(shell pkg-config --cflags libcjson)
CFLAGS += $(shell pkg-config --cflags dbus-1)
LDFLAGS += $(shell pkg-config --libs libcjson)
LDFLAGS += $(shell pkg-config --libs alsa)
LDFLAGS += $(shell pkg-config --libs dbus-1)

SOURCES = $(wildcard src/*.c) $(wildcard src/modules/*.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))
DEPENDS = $(OBJECTS:.o=.d)

all: $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(EXEC) $(OBJECTS) $(DEPENDS)

.PHONY: all clean

-include $(DEPENDS)
