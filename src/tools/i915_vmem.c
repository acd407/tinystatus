#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

uint64_t parse_hex(const char *str) {
    return strtoull(str, NULL, 16);
}

int get_i915_gem_memory(uint64_t *gem_bytes) {
    FILE *f = NULL;
    char path[256];

    // 遍历dri设备，找到第一个可以打开的
    for (int i = 0; i < 16; i++) {
        snprintf(path, sizeof(path), "/sys/kernel/debug/dri/%d/i915_gem_objects", i);
        f = fopen(path, "r");
        if (f) {
            break;
        }
    }

    if (!f) {
        return -1;
    }

    char line[256];
    // 第一行格式: "401 shrinkable [0 free] objects, 911364096 bytes"
    if (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "objects, ");
        if (p) {
            *gem_bytes = strtoull(p + 9, NULL, 10);
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return -1;
}

int main(void) {
    uint64_t gem_bytes;

    while (1) {
        if (get_i915_gem_memory(&gem_bytes) == 0) {
            printf("%lu\n", gem_bytes);
            fflush(stdout);
        } else {
            fprintf(stderr, "Failed to read i915_gem_objects\n");
        }
        sleep(1);
    }

    return 0;
}
