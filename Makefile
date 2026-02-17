CC = gcc
EXEC = tinystatus
CFLAGS = -Wall -Wextra -MMD -MP -Isrc/include -std=gnu23 -DTOOLS_DIR=\"$(PWD)\"
LDFLAGS = -lm

ifeq ($(DEBUG),1)
CFLAGS += -g -DDEBUG -Og -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
else
CFLAGS += -O2 -ffunction-sections -fdata-sections
LDFLAGS += -s -Wl,--gc-sections
endif

CFLAGS += $(shell pkg-config --cflags libcjson dbus-1 libpulse libnl-3.0 libnl-genl-3.0 glib-2.0)
LDFLAGS += $(shell pkg-config --libs libcjson dbus-1 libpulse libnl-3.0 libnl-genl-3.0 glib-2.0)

SOURCES = $(wildcard src/*.c) $(wildcard src/modules/*.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))
DEPENDS = $(OBJECTS:.o=.d)

TOOLS = i915_gpu_usage i915_vmem

all: $(EXEC) $(TOOLS)

$(EXEC): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

# 编译工具程序
i915_gpu_usage: src/tools/i915_gpu_usage.o
	$(CC) $^ -o $@

i915_vmem: src/tools/i915_vmem.o
	$(CC) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(EXEC) $(TOOLS) $(OBJECTS) $(DEPENDS)

.PHONY: all clean

-include $(DEPENDS)
