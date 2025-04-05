#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");

    char output_str[] = "\xf3\xb0\x8d\xb9\u20041.31G"; // 󰍹

    uint64_t usage = read_uint64_file (GPU_USAGE);
    if (modules[module_id].state) {
        format_storage_units (output_str + 7, read_uint64_file (VRAM_USED));
    } else {
        snprintf (
            output_str + 4, sizeof (output_str) - 4, "\u2004%*ld%%",
            usage < 100 ? 2 : 3, usage
        );
    }
    cJSON_AddStringToObject (json, "full_text", output_str);

    char *colors[] = {IDLE, WARNING, CRITICAL};
    size_t idx;
    if (usage < 30)
        idx = 0;
    else if (usage < 60)
        idx = 1;
    else
        idx = 2;
    cJSON_AddStringToObject (json, "color", colors[idx]);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

void init_gpu (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE ();

    modules[module_id].alter = alter;
    modules[module_id].update = update;
    modules[module_id].interval = 1;

    UPDATE_Q ();
}
