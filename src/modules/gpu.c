#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>

#define GPU_USAGE "/sys/class/drm/card0/device/gpu_busy_percent"
#define VRAM_USED "/sys/class/drm/card0/device/mem_info_vram_used"

static void alter (size_t module_id, uint64_t btn) {
    switch (btn) {
    case 3: // right button
        modules[module_id].state ^= 1;
        modules[module_id].interval = modules[module_id].state + 1;
        break;
    default:
        return;
    }
    modules[module_id].update (module_id);
}

static void update (size_t module_id) {
    char output_str[] = "󰍹\u2004\0.00G";

    uint64_t usage = read_uint64_file (GPU_USAGE);
    if (modules[module_id].state) {
        format_storage_units (
            (char (*)[6]) (output_str + strlen (output_str)),
            read_uint64_file (VRAM_USED)
        );
    } else {
        snprintf (
            output_str + 4, sizeof (output_str) - 4, "\u2004%*ld%%",
            usage < 100 ? 2 : 3, usage
        );
    }

    char *colors[] = {IDLE, WARNING, CRITICAL};
    size_t idx;
    if (usage < 30)
        idx = 0;
    else if (usage < 60)
        idx = 1;
    else
        idx = 2;

    update_json (module_id, output_str, colors[idx]);
}

void init_gpu (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE;

    modules[module_id].alter = alter;
    modules[module_id].update = update;
    modules[module_id].interval = 1;

    UPDATE_Q (module_id);
}
